from datetime import datetime

from sqlalchemy import or_, update
from sqlalchemy.orm import Session

from app.enums import LogSource, PileStatus
from app.errors import TELEMETRY_OUT_OF_ORDER, error
from app.helpers import as_utc, to_utc_naive
from app.models import ChargingPile, PileStatusLog


def update_heartbeat(
    db: Session, pile: ChargingPile, reported_at: datetime, device_status: str
) -> ChargingPile:
    reported = to_utc_naive(reported_at)
    for _attempt in range(3):
        if pile.last_heartbeat_at is not None:
            if reported < pile.last_heartbeat_at:
                raise error(
                    TELEMETRY_OUT_OF_ORDER,
                    {"last_heartbeat_at": as_utc(pile.last_heartbeat_at)},
                )
            if reported == pile.last_heartbeat_at:
                return pile
        old = pile.status
        new = old
        if device_status == "OFFLINE" and old not in (
            PileStatus.RESERVED,
            PileStatus.CHARGING,
        ):
            new = PileStatus.OFFLINE
        elif device_status == "ONLINE" and old == PileStatus.OFFLINE:
            new = PileStatus.IDLE
        version = pile.version
        changed = old != new
        result = db.execute(
            update(ChargingPile)
            .where(
                ChargingPile.id == pile.id,
                ChargingPile.version == version,
                or_(
                    ChargingPile.last_heartbeat_at.is_(None),
                    ChargingPile.last_heartbeat_at < reported,
                ),
            )
            .values(
                last_heartbeat_at=reported,
                status=new,
                version=version + int(changed),
                updated_at=max(pile.updated_at, reported),
            )
            .execution_options(synchronize_session=False)
        )
        if result.rowcount == 1:
            if changed:
                db.add(
                    PileStatusLog(
                        pile_id=pile.id,
                        order_id=None,
                        old_status=old,
                        new_status=new,
                        source=LogSource.SIMULATOR,
                        reason=f"device reported {device_status}",
                        created_at=reported,
                    )
                )
            db.commit()
            db.refresh(pile)
            return pile
        db.rollback()
        db.refresh(pile)
    raise error(
        TELEMETRY_OUT_OF_ORDER,
        {"reason": "pile changed concurrently; retry heartbeat"},
    )
