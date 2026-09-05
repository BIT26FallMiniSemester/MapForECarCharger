import argparse
import json
from dataclasses import dataclass
from decimal import Decimal, InvalidOperation
from pathlib import Path

from sqlalchemy import select
from sqlalchemy.orm import Session

from app.config import BACKEND_DIR
from app.database import SessionLocal
from app.enums import StationStatus
from app.helpers import utcnow
from app.models import Station

DATA_SOURCE = "BEIJING_PUBLIC_DATA_OPEN_PLATFORM"
DEFAULT_JSON_PATH = (
    BACKEND_DIR / "data" / "processed" / "beijing_public_charging_stations.json"
)
OPTIONAL_TEXT_FIELDS = (
    "operator_name",
    "service_type",
    "district",
    "region_scope",
    "location_type",
)


@dataclass
class ImportResult:
    total_rows: int = 0
    inserted: int = 0
    updated: int = 0
    unchanged: int = 0
    districts_unresolved: int = 0


def _required_text(row: dict, field: str, row_number: int) -> str:
    value = row.get(field)
    if not isinstance(value, str) or not value.strip():
        raise ValueError(f"row {row_number}: {field} is required")
    return value.strip()


def _optional_text(row: dict, field: str, row_number: int) -> str | None:
    value = row.get(field)
    if value is None:
        return None
    if not isinstance(value, str):
        raise TypeError(f"row {row_number}: {field} must be a string or null")
    return value.strip() or None


def _nonnegative_int(row: dict, field: str, row_number: int) -> int:
    value = row.get(field)
    if isinstance(value, bool):
        raise TypeError(f"row {row_number}: {field} must be a nonnegative integer")
    try:
        parsed = int(value)
    except (TypeError, ValueError) as exc:
        raise ValueError(
            f"row {row_number}: {field} must be a nonnegative integer"
        ) from exc
    if parsed < 0 or parsed != value:
        raise ValueError(f"row {row_number}: {field} must be a nonnegative integer")
    return parsed


def _decimal_in_range(
    row: dict, field: str, minimum: Decimal, maximum: Decimal, row_number: int
) -> Decimal:
    try:
        value = Decimal(str(row.get(field)))
    except (InvalidOperation, ValueError) as exc:
        raise ValueError(f"row {row_number}: {field} must be numeric") from exc
    if not value.is_finite() or not minimum <= value <= maximum:
        raise ValueError(
            f"row {row_number}: {field} must be between {minimum} and {maximum}"
        )
    return value


def _station_values(row: dict, row_number: int) -> tuple[str, dict]:
    data_source = _required_text(row, "data_source", row_number)
    if data_source != DATA_SOURCE:
        raise ValueError(f"row {row_number}: unsupported data_source {data_source}")
    status = _required_text(row, "status", row_number)
    if status != StationStatus.ACTIVE.value:
        raise ValueError(f"row {row_number}: unsupported station status {status}")
    price = row.get("price_cents_per_kwh")
    if price is not None:
        price = _nonnegative_int(row, "price_cents_per_kwh", row_number)
        if price == 0:
            raise ValueError(
                f"row {row_number}: price_cents_per_kwh must be positive or null"
            )
    values = {
        "name": _required_text(row, "name", row_number),
        "address": _required_text(row, "address", row_number),
        "latitude": _decimal_in_range(
            row, "latitude", Decimal(-90), Decimal(90), row_number
        ),
        "longitude": _decimal_in_range(
            row, "longitude", Decimal(-180), Decimal(180), row_number
        ),
        "price_cents_per_kwh": price,
        "fast_connector_count": _nonnegative_int(
            row, "fast_connector_count", row_number
        ),
        "slow_connector_count": _nonnegative_int(
            row, "slow_connector_count", row_number
        ),
    }
    values.update(
        {
            field: _optional_text(row, field, row_number)
            for field in OPTIONAL_TEXT_FIELDS
        }
    )
    return _required_text(row, "external_id", row_number), values


def import_public_stations(
    db: Session,
    json_path: Path = DEFAULT_JSON_PATH,
    *,
    commit: bool = True,
) -> ImportResult:
    try:
        document = json.loads(json_path.read_text(encoding="utf-8-sig"))
    except (OSError, json.JSONDecodeError) as exc:
        raise ValueError(f"cannot read station dataset: {json_path}") from exc
    if not isinstance(document, dict):
        raise TypeError("station dataset must be a JSON object")
    rows = document.get("stations")
    if not isinstance(rows, list) or not rows:
        raise ValueError("station dataset must contain a non-empty stations array")
    metadata = document.get("metadata", {})
    if metadata.get("data_source") != DATA_SOURCE:
        raise ValueError("station dataset metadata has an unsupported data_source")

    result = ImportResult()
    existing = {
        station.external_id: station
        for station in db.scalars(
            select(Station).where(Station.data_source == DATA_SOURCE)
        ).all()
    }
    seen_ids: set[str] = set()
    now = utcnow()
    for row_number, row in enumerate(rows, start=1):
        if not isinstance(row, dict):
            raise TypeError(f"row {row_number}: station must be an object")
        result.total_rows += 1
        external_id, values = _station_values(row, row_number)
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
                status=StationStatus.ACTIVE,
                created_at=now,
                updated_at=now,
            )
            db.add(station)
            result.inserted += 1
            continue
        price = values.pop("price_cents_per_kwh")
        changed = False
        for field_name, value in values.items():
            if getattr(station, field_name) != value:
                setattr(station, field_name, value)
                changed = True
        if price is not None and station.price_cents_per_kwh != price:
            station.price_cents_per_kwh = price
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
        description="Import cleaned Beijing public charging-station data"
    )
    parser.add_argument("--json", type=Path, default=DEFAULT_JSON_PATH)
    arguments = parser.parse_args()
    with SessionLocal() as db:
        result = import_public_stations(db, arguments.json)
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
