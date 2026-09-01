import json

from sqlalchemy.exc import IntegrityError
from sqlalchemy.orm import Session

from app.enums import LogSource, PileStatus
from app.errors import (
    DUPLICATE_RESOURCE,
    INVALID_REQUEST,
    PILE_NOT_AVAILABLE,
    error,
)
from app.helpers import utcnow
from app.models import Admin, ChargingPile, OperationLog, PileStatusLog, Station
from app.schemas import (
    PileStatusUpdate,
    StationCreate,
    StationPileCreate,
    StationUpdate,
)


def operation_log(
    db: Session,
    admin: Admin,
    action: str,
    target_type: str,
    target_id: int,
    request_data: dict | None = None,
    result: str = "SUCCESS",
) -> None:
    db.add(
        OperationLog(
            admin_id=admin.id,
            action=action,
            target_type=target_type,
            target_id=target_id,
            request_json=json.dumps(request_data, ensure_ascii=False)
            if request_data
            else None,
            result=result,
            created_at=utcnow(),
        )
    )


def create_station(db: Session, admin: Admin, payload: StationCreate) -> Station:
    now = utcnow()
    station = Station(
        name=payload.name,
        address=payload.address,
        latitude=payload.latitude,
        longitude=payload.longitude,
        price_cents_per_kwh=payload.price_cents_per_kwh,
        operator_name=payload.operator_name,
        service_type=payload.service_type,
        district=payload.district,
        region_scope=payload.region_scope,
        location_type=payload.location_type,
        fast_connector_count=payload.fast_connector_count,
        slow_connector_count=payload.slow_connector_count,
        created_at=now,
        updated_at=now,
    )
    db.add(station)
    db.flush()
    for item in payload.piles:
        db.add(
            ChargingPile(
                pile_no=item.pile_no,
                station_id=station.id,
                pile_type=item.pile_type,
                rated_power_w=item.rated_power_w,
                status=PileStatus.IDLE,
                version=1,
                created_at=now,
                updated_at=now,
            )
        )
    operation_log(
        db,
        admin,
        "CREATE_STATION",
        "STATION",
        station.id,
        payload.model_dump(mode="json"),
    )
    try:
        db.commit()
    except IntegrityError:
        db.rollback()
        raise error(DUPLICATE_RESOURCE, {"field": "pile_no"})
    db.refresh(station)
    return station


def update_station(
    db: Session, admin: Admin, station: Station, payload: StationUpdate
) -> Station:
    values = payload.model_dump(exclude_unset=True)
    if not values:
        raise error(INVALID_REQUEST, {"reason": "at least one field is required"})
    for key, value in values.items():
        setattr(station, key, value)
    station.updated_at = utcnow()
    operation_log(
        db,
        admin,
        "UPDATE_STATION",
        "STATION",
        station.id,
        payload.model_dump(exclude_unset=True, mode="json"),
    )
    db.commit()
    db.refresh(station)
    return station


def add_pile(
    db: Session, admin: Admin, station: Station, payload: StationPileCreate
) -> ChargingPile:
    now = utcnow()
    pile = ChargingPile(
        pile_no=payload.pile_no,
        station_id=station.id,
        pile_type=payload.pile_type,
        rated_power_w=payload.rated_power_w,
        status=PileStatus.IDLE,
        version=1,
        created_at=now,
        updated_at=now,
    )
    db.add(pile)
    db.flush()
    operation_log(
        db, admin, "CREATE_PILE", "PILE", pile.id, payload.model_dump(mode="json")
    )
    try:
        db.commit()
    except IntegrityError:
        db.rollback()
        raise error(DUPLICATE_RESOURCE, {"field": "pile_no"})
    db.refresh(pile)
    return pile


def restart_pile(db: Session, admin: Admin, pile: ChargingPile) -> ChargingPile:
    if pile.status == PileStatus.IDLE:
        operation_log(db, admin, "RESTART_PILE", "PILE", pile.id, result="SUCCESS")
        db.commit()
        return pile
    if pile.status != PileStatus.FAULT:
        raise error(
            PILE_NOT_AVAILABLE,
            {"pile_id": pile.id, "current_status": pile.status.value},
        )
    now = utcnow()
    pile.status = PileStatus.IDLE
    pile.version += 1
    pile.updated_at = now
    db.add(
        PileStatusLog(
            pile_id=pile.id,
            order_id=None,
            old_status=PileStatus.FAULT,
            new_status=PileStatus.IDLE,
            source=LogSource.ADMIN,
            reason="remote restart",
            created_at=now,
        )
    )
    operation_log(db, admin, "RESTART_PILE", "PILE", pile.id)
    db.commit()
    db.refresh(pile)
    return pile


def set_pile_status(
    db: Session, admin: Admin, pile: ChargingPile, payload: PileStatusUpdate
) -> ChargingPile:
    if pile.status in (PileStatus.RESERVED, PileStatus.CHARGING):
        raise error(
            PILE_NOT_AVAILABLE,
            {"pile_id": pile.id, "current_status": pile.status.value},
        )
    if payload.status not in (PileStatus.IDLE, PileStatus.FAULT, PileStatus.OFFLINE):
        raise error(INVALID_REQUEST, {"status": payload.status.value})
    if pile.status == payload.status:
        return pile
    old = pile.status
    now = utcnow()
    pile.status = payload.status
    pile.version += 1
    pile.updated_at = now
    db.add(
        PileStatusLog(
            pile_id=pile.id,
            order_id=None,
            old_status=old,
            new_status=payload.status,
            source=LogSource.ADMIN,
            reason=payload.reason,
            created_at=now,
        )
    )
    operation_log(
        db, admin, "SET_PILE_STATUS", "PILE", pile.id, payload.model_dump(mode="json")
    )
    db.commit()
    db.refresh(pile)
    return pile
