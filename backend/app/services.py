from datetime import datetime, timedelta
from decimal import ROUND_HALF_UP, Decimal
from uuid import uuid4

from sqlalchemy import or_, select, update
from sqlalchemy.exc import IntegrityError
from sqlalchemy.orm import Session, selectinload

from app.config import get_settings
from app.enums import (
    ACTIVE_ORDER_STATUSES,
    LogSource,
    OrderStatus,
    PileStatus,
    StationStatus,
    UserStatus,
)
from app.errors import (
    INSUFFICIENT_BALANCE,
    INVALID_ORDER_STATE,
    INVALID_REQUEST,
    ORDER_NOT_OWNED,
    PILE_NOT_AVAILABLE,
    RESERVATION_EXPIRED,
    RESOURCE_NOT_FOUND,
    TELEMETRY_OUT_OF_ORDER,
    USER_FROZEN,
    USER_HAS_ACTIVE_ORDER,
    error,
)
from app.helpers import as_utc, to_utc_naive, utcnow
from app.models import (
    ChargingOrder,
    ChargingPile,
    PileStatusLog,
    RechargeRecord,
    Station,
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


def create_order(
    db: Session, user: User, station_id: int, pile_id: int
) -> ChargingOrder:
    expire_reservations(db, user_id=user.id)
    station = db.get(Station, station_id)
    if station is None:
        raise error(RESOURCE_NOT_FOUND, {"resource": "station", "id": station_id})
    pile = db.get(ChargingPile, pile_id)
    if pile is None:
        raise error(RESOURCE_NOT_FOUND, {"resource": "pile", "id": pile_id})
    if pile.station_id != station.id:
        raise error(
            INVALID_REQUEST,
            {"reason": "charging pile does not belong to station"},
        )
    if station.status != StationStatus.ACTIVE:
        raise error(
            PILE_NOT_AVAILABLE,
            {"pile_id": pile_id, "station_status": station.status.value},
        )
    if station.price_cents_per_kwh is None:
        raise error(
            PILE_NOT_AVAILABLE,
            {"pile_id": pile_id, "reason": "station price is not configured"},
        )
    claimed_user = db.execute(
        update(User)
        .where(User.id == user.id, User.status == UserStatus.NORMAL)
        .values(balance_cents=User.balance_cents)
        .execution_options(synchronize_session=False)
    )
    if claimed_user.rowcount != 1:
        db.rollback()
        db.refresh(user)
        raise error(USER_FROZEN)
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
        station_id=station.id,
        pile_id=pile.id,
        status=OrderStatus.PENDING,
        unit_price_cents_per_kwh=station.price_cents_per_kwh,
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


def expire_reservations(
    db: Session,
    *,
    now: datetime | None = None,
    user_id: int | None = None,
    order_id: int | None = None,
) -> list[int]:
    cutoff = now or utcnow()
    filters = [
        ChargingOrder.status == OrderStatus.RESERVED,
        ChargingOrder.reservation_expires_at.is_not(None),
        ChargingOrder.reservation_expires_at <= cutoff,
    ]
    if user_id is not None:
        filters.append(ChargingOrder.user_id == user_id)
    if order_id is not None:
        filters.append(ChargingOrder.id == order_id)
    orders = db.scalars(
        select(ChargingOrder)
        .options(selectinload(ChargingOrder.pile))
        .where(*filters)
        .order_by(ChargingOrder.id)
    ).all()
    expired_order_ids: list[int] = []
    for order in orders:
        claimed = db.execute(
            update(ChargingOrder)
            .where(
                ChargingOrder.id == order.id,
                ChargingOrder.status == OrderStatus.RESERVED,
                ChargingOrder.version == order.version,
            )
            .values(
                status=OrderStatus.CANCELLED,
                cancel_reason="reservation expired",
                cancelled_at=cutoff,
                updated_at=cutoff,
                version=order.version + 1,
            )
            .execution_options(synchronize_session=False)
        )
        if claimed.rowcount != 1:
            continue
        pile = order.pile
        if pile.status == PileStatus.RESERVED:
            released = db.execute(
                update(ChargingPile)
                .where(
                    ChargingPile.id == pile.id,
                    ChargingPile.status == PileStatus.RESERVED,
                    ChargingPile.version == pile.version,
                )
                .values(
                    status=PileStatus.IDLE,
                    version=pile.version + 1,
                    updated_at=cutoff,
                )
                .execution_options(synchronize_session=False)
            )
            if released.rowcount == 1:
                _pile_log(
                    db,
                    pile,
                    PileStatus.RESERVED,
                    PileStatus.IDLE,
                    LogSource.SYSTEM,
                    order.id,
                    "reservation expired",
                )
        expired_order_ids.append(order.id)
    if expired_order_ids:
        db.commit()
    return expired_order_ids


def reserve_order(db: Session, order: ChargingOrder) -> ChargingOrder:
    if order.status != OrderStatus.PENDING:
        raise error(INVALID_ORDER_STATE, {"current_status": order.status.value})
    if order.station.status != StationStatus.ACTIVE:
        raise error(PILE_NOT_AVAILABLE, {"station_status": order.station.status.value})
    now = utcnow()
    order_version = order.version
    claimed = db.execute(
        update(ChargingOrder)
        .where(
            ChargingOrder.id == order.id,
            ChargingOrder.status == OrderStatus.PENDING,
            ChargingOrder.version == order_version,
        )
        .values(
            status=OrderStatus.RESERVED,
            reserved_at=now,
            reservation_expires_at=now
            + timedelta(seconds=get_settings().reservation_timeout_seconds),
            version=order_version + 1,
            updated_at=now,
        )
        .execution_options(synchronize_session=False)
    )
    if claimed.rowcount != 1:
        db.rollback()
        db.refresh(order)
        raise error(INVALID_ORDER_STATE, {"current_status": order.status.value})
    pile = order.pile
    pile_version = pile.version
    transitioned = db.execute(
        update(ChargingPile)
        .where(
            ChargingPile.id == pile.id,
            ChargingPile.status == PileStatus.IDLE,
            ChargingPile.version == pile_version,
        )
        .values(
            status=PileStatus.RESERVED,
            version=pile_version + 1,
            updated_at=now,
        )
        .execution_options(synchronize_session=False)
    )
    if transitioned.rowcount != 1:
        db.rollback()
        db.refresh(pile)
        raise error(
            PILE_NOT_AVAILABLE,
            {"pile_id": pile.id, "current_status": pile.status.value},
        )
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
    db.refresh(pile)
    return order


def start_order(db: Session, order: ChargingOrder) -> ChargingOrder:
    now = utcnow()
    expires_at = order.reservation_expires_at
    if expires_at is not None and expires_at <= now:
        expired = expire_reservations(db, order_id=order.id)
        if order.id in expired or (
            order.status == OrderStatus.CANCELLED
            and order.cancel_reason == "reservation expired"
        ):
            raise error(
                RESERVATION_EXPIRED,
                {
                    "order_id": order.id,
                    "reservation_expires_at": as_utc(expires_at),
                },
            )
    pile = order.pile
    order_version = order.version
    claimed = db.execute(
        update(ChargingOrder)
        .where(
            ChargingOrder.id == order.id,
            ChargingOrder.status == OrderStatus.RESERVED,
            ChargingOrder.version == order_version,
            ChargingOrder.reservation_expires_at.is_(None)
            | (ChargingOrder.reservation_expires_at > now),
        )
        .values(
            status=OrderStatus.CHARGING,
            started_at=now,
            updated_at=now,
            version=order_version + 1,
        )
        .execution_options(synchronize_session=False)
    )
    if claimed.rowcount != 1:
        db.rollback()
        db.refresh(order)
        if (
            order.status == OrderStatus.CANCELLED
            and order.cancel_reason == "reservation expired"
        ):
            raise error(
                RESERVATION_EXPIRED,
                {
                    "order_id": order.id,
                    "reservation_expires_at": as_utc(expires_at),
                },
            )
        raise error(
            INVALID_ORDER_STATE,
            {
                "order_status": order.status.value,
                "pile_status": pile.status.value,
            },
        )
    pile_version = pile.version
    transitioned = db.execute(
        update(ChargingPile)
        .where(
            ChargingPile.id == pile.id,
            ChargingPile.status == PileStatus.RESERVED,
            ChargingPile.version == pile_version,
        )
        .values(
            status=PileStatus.CHARGING,
            updated_at=now,
            version=pile_version + 1,
        )
        .execution_options(synchronize_session=False)
    )
    if transitioned.rowcount != 1:
        db.rollback()
        db.refresh(order)
        db.refresh(pile)
        raise error(
            INVALID_ORDER_STATE,
            {
                "order_status": order.status.value,
                "pile_status": pile.status.value,
            },
        )
    _pile_log(
        db,
        pile,
        PileStatus.RESERVED,
        PileStatus.CHARGING,
        LogSource.USER,
        order.id,
        "charging started",
    )
    db.commit()
    db.refresh(order)
    return order


def stop_order(db: Session, order: ChargingOrder) -> ChargingOrder:
    if order.status in (OrderStatus.UNPAID, OrderStatus.COMPLETED):
        return order
    if order.status != OrderStatus.CHARGING or order.pile.status != PileStatus.CHARGING:
        raise error(
            INVALID_ORDER_STATE,
            {
                "order_status": order.status.value,
                "pile_status": order.pile.status.value,
            },
        )
    if order.started_at is None:
        raise error(INVALID_ORDER_STATE, {"reason": "started_at is missing"})
    now = max(utcnow(), order.started_at)
    duration = max(0, round((now - order.started_at).total_seconds()))
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
    order_version = order.version
    claimed = db.execute(
        update(ChargingOrder)
        .where(
            ChargingOrder.id == order.id,
            ChargingOrder.status == OrderStatus.CHARGING,
            ChargingOrder.version == order_version,
        )
        .values(
            status=OrderStatus.UNPAID,
            duration_seconds=duration,
            energy_wh=energy,
            amount_cents=amount,
            stopped_at=now,
            updated_at=now,
            version=order_version + 1,
        )
        .execution_options(synchronize_session=False)
    )
    if claimed.rowcount != 1:
        db.rollback()
        db.refresh(order)
        if order.status in (OrderStatus.UNPAID, OrderStatus.COMPLETED):
            return order
        raise error(INVALID_ORDER_STATE, {"current_status": order.status.value})
    pile = order.pile
    pile_version = pile.version
    transitioned = db.execute(
        update(ChargingPile)
        .where(
            ChargingPile.id == pile.id,
            ChargingPile.status == PileStatus.CHARGING,
            ChargingPile.version == pile_version,
        )
        .values(
            status=PileStatus.IDLE,
            updated_at=now,
            version=pile_version + 1,
        )
        .execution_options(synchronize_session=False)
    )
    if transitioned.rowcount != 1:
        db.rollback()
        db.refresh(order)
        db.refresh(pile)
        raise error(
            INVALID_ORDER_STATE,
            {"order_status": order.status.value, "pile_status": pile.status.value},
        )
    _pile_log(
        db,
        pile,
        PileStatus.CHARGING,
        PileStatus.IDLE,
        LogSource.USER,
        order.id,
        "charging stopped",
    )
    db.commit()
    db.refresh(order)
    db.refresh(pile)
    return order


def settle_order(db: Session, order: ChargingOrder, user: User) -> ChargingOrder:
    if order.status == OrderStatus.COMPLETED:
        return order
    if order.status != OrderStatus.UNPAID:
        raise error(INVALID_ORDER_STATE, {"current_status": order.status.value})
    now = utcnow()
    claimed = db.execute(
        update(ChargingOrder)
        .where(
            ChargingOrder.id == order.id,
            ChargingOrder.status == OrderStatus.UNPAID,
        )
        .values(
            status=OrderStatus.COMPLETED,
            settled_at=now,
            updated_at=now,
            version=ChargingOrder.version + 1,
        )
        .execution_options(synchronize_session=False)
    )
    if claimed.rowcount != 1:
        db.rollback()
        db.refresh(order)
        db.refresh(user)
        if order.status == OrderStatus.COMPLETED:
            return order
        raise error(INVALID_ORDER_STATE, {"current_status": order.status.value})
    deducted = db.execute(
        update(User)
        .where(User.id == user.id, User.balance_cents >= order.amount_cents)
        .values(balance_cents=User.balance_cents - order.amount_cents, updated_at=now)
        .execution_options(synchronize_session=False)
    )
    if deducted.rowcount != 1:
        db.rollback()
        db.refresh(order)
        db.refresh(user)
        raise error(
            INSUFFICIENT_BALANCE,
            {"amount_cents": order.amount_cents, "balance_cents": user.balance_cents},
        )
    db.commit()
    db.refresh(order)
    db.refresh(user)
    return order


def cancel_order(
    db: Session, order: ChargingOrder, reason: str | None
) -> ChargingOrder:
    if order.status not in (OrderStatus.PENDING, OrderStatus.RESERVED):
        raise error(INVALID_ORDER_STATE, {"current_status": order.status.value})
    original_status = order.status
    order_version = order.version
    now = utcnow()
    claimed = db.execute(
        update(ChargingOrder)
        .where(
            ChargingOrder.id == order.id,
            ChargingOrder.status == original_status,
            ChargingOrder.version == order_version,
        )
        .values(
            status=OrderStatus.CANCELLED,
            cancel_reason=reason,
            cancelled_at=now,
            updated_at=now,
            version=order_version + 1,
        )
        .execution_options(synchronize_session=False)
    )
    if claimed.rowcount != 1:
        db.rollback()
        db.refresh(order)
        raise error(INVALID_ORDER_STATE, {"current_status": order.status.value})
    if original_status == OrderStatus.RESERVED:
        pile = order.pile
        pile_version = pile.version
        released = db.execute(
            update(ChargingPile)
            .where(
                ChargingPile.id == pile.id,
                ChargingPile.status == PileStatus.RESERVED,
                ChargingPile.version == pile_version,
            )
            .values(
                status=PileStatus.IDLE,
                version=pile_version + 1,
                updated_at=now,
            )
            .execution_options(synchronize_session=False)
        )
        if released.rowcount != 1:
            db.rollback()
            db.refresh(order)
            db.refresh(pile)
            raise error(
                INVALID_ORDER_STATE,
                {"order_status": order.status.value, "pile_status": pile.status.value},
            )
        _pile_log(
            db,
            pile,
            PileStatus.RESERVED,
            PileStatus.IDLE,
            LogSource.USER,
            order.id,
            "reservation cancelled",
        )
    db.commit()
    db.refresh(order)
    if original_status == OrderStatus.RESERVED:
        db.refresh(pile)
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
            raise error(INVALID_REQUEST, {"field": "client_request_id"})
        return existing
    now = utcnow()
    db.execute(
        update(User)
        .where(User.id == user.id)
        .values(
            balance_cents=User.balance_cents + amount_cents,
            updated_at=now,
        )
        .execution_options(synchronize_session=False)
    )
    db.refresh(user)
    before = user.balance_cents - amount_cents
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
        raise error(INVALID_REQUEST, {"field": "client_request_id"})
    db.refresh(record)
    return record


def update_telemetry(
    db: Session, order: ChargingOrder, reported_at: datetime, energy_wh: int
) -> ChargingOrder:
    if order.status != OrderStatus.CHARGING:
        raise error(INVALID_ORDER_STATE, {"current_status": order.status.value})
    reported = to_utc_naive(reported_at)
    if energy_wh < order.energy_wh or reported < order.updated_at:
        raise error(TELEMETRY_OUT_OF_ORDER, {"current_energy_wh": order.energy_wh})
    changed = db.execute(
        update(ChargingOrder)
        .where(
            ChargingOrder.id == order.id,
            ChargingOrder.status == OrderStatus.CHARGING,
            ChargingOrder.energy_wh <= energy_wh,
            ChargingOrder.updated_at <= reported,
            or_(
                ChargingOrder.energy_wh < energy_wh,
                ChargingOrder.updated_at < reported,
            ),
        )
        .values(
            energy_wh=energy_wh,
            updated_at=reported,
            version=ChargingOrder.version + 1,
        )
        .execution_options(synchronize_session=False)
    )
    if changed.rowcount != 1:
        db.rollback()
        db.refresh(order)
        if order.status != OrderStatus.CHARGING:
            raise error(INVALID_ORDER_STATE, {"current_status": order.status.value})
        if energy_wh == order.energy_wh and reported == order.updated_at:
            return order
        raise error(TELEMETRY_OUT_OF_ORDER, {"current_energy_wh": order.energy_wh})
    db.commit()
    db.refresh(order)
    return order
