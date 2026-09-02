import argparse
import csv
from dataclasses import dataclass
from pathlib import Path

from sqlalchemy import select
from sqlalchemy.orm import Session

from app.config import BACKEND_DIR
from app.database import SessionLocal
from app.enums import StationStatus
from app.helpers import utcnow
from app.models import Station

DATA_SOURCE = "BEIJING_PUBLIC_DATA_OPEN_PLATFORM"
DEFAULT_CSV_PATH = BACKEND_DIR / "data" / "raw" / "beijing_public_charging_stations.csv"
REQUIRED_COLUMNS = {
    "序号",
    "运营商",
    "充电站点标准名称",
    "服务类型",
    "区县具体地址",
    "快充接口数",
    "慢充接口数",
    "地域范围",
    "所在地类型",
}
DISTRICT_ALIASES = (
    ("东城区", ("东城区", "东城")),
    ("西城区", ("西城区", "西城")),
    ("朝阳区", ("朝阳区", "朝阳")),
    ("海淀区", ("海淀区", "海淀")),
    ("丰台区", ("丰台区", "丰台")),
    ("石景山区", ("石景山区", "石景山")),
    ("门头沟区", ("门头沟区", "门头沟")),
    ("房山区", ("房山区", "房山")),
    ("通州区", ("通州区", "通州")),
    ("顺义区", ("顺义区", "顺义")),
    ("昌平区", ("昌平区", "昌平")),
    ("大兴区", ("大兴区", "大兴")),
    ("怀柔区", ("怀柔区", "怀柔")),
    ("平谷区", ("平谷区", "平谷")),
    ("密云区", ("密云区", "密云县", "密云")),
    ("延庆区", ("延庆区", "延庆县", "延庆")),
    ("北京经济技术开发区", ("北京经济技术开发区", "经济技术开发区", "亦庄")),
)


@dataclass
class ImportResult:
    total_rows: int = 0
    inserted: int = 0
    updated: int = 0
    unchanged: int = 0
    districts_unresolved: int = 0


def derive_district(name: str, address: str) -> str | None:
    text = f"{address} {name}"
    for district, aliases in DISTRICT_ALIASES:
        if any(alias in text for alias in aliases):
            return district
    return None


def _parse_nonnegative_int(value: str, field_name: str, row_number: int) -> int:
    try:
        parsed = int(value.strip())
    except ValueError as exc:
        raise ValueError(f"row {row_number}: {field_name} must be an integer") from exc
    if parsed < 0:
        raise ValueError(f"row {row_number}: {field_name} must not be negative")
    return parsed


def _source_values(row: dict[str, str], row_number: int) -> dict:
    name = row["充电站点标准名称"].strip()
    address = row["区县具体地址"].strip()
    external_id = row["序号"].strip()
    if not external_id or not name or not address:
        raise ValueError(f"row {row_number}: id, name and address are required")
    return {
        "external_id": external_id,
        "name": name,
        "address": address,
        "operator_name": row["运营商"].strip(),
        "service_type": row["服务类型"].strip(),
        "district": derive_district(name, address),
        "region_scope": row["地域范围"].strip(),
        "location_type": row["所在地类型"].strip(),
        "fast_connector_count": _parse_nonnegative_int(
            row["快充接口数"], "快充接口数", row_number
        ),
        "slow_connector_count": _parse_nonnegative_int(
            row["慢充接口数"], "慢充接口数", row_number
        ),
    }


def import_public_stations(
    db: Session,
    csv_path: Path = DEFAULT_CSV_PATH,
    *,
    commit: bool = True,
) -> ImportResult:
    result = ImportResult()
    existing = {
        station.external_id: station
        for station in db.scalars(
            select(Station).where(Station.data_source == DATA_SOURCE)
        ).all()
    }
    seen_ids: set[str] = set()
    now = utcnow()
    with csv_path.open(encoding="utf-8-sig", newline="") as source_file:
        reader = csv.DictReader(source_file)
        actual_columns = set(reader.fieldnames or ())
        if not REQUIRED_COLUMNS.issubset(actual_columns):
            missing = sorted(REQUIRED_COLUMNS - actual_columns)
            raise ValueError(f"CSV is missing required columns: {missing}")
        for row_number, row in enumerate(reader, start=2):
            result.total_rows += 1
            values = _source_values(row, row_number)
            external_id = values.pop("external_id")
            if external_id in seen_ids:
                raise ValueError(f"row {row_number}: duplicate source id {external_id}")
            seen_ids.add(external_id)
            if values["district"] is None:
                result.districts_unresolved += 1
            station = existing.get(external_id)
            if station is None:
                station = Station(
                    **values,
                    data_source=DATA_SOURCE,
                    external_id=external_id,
                    latitude=None,
                    longitude=None,
                    price_cents_per_kwh=None,
                    status=StationStatus.ACTIVE,
                    created_at=now,
                    updated_at=now,
                )
                db.add(station)
                result.inserted += 1
                continue
            changed = False
            for field_name, value in values.items():
                if getattr(station, field_name) != value:
                    setattr(station, field_name, value)
                    changed = True
            if changed:
                station.updated_at = now
                result.updated += 1
            else:
                result.unchanged += 1
    if commit:
        db.commit()
    else:
        db.flush()
    return result


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Import Beijing public charging-station data"
    )
    parser.add_argument("--csv", type=Path, default=DEFAULT_CSV_PATH)
    arguments = parser.parse_args()
    with SessionLocal() as db:
        result = import_public_stations(db, arguments.csv)
    print(
        "Imported public stations:",
        f"rows={result.total_rows}",
        f"inserted={result.inserted}",
        f"updated={result.updated}",
        f"unchanged={result.unchanged}",
        f"districts_unresolved={result.districts_unresolved}",
    )


if __name__ == "__main__":
    main()
