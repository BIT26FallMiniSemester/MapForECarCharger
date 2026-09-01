"""
Convert public charging-station Excel data into project-aligned JSON seed data.

The output follows the agreed backend/database contract:
- snake_case fields
- money in cents
- power in watts
- timestamps as ISO 8601 UTC strings
- station status: ACTIVE / INACTIVE
- pile status: IDLE / RESERVED / CHARGING / FAULT / OFFLINE
- pile type: FAST / SLOW

Input dataset columns expected:
- 序号
- 运营商
- 充电站点标准名称
- 服务类型
- 区县具体地址
- 快充接口数
- 慢充接口数
- 地域范围
- 所在地类型

The source file does not include latitude/longitude or station electricity prices.
For demo seed data, this script generates stable approximate coordinates around
Beijing and applies a configurable default price. Replace generated coordinates
with geocoded values later if Tencent Map geocoding is available.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import re
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Dict, Iterable, List, Optional

try:
    import pandas as pd
except ImportError as exc:  # pragma: no cover
    raise SystemExit(
        "Missing dependency: pandas. Install with: pip install pandas openpyxl"
    ) from exc

COLUMN_MAP = {
    "index": "序号",
    "operator": "运营商",
    "name": "充电站点标准名称",
    "service_type": "服务类型",
    "address": "区县具体地址",
    "fast_count": "快充接口数",
    "slow_count": "慢充接口数",
    "region_range": "地域范围",
    "location_type": "所在地类型",
}

PILE_STATUSES = ["IDLE", "IDLE", "IDLE", "CHARGING", "RESERVED", "FAULT", "OFFLINE"]


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


def generated_coordinate(seed: str, center: float, spread: float, salt: str) -> float:
    # Spread around the center in a deterministic way so repeated runs are stable.
    offset = (stable_fraction(seed, salt) - 0.5) * spread
    return round(center + offset, 7)


def build_pile_no(station_id: int, pile_type: str, sequence: int) -> str:
    type_code = "F" if pile_type == "FAST" else "S"
    return f"BJ-{station_id:04d}-{type_code}-{sequence:03d}"


def read_station_rows(input_path: Path, sheet_name: Optional[str]) -> Iterable[Dict[str, Any]]:
    df = pd.read_excel(input_path, sheet_name=sheet_name or 0)
    missing = [column for column in COLUMN_MAP.values() if column not in df.columns]
    if missing:
        raise ValueError(f"Excel missing required columns: {', '.join(missing)}")
    return df.to_dict(orient="records")


def convert(
    input_path: Path,
    output_path: Path,
    sheet_name: Optional[str],
    limit: Optional[int],
    default_price_cents_per_kwh: int,
    center_latitude: float,
    center_longitude: float,
    coordinate_spread: float,
) -> Dict[str, Any]:
    now = datetime.now(timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")
    rows = list(read_station_rows(input_path, sheet_name))
    if limit is not None:
        rows = rows[:limit]

    stations: List[Dict[str, Any]] = []
    charging_piles: List[Dict[str, Any]] = []
    skipped_rows: List[Dict[str, Any]] = []
    pile_id = 1

    for raw in rows:
        source_index = to_int(raw.get(COLUMN_MAP["index"]), len(stations) + 1)
        station_name = clean_text(raw.get(COLUMN_MAP["name"]))
        address = clean_text(raw.get(COLUMN_MAP["address"]))
        fast_count = to_int(raw.get(COLUMN_MAP["fast_count"]))
        slow_count = to_int(raw.get(COLUMN_MAP["slow_count"]))

        if not station_name or not address or fast_count + slow_count <= 0:
            skipped_rows.append({
                "source_index": source_index,
                "reason": "missing name/address or no charging interfaces",
            })
            continue

        station_id = len(stations) + 1
        seed = f"{station_name}|{address}|{source_index}"
        station = {
            "id": station_id,
            "source_index": source_index,
            "name": station_name,
            "address": address,
            "latitude": generated_coordinate(seed, center_latitude, coordinate_spread, "lat"),
            "longitude": generated_coordinate(seed, center_longitude, coordinate_spread, "lng"),
            "price_cents_per_kwh": default_price_cents_per_kwh,
            "status": "ACTIVE",
            "operator_name": clean_text(raw.get(COLUMN_MAP["operator"]), "未知运营商"),
            "service_type": clean_text(raw.get(COLUMN_MAP["service_type"]), "社会公用"),
            "region_range": clean_text(raw.get(COLUMN_MAP["region_range"]), "未知"),
            "location_type": clean_text(raw.get(COLUMN_MAP["location_type"]), "未知"),
            "created_at": now,
            "updated_at": now,
        }
        stations.append(station)

        for sequence in range(1, fast_count + 1):
            status = PILE_STATUSES[(pile_id - 1) % len(PILE_STATUSES)]
            charging_piles.append({
                "id": pile_id,
                "pile_no": build_pile_no(station_id, "FAST", sequence),
                "station_id": station_id,
                "pile_type": "FAST",
                "rated_power_w": 60000,
                "status": status,
                "last_heartbeat_at": now if status != "OFFLINE" else None,
                "created_at": now,
                "updated_at": now,
            })
            pile_id += 1

        for sequence in range(1, slow_count + 1):
            status = PILE_STATUSES[(pile_id - 1) % len(PILE_STATUSES)]
            charging_piles.append({
                "id": pile_id,
                "pile_no": build_pile_no(station_id, "SLOW", sequence),
                "station_id": station_id,
                "pile_type": "SLOW",
                "rated_power_w": 7000,
                "status": status,
                "last_heartbeat_at": now if status != "OFFLINE" else None,
                "created_at": now,
                "updated_at": now,
            })
            pile_id += 1

    output = {
        "metadata": {
            "source_file": str(input_path),
            "generated_at": now,
            "schema_version": "v0.1",
            "base_api_path": "/api/v1",
            "notes": [
                "Coordinates are deterministic demo coordinates generated from station name/address.",
                "price_cents_per_kwh is a configurable default because the public dataset has no price field.",
                "operator_name, service_type, region_range and location_type are source extension fields for display/filtering.",
            ],
            "counts": {
                "source_rows": len(rows),
                "stations": len(stations),
                "charging_piles": len(charging_piles),
                "skipped_rows": len(skipped_rows),
            },
        },
        "stations": stations,
        "charging_piles": charging_piles,
        "skipped_rows": skipped_rows,
    }

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(json.dumps(output, ensure_ascii=False, indent=2), encoding="utf-8")
    return output


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Convert public charging station XLSX to project JSON seed data.")
    parser.add_argument("--input", required=True, help="Path to source .xlsx file")
    parser.add_argument("--output", required=True, help="Path to output .json file")
    parser.add_argument("--sheet", default=None, help="Sheet name. Defaults to the first sheet")
    parser.add_argument("--limit", type=int, default=None, help="Optional max source rows to convert")
    parser.add_argument("--price-cents-per-kwh", type=int, default=125, help="Default station price, cents/kWh")
    parser.add_argument("--center-latitude", type=float, default=39.9042, help="Demo coordinate center latitude")
    parser.add_argument("--center-longitude", type=float, default=116.4074, help="Demo coordinate center longitude")
    parser.add_argument("--coordinate-spread", type=float, default=0.7, help="Coordinate spread in degrees")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    result = convert(
        input_path=Path(args.input),
        output_path=Path(args.output),
        sheet_name=args.sheet,
        limit=args.limit,
        default_price_cents_per_kwh=args.price_cents_per_kwh,
        center_latitude=args.center_latitude,
        center_longitude=args.center_longitude,
        coordinate_spread=args.coordinate_spread,
    )
    counts = result["metadata"]["counts"]
    print(
        "Converted {stations} stations and {charging_piles} piles. Skipped {skipped_rows} rows.".format(**counts)
    )
    print(f"Output: {args.output}")


if __name__ == "__main__":
    main()
