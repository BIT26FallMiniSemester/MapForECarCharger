from datetime import datetime
from decimal import ROUND_HALF_UP, Decimal
from uuid import uuid4

from sqlalchemy import select, update
from sqlalchemy.exc import IntegrityError
from sqlalchemy.orm import Session

from app.enums import (
    ACTIVE_ORDER_STATUSES,
    LogSource,
    OrderStatus,
    PileStatus,
    StationStatus,
)
from app.errors import (
    DUPLICATE_RESOURCE,
    INSUFFICIENT_BALANCE,
    INVALID_ORDER_STATE,
    ORDER_NOT_OWNED,
    PILE_NOT_AVAILABLE,
    RESOURCE_NOT_FOUND,
    TELEMETRY_OUT_OF_ORDER,
    USER_HAS_ACTIVE_ORDER,
    error,
)
from app.helpers import utcnow
from app.models import (
    ChargingOrder,
    ChargingPile,
    PileStatusLog,
    RechargeRecord,
    User,
)


def _number(prefix: str) -> str:
    return f"{prefix}{utcnow():%Y%m%d%H%M%S}{uuid4().hex[:8].upper()}"


def get_owned_order(db: Session, order_id: int, user_id: int) -> ChargingOrder:
    order = db.get(ChargingOrder, order_id)
    if order is None:
        raise error(RESOURCE_NOT_FOUND, {"resource": "order", "id": order_id})
    if order.user_id != user_id:
        raise error(ORDER_NOT_OWNED, {"order_id": order_id})
    return order


def create_order(db: Session, user: User, pile_id: int) -> ChargingOrder:
    pile = db.get(ChargingPile, pile_id)
    if pile is None:
        raise error(RESOURCE_NOT_FOUND, {"resource": "pile", "id": pile_id})
    if pile.station.status != StationStatus.ACTIVE:
        raise error(
            PILE_NOT_AVAILABLE,
            {"pile_id": pile_id, "station_status": pile.station.status.value},
        )
    if pile.station.price_cents_per_kwh is None:
        raise error(
            PILE_NOT_AVAILABLE,
            {"pile_id": pile_id, "reason": "station price is not configured"},
        )
    active = db.scalar(
        select(ChargingOrder.id).where(
            ChargingOrder.user_id == user.id,
            ChargingOrder.status.in_(ACTIVE_ORDER_STATUSES),
        )
    )
    if active is not None:
        raise error(USER_HAS_ACTIVE_ORDER, {"order_id": active})
    now = utcnow()
    order = ChargingOrder(
        order_no=_number("CO"),
        user_id=user.id,
        station_id=pile.station_id,
        pile_id=pile.id,
        status=OrderStatus.PENDING,
        unit_price_cents_per_kwh=pile.station.price_cents_per_kwh,
        energy_wh=0,
        duration_seconds=0,
        amount_cents=0,
        version=1,
        created_at=now,
        updated_at=now,
    )
    db.add(order)
    try:
        db.commit()
    except IntegrityError:
        db.rollback()
        raise error(USER_HAS_ACTIVE_ORDER)
    db.refresh(order)
    return order


def _pile_log(
    db: Session,
    pile: ChargingPile,
    old: PileStatus,
    new: PileStatus,
    source: LogSource,
    order_id: int | None,
    reason: str | None,
) -> None:
    db.add(
        PileStatusLog(
            pile_id=pile.id,
            order_id=order_id,
            old_status=old,
            new_status=new,
            source=source,
            reason=reason,
            created_at=utcnow(),
        )
    )


def reserve_order(db: Session, order: ChargingOrder) -> ChargingOrder:
    if order.status != OrderStatus.PENDING:
        raise error(INVALID_ORDER_STATE, {"current_status": order.status.value})
    if order.station.status != StationStatus.ACTIVE:
        raise error(PILE_NOT_AVAILABLE, {"station_status": order.station.status.value})
    pile = order.pile
    old_version = pile.version
    result = db.execute(
        update(ChargingPile)
        .where(
            ChargingPile.id == pile.id,
            ChargingPile.status == PileStatus.IDLE,
            ChargingPile.version == old_version,
        )
        .values(
            status=PileStatus.RESERVED, version=old_version + 1, updated_at=utcnow()
        )
    )
    if result.rowcount != 1:
        db.rollback()
        raise error(
            PILE_NOT_AVAILABLE,
            {"pile_id": pile.id, "current_status": pile.status.value},
        )
    now = utcnow()
    order.status = OrderStatus.RESERVED
    order.reserved_at = now
    order.version += 1
    order.updated_at = now
    _pile_log(
        db,
        pile,
        PileStatus.IDLE,
        PileStatus.RESERVED,
        LogSource.USER,
        order.id,
        "order reserved",
    )
    db.commit()
    db.refresh(order)
    return order


def start_order(db: Session, order: ChargingOrder) -> ChargingOrder:
    if order.status != OrderStatus.RESERVED or order.pile.status != PileStatus.RESERVED:
        raise error(
            INVALID_ORDER_STATE,
            {
                "order_status": order.status.value,
                "pile_status": order.pile.status.value,
            },
        )
    now = utcnow()
    order.status = OrderStatus.CHARGING
    order.started_at = now
    order.updated_at = now
    order.version += 1
    old = order.pile.status
    order.pile.status = PileStatus.CHARGING
    order.pile.updated_at = now
    order.pile.version += 1
    _pile_log(
        db,
        order.pile,
        old,
        PileStatus.CHARGING,
        LogSource.USER,
        order.id,
        "charging started",
    )
    db.commit()
    db.refresh(order)
    return order


def stop_order(db: Session, order: ChargingOrder) -> ChargingOrder:
    if order.status != OrderStatus.CHARGING or order.pile.status != PileStatus.CHARGING:
        raise error(
            INVALID_ORDER_STATE,
            {
                "order_status": order.status.value,
                "pile_status": order.pile.status.value,
            },
        )
    now = max(utcnow(), order.updated_at)
    duration = max(0, round((now - order.started_at).total_seconds()))
    energy = order.energy_wh
    if energy <= 0:
        energy = int(
            (
                Decimal(order.pile.rated_power_w) * Decimal(duration) / Decimal(3600)
            ).quantize(Decimal(1), rounding=ROUND_HALF_UP)
        )
    amount = int(
        (
            Decimal(energy) * Decimal(order.unit_price_cents_per_kwh) / Decimal(1000)
        ).quantize(Decimal(1), rounding=ROUND_HALF_UP)
    )
    order.status = OrderStatus.UNPAID
    order.duration_seconds = duration
    order.energy_wh = energy
    order.amount_cents = amount
    order.stopped_at = now
    order.updated_at = now
    order.version += 1
    order.pile.status = PileStatus.IDLE
    order.pile.updated_at = now
    order.pile.version += 1
    _pile_log(
        db,
        order.pile,
        PileStatus.CHARGING,
        PileStatus.IDLE,
        LogSource.USER,
        order.id,
        "charging stopped",
    )
    db.commit()
    db.refresh(order)
    return order


def settle_order(db: Session, order: ChargingOrder, user: User) -> ChargingOrder:
    if order.status == OrderStatus.COMPLETED:
        return order
    if order.status != OrderStatus.UNPAID:
        raise error(INVALID_ORDER_STATE, {"current_status": order.status.value})
    result = db.execute(
        update(User)
        .where(User.id == user.id, User.balance_cents >= order.amount_cents)
        .values(
            balance_cents=User.balance_cents - order.amount_cents, updated_at=utcnow()
        )
    )
    if result.rowcount != 1:
        db.rollback()
        raise error(
            INSUFFICIENT_BALANCE,
            {"amount_cents": order.amount_cents, "balance_cents": user.balance_cents},
        )
    now = utcnow()
    order.status = OrderStatus.COMPLETED
    order.settled_at = now
    order.updated_at = now
    order.version += 1
    db.commit()
    db.refresh(order)
    db.refresh(user)
    return order


def cancel_order(
    db: Session, order: ChargingOrder, reason: str | None
) -> ChargingOrder:
    if order.status not in (OrderStatus.PENDING, OrderStatus.RESERVED):
        raise error(INVALID_ORDER_STATE, {"current_status": order.status.value})
    now = utcnow()
    if order.status == OrderStatus.RESERVED:
        pile = order.pile
        pile.status = PileStatus.IDLE
        pile.version += 1
        pile.updated_at = now
        _pile_log(
            db,
            pile,
            PileStatus.RESERVED,
            PileStatus.IDLE,
            LogSource.USER,
            order.id,
            "reservation cancelled",
        )
    order.status = OrderStatus.CANCELLED
    order.cancel_reason = reason
    order.cancelled_at = now
    order.updated_at = now
    order.version += 1
    db.commit()
    db.refresh(order)
    return order


def recharge(
    db: Session, user: User, amount_cents: int, client_request_id: str
) -> RechargeRecord:
    existing = db.scalar(
        select(RechargeRecord).where(
            RechargeRecord.client_request_id == client_request_id
        )
    )
    if existing is not None:
        if existing.user_id != user.id or existing.amount_cents != amount_cents:
            raise error(DUPLICATE_RESOURCE, {"field": "client_request_id"})
        return existing
    before = user.balance_cents
    now = utcnow()
    user.balance_cents += amount_cents
    user.updated_at = now
    record = RechargeRecord(
        recharge_no=_number("RC"),
        client_request_id=client_request_id,
        user_id=user.id,
        amount_cents=amount_cents,
        balance_before_cents=before,
        balance_after_cents=user.balance_cents,
        channel="SIMULATED",
        status="SUCCESS",
        created_at=now,
    )
    db.add(record)
    try:
        db.commit()
    except IntegrityError:
        db.rollback()
        existing = db.scalar(
            select(RechargeRecord).where(
                RechargeRecord.client_request_id == client_request_id
            )
        )
        if (
            existing is not None
            and existing.user_id == user.id
            and existing.amount_cents == amount_cents
        ):
            return existing
        raise error(DUPLICATE_RESOURCE, {"field": "client_request_id"})
    db.refresh(record)
    return record


def update_telemetry(
    db: Session, order: ChargingOrder, reported_at: datetime, energy_wh: int
) -> ChargingOrder:
    if order.status != OrderStatus.CHARGING:
        raise error(INVALID_ORDER_STATE, {"current_status": order.status.value})
    reported = reported_at.replace(tzinfo=None)
    if energy_wh < order.energy_wh or reported < order.updated_at:
        raise error(TELEMETRY_OUT_OF_ORDER, {"current_energy_wh": order.energy_wh})
    if energy_wh == order.energy_wh and reported == order.updated_at:
        return order
    order.energy_wh = energy_wh
    order.updated_at = reported
    order.version += 1
    db.commit()
    db.refresh(order)
    return order
