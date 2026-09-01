"""
Read the public charging-station XLSX file and convert it to project API JSON.

This script is for the MiniSemester EV charging project. It reads the original
public Excel dataset directly, cleans fields, optionally geocodes real addresses
with Tencent Map WebService, and writes JSON that follows the agreed contract:

- base API path: /api/v1
- response envelope: code/message/data/request_id
- JSON fields: snake_case
- money: cents, e.g. price_cents_per_kwh
- energy: Wh
- power: W
- timestamps: ISO 8601 UTC
- station status: ACTIVE / INACTIVE
- pile status: IDLE / RESERVED / CHARGING / FAULT / OFFLINE
- pile type: FAST / SLOW

Output structure:

{
  "metadata": {...},
  "seed_data": {
    "stations": [...],
    "charging_piles": [...]
  },
  "api_responses": {
    "GET /api/v1/stations": {...},
    "GET /api/v1/dashboard/overview": {...},
    "GET /api/v1/dashboard/pile-status": {...},
    "GET /api/v1/dashboard/station-ranking": {...}
  },
  "skipped_rows": [...]
}

Example:

python tools/xlsx_to_api_json.py ^
  --input "D:/北理文件/课程/计算机软件和大数据开发/充电站基本信息（社会公用）.xlsx" ^
  --output data/stations_api.json ^
  --tencent-key YOUR_KEY
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import re
import time
import urllib.parse
import urllib.request
from collections import Counter, defaultdict
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Dict, Iterable, List, Optional, Tuple

try:
    import pandas as pd
except ImportError as exc:  # pragma: no cover
    raise SystemExit("Missing dependency: pandas/openpyxl. Install with: pip install pandas openpyxl") from exc

TENCENT_GEOCODER_URL = "https://apis.map.qq.com/ws/geocoder/v1/"

COLUMN_MAP = {
    "source_index": "序号",
    "operator_name": "运营商",
    "name": "充电站点标准名称",
    "service_type": "服务类型",
    "address": "区县具体地址",
    "fast_count": "快充接口数",
    "slow_count": "慢充接口数",
    "region_range": "地域范围",
    "location_type": "所在地类型",
}

PILE_STATUS_CYCLE = ["IDLE", "IDLE", "IDLE", "CHARGING", "RESERVED", "FAULT", "OFFLINE"]
PILE_STATUS_TEXT = {
    "IDLE": "空闲",
    "RESERVED": "已预约",
    "CHARGING": "充电中",
    "FAULT": "故障",
    "OFFLINE": "离线",
}


def utc_now() -> str:
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def clean_text(value: Any, default: str = "") -> str:
    if value is None:
        return default
    if isinstance(value, float) and math.isnan(value):
        return default
    text = str(value).strip()
    return text if text else default


def to_int(value: Any, default: int = 0) -> int:
    if value is None:
        return default
    if isinstance(value, float) and math.isnan(value):
        return default
    try:
        return max(0, int(float(value)))
    except (TypeError, ValueError):
        match = re.search(r"\d+", str(value))
        return int(match.group()) if match else default


def stable_fraction(text: str, salt: str) -> float:
    digest = hashlib.sha1(f"{text}|{salt}".encode("utf-8")).hexdigest()
    return int(digest[:8], 16) / 0xFFFFFFFF


def fallback_coordinate(seed: str, center: float, spread: float, salt: str) -> float:
    offset = (stable_fraction(seed, salt) - 0.5) * spread
    return round(center + offset, 7)


def normalize_address(address: str, city_prefix: str) -> str:
    address = clean_text(address)
    if not address:
        return ""
    if city_prefix and not address.startswith(city_prefix):
        return f"{city_prefix}{address}"
    return address


def load_cache(path: Optional[Path]) -> Dict[str, Any]:
    if not path or not path.exists():
        return {}
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError:
        return {}


def save_cache(path: Optional[Path], cache: Dict[str, Any]) -> None:
    if not path:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(cache, ensure_ascii=False, indent=2), encoding="utf-8")


def call_tencent_geocoder(address: str, key: str, timeout: int) -> Dict[str, Any]:
    query = urllib.parse.urlencode({"address": address, "key": key})
    request = urllib.request.Request(
        f"{TENCENT_GEOCODER_URL}?{query}",
        headers={"User-Agent": "MiniSemester-XlsxToApiJson/1.0"},
    )
    with urllib.request.urlopen(request, timeout=timeout) as response:
        return json.loads(response.read().decode("utf-8"))


def geocode_address(
    address: str,
    key: Optional[str],
    cache: Dict[str, Any],
    timeout: int,
    retries: int,
    retry_delay: float,
) -> Dict[str, Any]:
    if not key:
        return {"status": "SKIPPED", "latitude": None, "longitude": None, "message": "no tencent key"}
    if address in cache:
        cached = dict(cache[address])
        cached["from_cache"] = True
        return cached

    last_error = ""
    for attempt in range(retries + 1):
        try:
            payload = call_tencent_geocoder(address, key, timeout)
            status = payload.get("status")
            message = payload.get("message", "")
            location = payload.get("result", {}).get("location")
            if status == 0 and location:
                result = {
                    "status": "OK",
                    "latitude": float(location["lat"]),
                    "longitude": float(location["lng"]),
                    "message": message,
                    "title": payload.get("result", {}).get("title"),
                    "ad_info": payload.get("result", {}).get("ad_info"),
                    "from_cache": False,
                }
                cache[address] = {k: v for k, v in result.items() if k != "from_cache"}
                return result
            last_error = f"status={status}, message={message}"
        except Exception as exc:  # noqa: BLE001
            last_error = str(exc)
        if attempt < retries:
            time.sleep(retry_delay)

    result = {"status": "FAILED", "latitude": None, "longitude": None, "error": last_error, "from_cache": False}
    cache[address] = {k: v for k, v in result.items() if k != "from_cache"}
    return result


def read_rows(input_path: Path, sheet_name: Optional[str]) -> Iterable[Dict[str, Any]]:
    df = pd.read_excel(input_path, sheet_name=sheet_name or 0)
    missing = [column for column in COLUMN_MAP.values() if column not in df.columns]
    if missing:
        raise ValueError(f"Excel missing required columns: {', '.join(missing)}")
    return df.to_dict(orient="records")


def build_pile_no(station_id: int, pile_type: str, sequence: int) -> str:
    type_code = "F" if pile_type == "FAST" else "S"
    return f"BJ-{station_id:04d}-{type_code}-{sequence:03d}"


def build_station_summary(station: Dict[str, Any], piles: List[Dict[str, Any]]) -> Dict[str, Any]:
    station_piles = [pile for pile in piles if pile["station_id"] == station["id"]]
    total = len(station_piles)
    available = sum(1 for pile in station_piles if pile["status"] == "IDLE")
    online = sum(1 for pile in station_piles if pile["status"] in {"IDLE", "RESERVED", "CHARGING", "FAULT"})
    return {
        "id": station["id"],
        "name": station["name"],
        "address": station["address"],
        "latitude": station["latitude"],
        "longitude": station["longitude"],
        "price_cents_per_kwh": station["price_cents_per_kwh"],
        "status": station["status"],
        "total_piles": total,
        "available_piles": available,
        "online_rate": round(online / total * 100, 2) if total else 0,
        "distance_km": None,
    }


def response(data: Any, request_id: str) -> Dict[str, Any]:
    return {"code": 0, "message": "success", "data": data, "request_id": request_id}


def build_api_responses(stations: List[Dict[str, Any]], piles: List[Dict[str, Any]], generated_at: str) -> Dict[str, Any]:
    summaries = [build_station_summary(station, piles) for station in stations]
    total_piles = len(piles)
    status_counts = Counter(pile["status"] for pile in piles)
    online_count = sum(status_counts[status] for status in ["IDLE", "RESERVED", "CHARGING", "FAULT"])
    available_count = status_counts["IDLE"]
    status_items = []
    for status in ["IDLE", "RESERVED", "CHARGING", "FAULT", "OFFLINE"]:
        count = status_counts[status]
        status_items.append({
            "status": status,
            "status_text": PILE_STATUS_TEXT[status],
            "count": count,
            "percentage": round(count / total_piles * 100, 2) if total_piles else 0,
        })

    station_order_counts: Dict[int, int] = defaultdict(int)
    station_energy_wh: Dict[int, int] = defaultdict(int)
    station_revenue_cents: Dict[int, int] = defaultdict(int)
    for summary in summaries:
        station_id = summary["id"]
        total = summary["total_piles"]
        used = max(0, total - summary["available_piles"])
        station_order_counts[station_id] = used * 8
        station_energy_wh[station_id] = used * 42000
        station_revenue_cents[station_id] = round(station_energy_wh[station_id] / 1000 * summary["price_cents_per_kwh"])

    ranking_items = []
    for summary in summaries:
        station_id = summary["id"]
        total = summary["total_piles"]
        available = summary["available_piles"]
        utilization = round((total - available) / total * 100, 2) if total else 0
        ranking_items.append({
            "station_id": station_id,
            "station_name": summary["name"],
            "revenue_cents": station_revenue_cents[station_id],
            "order_count": station_order_counts[station_id],
            "energy_wh": station_energy_wh[station_id],
            "utilization_rate": utilization,
        })
    ranking_items.sort(key=lambda item: item["revenue_cents"], reverse=True)

    overview = {
        "today_revenue_cents": sum(item["revenue_cents"] for item in ranking_items[:20]),
        "total_revenue_cents": sum(item["revenue_cents"] for item in ranking_items),
        "today_order_count": sum(item["order_count"] for item in ranking_items[:20]),
        "total_order_count": sum(item["order_count"] for item in ranking_items),
        "today_energy_wh": sum(item["energy_wh"] for item in ranking_items[:20]),
        "charging_order_count": status_counts["CHARGING"],
        "station_count": len(stations),
        "pile_count": total_piles,
        "available_pile_count": available_count,
        "online_rate": round(online_count / total_piles * 100, 2) if total_piles else 0,
        "updated_at": generated_at,
    }

    return {
        "GET /api/v1/stations": response({
            "items": summaries,
            "pagination": {
                "page": 1,
                "page_size": len(summaries),
                "total": len(summaries),
                "total_pages": 1,
            },
        }, "mock-stations-list"),
        "GET /api/v1/dashboard/overview": response(overview, "mock-dashboard-overview"),
        "GET /api/v1/dashboard/pile-status": response({"total": total_piles, "items": status_items}, "mock-dashboard-pile-status"),
        "GET /api/v1/dashboard/station-ranking": response({
            "metric": "revenue",
            "days": 30,
            "items": ranking_items[:10],
        }, "mock-dashboard-station-ranking"),
    }


def convert(args: argparse.Namespace) -> Dict[str, Any]:
    generated_at = utc_now()
    rows = list(read_rows(Path(args.input), args.sheet))
    if args.limit is not None:
        rows = rows[:args.limit]

    cache_path = Path(args.cache) if args.cache else None
    cache = load_cache(cache_path)
    stations: List[Dict[str, Any]] = []
    charging_piles: List[Dict[str, Any]] = []
    skipped_rows: List[Dict[str, Any]] = []
    geocode_counts = Counter()
    pile_id = 1

    for raw_index, raw in enumerate(rows, start=1):
        source_index = to_int(raw.get(COLUMN_MAP["source_index"]), raw_index)
        name = clean_text(raw.get(COLUMN_MAP["name"]))
        address = clean_text(raw.get(COLUMN_MAP["address"]))
        fast_count = to_int(raw.get(COLUMN_MAP["fast_count"]))
        slow_count = to_int(raw.get(COLUMN_MAP["slow_count"]))

        if not name or not address or fast_count + slow_count <= 0:
            skipped_rows.append({"source_index": source_index, "reason": "missing name/address or no charging interfaces"})
            continue

        station_id = len(stations) + 1
        geocode_query = normalize_address(address, args.city_prefix)
        geo = geocode_address(geocode_query, args.tencent_key, cache, args.timeout, args.retries, args.retry_delay)
        geocode_counts[geo["status"]] += 1
        if geo.get("from_cache"):
            geocode_counts["CACHED"] += 1

        seed = f"{name}|{address}|{source_index}"
        latitude = geo.get("latitude")
        longitude = geo.get("longitude")
        if latitude is None or longitude is None:
            latitude = fallback_coordinate(seed, args.center_latitude, args.coordinate_spread, "lat")
            longitude = fallback_coordinate(seed, args.center_longitude, args.coordinate_spread, "lng")

        station = {
            "id": station_id,
            "source_index": source_index,
            "name": name,
            "address": address,
            "latitude": round(float(latitude), 7),
            "longitude": round(float(longitude), 7),
            "price_cents_per_kwh": args.price_cents_per_kwh,
            "status": "ACTIVE",
            "operator_name": clean_text(raw.get(COLUMN_MAP["operator_name"]), "未知运营商"),
            "service_type": clean_text(raw.get(COLUMN_MAP["service_type"]), "社会公用"),
            "region_range": clean_text(raw.get(COLUMN_MAP["region_range"]), "未知"),
            "location_type": clean_text(raw.get(COLUMN_MAP["location_type"]), "未知"),
            "geocode_source": "tencent" if args.tencent_key else "fallback",
            "geocode_status": geo["status"],
            "geocode_query": geocode_query,
            "created_at": generated_at,
            "updated_at": generated_at,
        }
        if geo.get("title"):
            station["geocode_title"] = geo["title"]
        if geo.get("ad_info"):
            station["geocode_ad_info"] = geo["ad_info"]
        if geo.get("error"):
            station["geocode_error"] = geo["error"]
        stations.append(station)

        for sequence in range(1, fast_count + 1):
            status = PILE_STATUS_CYCLE[(pile_id - 1) % len(PILE_STATUS_CYCLE)]
            charging_piles.append({
                "id": pile_id,
                "pile_no": build_pile_no(station_id, "FAST", sequence),
                "station_id": station_id,
                "station_name": name,
                "pile_type": "FAST",
                "rated_power_w": args.fast_power_w,
                "status": status,
                "last_heartbeat_at": generated_at if status != "OFFLINE" else None,
                "created_at": generated_at,
                "updated_at": generated_at,
            })
            pile_id += 1

        for sequence in range(1, slow_count + 1):
            status = PILE_STATUS_CYCLE[(pile_id - 1) % len(PILE_STATUS_CYCLE)]
            charging_piles.append({
                "id": pile_id,
                "pile_no": build_pile_no(station_id, "SLOW", sequence),
                "station_id": station_id,
                "station_name": name,
                "pile_type": "SLOW",
                "rated_power_w": args.slow_power_w,
                "status": status,
                "last_heartbeat_at": generated_at if status != "OFFLINE" else None,
                "created_at": generated_at,
                "updated_at": generated_at,
            })
            pile_id += 1

        if args.tencent_key:
            time.sleep(args.delay)
        if cache_path and station_id % args.save_every == 0:
            save_cache(cache_path, cache)
            print(f"processed stations={station_id}, geocode={dict(geocode_counts)}")

    save_cache(cache_path, cache)

    output = {
        "metadata": {
            "source_file": str(Path(args.input)),
            "generated_at": generated_at,
            "schema_version": "v0.1",
            "base_api_path": "/api/v1",
            "field_style": "snake_case",
            "units": {
                "money": "cents",
                "price": "cents_per_kwh",
                "energy": "wh",
                "power": "w",
                "time": "iso_8601_utc",
            },
            "counts": {
                "source_rows": len(rows),
                "stations": len(stations),
                "charging_piles": len(charging_piles),
                "skipped_rows": len(skipped_rows),
                "geocode": dict(geocode_counts),
            },
        },
        "seed_data": {
            "stations": stations,
            "charging_piles": charging_piles,
        },
        "api_responses": build_api_responses(stations, charging_piles, generated_at),
        "skipped_rows": skipped_rows,
    }
    return output


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Convert charging station XLSX to project API JSON.")
    parser.add_argument("--input", required=True, help="Source .xlsx path")
    parser.add_argument("--output", required=True, help="Output .json path")
    parser.add_argument("--sheet", default=None, help="Sheet name. Defaults to first sheet")
    parser.add_argument("--limit", type=int, default=None, help="Convert first N rows only")
    parser.add_argument("--tencent-key", default=None, help="Tencent Map WebService key for real geocoding")
    parser.add_argument("--cache", default="data/geocode_cache_tencent.json", help="Tencent geocode cache path")
    parser.add_argument("--city-prefix", default="北京市", help="Address prefix for geocoding")
    parser.add_argument("--delay", type=float, default=0.12, help="Delay between Tencent API calls")
    parser.add_argument("--timeout", type=int, default=10, help="HTTP timeout seconds")
    parser.add_argument("--retries", type=int, default=2, help="Retries per address")
    parser.add_argument("--retry-delay", type=float, default=1.0, help="Delay before retry")
    parser.add_argument("--save-every", type=int, default=50, help="Save geocode cache every N stations")
    parser.add_argument("--price-cents-per-kwh", type=int, default=125, help="Default station price in cents/kWh")
    parser.add_argument("--fast-power-w", type=int, default=60000, help="Generated fast pile rated power")
    parser.add_argument("--slow-power-w", type=int, default=7000, help="Generated slow pile rated power")
    parser.add_argument("--center-latitude", type=float, default=39.9042, help="Fallback coordinate center latitude")
    parser.add_argument("--center-longitude", type=float, default=116.4074, help="Fallback coordinate center longitude")
    parser.add_argument("--coordinate-spread", type=float, default=0.7, help="Fallback coordinate spread")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    output = convert(args)
    output_path = Path(args.output)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(json.dumps(output, ensure_ascii=False, indent=2), encoding="utf-8")
    counts = output["metadata"]["counts"]
    print(f"Converted stations={counts['stations']}, piles={counts['charging_piles']}, skipped={counts['skipped_rows']}")
    print(f"Geocode summary: {counts['geocode']}")
    print(f"Output: {output_path}")


if __name__ == "__main__":
    main()
