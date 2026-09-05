from collections.abc import Iterable

from sqlalchemy import case, func, select
from sqlalchemy.orm import Session

from app.enums import ONLINE_PILE_STATUSES, OrderStatus, PileStatus
from app.models import ChargingOrder, ChargingPile


def station_pile_stats(db: Session, station_ids: Iterable[int]) -> dict[int, dict]:
    ids = tuple(dict.fromkeys(station_ids))
    stats = {
        station_id: {
            "total_piles": 0,
            "available_piles": 0,
            "online_rate": 0.0,
        }
        for station_id in ids
    }
    if not ids:
        return stats
    rows = db.execute(
        select(
            ChargingPile.station_id,
            func.count(ChargingPile.id),
            func.sum(case((ChargingPile.status == PileStatus.IDLE, 1), else_=0)),
            func.sum(case((ChargingPile.status.in_(ONLINE_PILE_STATUSES), 1), else_=0)),
        )
        .where(ChargingPile.station_id.in_(ids))
        .group_by(ChargingPile.station_id)
    ).all()
    for station_id, total_value, available_value, online_value in rows:
        total = int(total_value or 0)
        available = int(available_value or 0)
        online = int(online_value or 0)
        stats[station_id] = {
            "total_piles": total,
            "available_piles": available,
            "online_rate": round(online * 100 / total, 2) if total else 0.0,
        }
    return stats


def pile_order_totals(db: Session, pile_ids: Iterable[int]) -> dict[int, dict]:
    ids = tuple(dict.fromkeys(pile_ids))
    totals = {
        pile_id: {
            "total_charge_count": 0,
            "total_charge_duration_seconds": 0,
        }
        for pile_id in ids
    }
    if not ids:
        return totals
    rows = db.execute(
        select(
            ChargingOrder.pile_id,
            func.count(ChargingOrder.id),
            func.coalesce(func.sum(ChargingOrder.duration_seconds), 0),
        )
        .where(
            ChargingOrder.pile_id.in_(ids),
            ChargingOrder.status.in_((OrderStatus.UNPAID, OrderStatus.COMPLETED)),
        )
        .group_by(ChargingOrder.pile_id)
    ).all()
    for pile_id, count_value, duration_value in rows:
        totals[pile_id] = {
            "total_charge_count": int(count_value or 0),
            "total_charge_duration_seconds": int(duration_value or 0),
        }
    return totals
