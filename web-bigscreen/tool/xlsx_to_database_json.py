"""
Convert the public charging-station XLSX file to database-ready JSON.

This script follows database.md v0.2 for these two tables:

1. stations
2. charging_piles

Important rule from database.md:
The public dataset's fast/slow connector counts are stored on stations as summary fields. For this course project, these counts are also used to simulate charging_piles records directly, so station-detail and pile-status demos can run without another device dataset.

Example, database-correct station catalog JSON with Tencent geocoding:

python tools/xlsx_to_database_json.py ^
  --input "D:/北理文件/课程/计算机软件和大数据开发/充电站基本信息（社会公用）.xlsx" ^
  --output data/stations_database.json ^
  --tencent-key YOUR_TENCENT_MAP_KEY

Small trial run:

python tools/xlsx_to_database_json.py ^
  --input "D:/北理文件/课程/计算机软件和大数据开发/充电站基本信息（社会公用）.xlsx" ^
  --output data/stations_database_sample.json ^
  --limit 10
"""

from __future__ import annotations

import argparse
import json
import math
import re
import time
import urllib.parse
import urllib.request
from collections import Counter
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Dict, Iterable, List, Optional

try:
    import pandas as pd
except ImportError as exc:  # pragma: no cover
    raise SystemExit("Missing dependency: pandas/openpyxl. Install with: pip install pandas openpyxl") from exc

TENCENT_GEOCODER_URL = "https://apis.map.qq.com/ws/geocoder/v1/"
DATA_SOURCE = "BEIJING_PUBLIC_DATA_OPEN_PLATFORM"

COLUMN_MAP = {
    "external_id": "序号",
    "operator_name": "运营商",
    "name": "充电站点标准名称",
    "service_type": "服务类型",
    "address": "区县具体地址",
    "fast_connector_count": "快充接口数",
    "slow_connector_count": "慢充接口数",
    "region_scope": "地域范围",
    "location_type": "所在地类型",
}

BEIJING_DISTRICTS = [
    "东城区",
    "西城区",
    "朝阳区",
    "海淀区",
    "丰台区",
    "石景山区",
    "门头沟区",
    "房山区",
    "通州区",
    "顺义区",
    "昌平区",
    "大兴区",
    "怀柔区",
    "平谷区",
    "密云区",
    "延庆区",
]

PILE_STATUS_CYCLE = ["IDLE", "IDLE", "IDLE", "CHARGING", "FAULT", "OFFLINE"]


def utc_now() -> str:
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def clean_text(value: Any, default: Optional[str] = None) -> Optional[str]:
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


def detect_district(address: str, name: str = "") -> Optional[str]:
    text = f"{address or ''} {name or ''}"
    for district in BEIJING_DISTRICTS:
        if district in text:
            return district
    return None


def normalize_address(address: str, city_prefix: str) -> str:
    address = clean_text(address, "") or ""
    if not address:
        return ""
    if city_prefix and not address.startswith(city_prefix):
        return f"{city_prefix}{address}"
    return address


def read_json(path: Path) -> Dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def write_json(path: Path, data: Dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding="utf-8")


def load_cache(path: Optional[Path]) -> Dict[str, Any]:
    if not path or not path.exists():
        return {}
    try:
        return read_json(path)
    except json.JSONDecodeError:
        return {}


def save_cache(path: Optional[Path], cache: Dict[str, Any]) -> None:
    if path:
        write_json(path, cache)


def call_tencent_geocoder(address: str, key: str, timeout: int) -> Dict[str, Any]:
    query = urllib.parse.urlencode({"address": address, "key": key})
    request = urllib.request.Request(
        f"{TENCENT_GEOCODER_URL}?{query}",
        headers={"User-Agent": "MiniSemester-DatabaseSeed/1.0"},
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
                    "latitude": round(float(location["lat"]), 7),
                    "longitude": round(float(location["lng"]), 7),
                    "message": message,
                    "title": payload.get("result", {}).get("title"),
                    "ad_info": payload.get("result", {}).get("ad_info"),
                    "from_cache": False,
                }
                cache[address] = {key: value for key, value in result.items() if key != "from_cache"}
                return result
            last_error = f"status={status}, message={message}"
        except Exception as exc:  # noqa: BLE001 - keep batch conversion robust
            last_error = str(exc)
        if attempt < retries:
            time.sleep(retry_delay)

    result = {"status": "FAILED", "latitude": None, "longitude": None, "error": last_error, "from_cache": False}
    cache[address] = {key: value for key, value in result.items() if key != "from_cache"}
    return result


def read_rows(input_path: Path, sheet_name: Optional[str]) -> Iterable[Dict[str, Any]]:
    df = pd.read_excel(input_path, sheet_name=sheet_name or 0)
    missing = [column for column in COLUMN_MAP.values() if column not in df.columns]
    if missing:
        raise ValueError(f"Excel missing required columns: {', '.join(missing)}")
    return df.to_dict(orient="records")


def build_pile_no(station_id: int, pile_type: str, sequence: int) -> str:
    type_code = "F" if pile_type == "FAST" else "S"
    return f"DEMO-BJ-{station_id:04d}-{type_code}-{sequence:03d}"


def build_simulated_piles(station: Dict[str, Any], next_pile_id: int, args: argparse.Namespace, now: str) -> List[Dict[str, Any]]:
    if args.no_generate_piles:
        return []

    piles: List[Dict[str, Any]] = []
    fast_count = station["fast_connector_count"]
    slow_count = station["slow_connector_count"]

    pile_id = next_pile_id
    for sequence in range(1, fast_count + 1):
        status = PILE_STATUS_CYCLE[(pile_id - 1) % len(PILE_STATUS_CYCLE)]
        piles.append({
            "id": pile_id,
            "pile_no": build_pile_no(station["id"], "FAST", sequence),
            "station_id": station["id"],
            "pile_type": "FAST",
            "rated_power_w": args.fast_power_w,
            "status": status,
            "last_heartbeat_at": now if status != "OFFLINE" else None,
            "version": 1,
            "created_at": now,
            "updated_at": now,
        })
        pile_id += 1

    for sequence in range(1, slow_count + 1):
        status = PILE_STATUS_CYCLE[(pile_id - 1) % len(PILE_STATUS_CYCLE)]
        piles.append({
            "id": pile_id,
            "pile_no": build_pile_no(station["id"], "SLOW", sequence),
            "station_id": station["id"],
            "pile_type": "SLOW",
            "rated_power_w": args.slow_power_w,
            "status": status,
            "last_heartbeat_at": now if status != "OFFLINE" else None,
            "version": 1,
            "created_at": now,
            "updated_at": now,
        })
        pile_id += 1

    return piles


def convert(args: argparse.Namespace) -> Dict[str, Any]:
    now = utc_now()
    input_path = Path(args.input)
    rows = list(read_rows(input_path, args.sheet))
    if args.limit is not None:
        rows = rows[:args.limit]

    cache_path = Path(args.cache) if args.cache else None
    cache = load_cache(cache_path)
    geocode_counts = Counter()
    stations: List[Dict[str, Any]] = []
    charging_piles: List[Dict[str, Any]] = []
    skipped_rows: List[Dict[str, Any]] = []

    for raw_row_number, raw in enumerate(rows, start=1):
        external_id = str(to_int(raw.get(COLUMN_MAP["external_id"]), raw_row_number))
        name = clean_text(raw.get(COLUMN_MAP["name"]))
        address = clean_text(raw.get(COLUMN_MAP["address"]))
        fast_count = to_int(raw.get(COLUMN_MAP["fast_connector_count"]))
        slow_count = to_int(raw.get(COLUMN_MAP["slow_connector_count"]))

        if not name or not address:
            skipped_rows.append({
                "row_number": raw_row_number,
                "external_id": external_id,
                "reason": "missing station name or address",
            })
            continue

        geocode_query = normalize_address(address, args.city_prefix)
        geo = geocode_address(geocode_query, args.tencent_key, cache, args.timeout, args.retries, args.retry_delay)
        geocode_counts[geo["status"]] += 1
        if geo.get("from_cache"):
            geocode_counts["CACHED"] += 1

        station = {
            "id": len(stations) + 1,
            "name": name[:64],
            "address": address[:255],
            "latitude": geo.get("latitude"),
            "longitude": geo.get("longitude"),
            "price_cents_per_kwh": args.price_cents_per_kwh,
            "operator_name": clean_text(raw.get(COLUMN_MAP["operator_name"])) or None,
            "service_type": clean_text(raw.get(COLUMN_MAP["service_type"])) or None,
            "district": detect_district(address, name),
            "region_scope": clean_text(raw.get(COLUMN_MAP["region_scope"])) or None,
            "location_type": clean_text(raw.get(COLUMN_MAP["location_type"])) or None,
            "fast_connector_count": fast_count,
            "slow_connector_count": slow_count,
            "data_source": DATA_SOURCE,
            "external_id": external_id,
            "status": "ACTIVE",
            "created_at": now,
            "updated_at": now,
        }
        stations.append(station)

        simulated_piles = build_simulated_piles(station, len(charging_piles) + 1, args, now)
        charging_piles.extend(simulated_piles)

        if args.tencent_key:
            time.sleep(args.delay)
        if cache_path and len(stations) % args.save_every == 0:
            save_cache(cache_path, cache)
            print(f"processed stations={len(stations)}, geocode={dict(geocode_counts)}")

    save_cache(cache_path, cache)

    result = {
        "metadata": {
            "source_file": str(input_path),
            "generated_at": now,
            "database_document": "database.md v0.2",
            "target_tables": ["stations", "charging_piles"],
            "data_source": DATA_SOURCE,
            "notes": [
                "stations fields match database.md v0.2 public station catalog columns.",
                "charging_piles are simulated directly from fast_connector_count and slow_connector_count by project decision.",
                "Use --no-generate-piles if you only want station catalog rows without simulated pile rows.",
                "latitude/longitude are filled only when --tencent-key is provided and geocoding succeeds; otherwise they remain null as allowed by database.md v0.2.",
                "price_cents_per_kwh remains null by default because the source file has no price field; pass --price-cents-per-kwh to set a demo price.",
            ],
            "counts": {
                "source_rows": len(rows),
                "stations": len(stations),
                "charging_piles": len(charging_piles),
                "skipped_rows": len(skipped_rows),
                "fast_connector_count": sum(station["fast_connector_count"] for station in stations),
                "slow_connector_count": sum(station["slow_connector_count"] for station in stations),
                "district_detected": sum(1 for station in stations if station["district"]),
                "district_null": sum(1 for station in stations if not station["district"]),
                "geocode": dict(geocode_counts),
            },
        },
        "stations": stations,
        "charging_piles": charging_piles,
        "skipped_rows": skipped_rows,
    }
    return result


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Convert public station XLSX to database-ready JSON for stations and charging_piles.")
    parser.add_argument("--input", required=True, help="Source .xlsx file")
    parser.add_argument("--output", required=True, help="Output .json file")
    parser.add_argument("--sheet", default=None, help="Sheet name. Defaults to first sheet")
    parser.add_argument("--limit", type=int, default=None, help="Convert first N rows only")
    parser.add_argument("--tencent-key", default=None, help="Tencent Map WebService key. If omitted, coordinates remain null")
    parser.add_argument("--cache", default="data/geocode_cache_tencent.json", help="Geocoding cache path")
    parser.add_argument("--city-prefix", default="北京市", help="Prefix added to addresses for geocoding")
    parser.add_argument("--delay", type=float, default=0.12, help="Delay between Tencent API calls")
    parser.add_argument("--timeout", type=int, default=10, help="HTTP timeout seconds")
    parser.add_argument("--retries", type=int, default=2, help="Retries per address")
    parser.add_argument("--retry-delay", type=float, default=1.0, help="Delay before retry")
    parser.add_argument("--save-every", type=int, default=50, help="Save geocode cache every N stations")
    parser.add_argument("--price-cents-per-kwh", type=int, default=None, help="Optional demo price in cents/kWh; default keeps DB field null")
    parser.add_argument("--no-generate-piles", action="store_true", help="Only output stations; do not simulate charging_piles from connector counts")
    parser.add_argument("--fast-power-w", type=int, default=60000, help="Synthetic fast pile power")
    parser.add_argument("--slow-power-w", type=int, default=7000, help="Synthetic slow pile power")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    output = convert(args)
    output_path = Path(args.output)
    write_json(output_path, output)
    counts = output["metadata"]["counts"]
    print(f"Converted stations={counts['stations']}, charging_piles={counts['charging_piles']}, skipped={counts['skipped_rows']}")
    print(f"Connector totals: fast={counts['fast_connector_count']}, slow={counts['slow_connector_count']}")
    print(f"District: detected={counts['district_detected']}, null={counts['district_null']}")
    print(f"Geocode: {counts['geocode']}")
    print(f"Output: {output_path}")


if __name__ == "__main__":
    main()



