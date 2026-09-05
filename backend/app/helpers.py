from datetime import UTC, datetime
from decimal import ROUND_HALF_UP, Decimal
from math import ceil

from app.enums import OrderStatus
from app.models import ChargingOrder, ChargingPile, RechargeRecord, Station, User


def utcnow() -> datetime:
    return datetime.now(UTC).replace(tzinfo=None)


def to_utc_naive(value: datetime) -> datetime:
    if value.tzinfo is None:
        return value
    return value.astimezone(UTC).replace(tzinfo=None)


def as_utc(value: datetime | None) -> str | None:
    if value is None:
        return None
    if value.tzinfo is None:
        value = value.replace(tzinfo=UTC)
    return value.astimezone(UTC).isoformat().replace("+00:00", "Z")


def user_data(user: User) -> dict:
    return {
        "id": user.id,
        "phone": user.phone,
        "nickname": user.nickname,
        "avatar_url": user.avatar_url,
        "balance_cents": user.balance_cents,
        "status": user.status.value,
        "created_at": as_utc(user.created_at),
        "updated_at": as_utc(user.updated_at),
    }


def station_data(
    station: Station,
    stats: dict | None = None,
    distance_km: float | None = None,
) -> dict:
    stats = stats or {}
    managed_total = stats.get("total_piles", 0)
    managed_available = stats.get("available_piles", 0)
    has_coordinates = station.latitude is not None and station.longitude is not None
    return {
        "id": station.id,
        "name": station.name,
        "address": station.address,
        "latitude": float(station.latitude) if station.latitude is not None else None,
        "longitude": float(station.longitude)
        if station.longitude is not None
        else None,
        "price_cents_per_kwh": station.price_cents_per_kwh,
        "status": station.status.value,
        "operator_name": station.operator_name,
        "service_type": station.service_type,
        "district": station.district,
        "region_scope": station.region_scope,
        "location_type": station.location_type,
        "fast_connector_count": station.fast_connector_count,
        "slow_connector_count": station.slow_connector_count,
        "total_connector_count": station.fast_connector_count
        + station.slow_connector_count,
        "data_source": station.data_source,
        "external_id": station.external_id,
        "has_coordinates": has_coordinates,
        "is_bookable": (
            station.status.value == "ACTIVE"
            and station.price_cents_per_kwh is not None
            and managed_available > 0
        ),
        "total_piles": managed_total,
        "available_piles": managed_available,
        "online_rate": stats.get("online_rate", 0.0),
        "distance_km": distance_km,
    }


def pile_data(pile: ChargingPile, totals: dict | None = None) -> dict:
    totals = totals or {}
    return {
        "id": pile.id,
        "pile_no": pile.pile_no,
        "station_id": pile.station_id,
        "station_name": pile.station.name,
        "charge_type": pile.pile_type.value,
        "rated_power_w": pile.rated_power_w,
        "status": pile.status.value,
        "last_heartbeat_at": as_utc(pile.last_heartbeat_at),
        "total_charge_count": totals.get("total_charge_count", 0),
        "total_charge_duration_seconds": totals.get("total_charge_duration_seconds", 0),
    }


def order_data(order: ChargingOrder) -> dict:
    estimated = order.status == OrderStatus.CHARGING and order.started_at is not None
    duration_seconds = order.duration_seconds
    energy_wh = order.energy_wh
    amount_cents = order.amount_cents
    if estimated:
        duration_seconds = max(0, round((utcnow() - order.started_at).total_seconds()))
        energy_wh = int(
            (
                Decimal(order.pile.rated_power_w)
                * Decimal(duration_seconds)
                / Decimal(3600)
            ).quantize(Decimal(1), rounding=ROUND_HALF_UP)
        )
        amount_cents = int(
            (
                Decimal(energy_wh)
                * Decimal(order.unit_price_cents_per_kwh)
                / Decimal(1000)
            ).quantize(Decimal(1), rounding=ROUND_HALF_UP)
        )
    return {
        "id": order.id,
        "order_no": order.order_no,
        "status": order.status.value,
        "station": {"id": order.station_id, "name": order.station.name},
        "pile": {
            "id": order.pile_id,
            "pile_no": order.pile.pile_no,
            "rated_power_w": order.pile.rated_power_w,
        },
        "price_cents_per_kwh": order.unit_price_cents_per_kwh,
        "duration_seconds": duration_seconds,
        "energy_wh": energy_wh,
        "amount_cents": amount_cents,
        "estimated": estimated,
        "reserved_at": as_utc(order.reserved_at),
        "expires_at": as_utc(order.reservation_expires_at),
        "started_at": as_utc(order.started_at),
        "stopped_at": as_utc(order.stopped_at),
        "paid_at": as_utc(order.settled_at),
        "cancelled_at": as_utc(order.cancelled_at),
        "cancel_reason": order.cancel_reason,
        "created_at": as_utc(order.created_at),
        "updated_at": as_utc(order.updated_at),
    }


def recharge_data(record: RechargeRecord) -> dict:
    return {
        "record_id": record.id,
        "amount_cents": record.amount_cents,
        "balance_cents": record.balance_after_cents,
        "created_at": as_utc(record.created_at),
    }


def page_data(items: list, page: int, page_size: int, total: int) -> dict:
    return {
        "items": items,
        "page": page,
        "page_size": page_size,
        "total": total,
        "total_pages": ceil(total / page_size) if total else 0,
    }
