from datetime import datetime

from sqlalchemy.orm import Session

from app.enums import LogSource, PileStatus
from app.errors import TELEMETRY_OUT_OF_ORDER, error
from app.helpers import as_utc
from app.models import ChargingPile, PileStatusLog


def update_heartbeat(
    db: Session, pile: ChargingPile, reported_at: datetime, device_status: str
) -> ChargingPile:
    reported = reported_at.replace(tzinfo=None)
    if pile.last_heartbeat_at is not None and reported < pile.last_heartbeat_at:
        raise error(
            TELEMETRY_OUT_OF_ORDER,
            {"last_heartbeat_at": as_utc(pile.last_heartbeat_at)},
        )
    old = pile.status
    pile.last_heartbeat_at = reported
    if device_status == "OFFLINE" and pile.status not in (
        PileStatus.RESERVED,
        PileStatus.CHARGING,
    ):
        pile.status = PileStatus.OFFLINE
    elif device_status == "ONLINE" and pile.status == PileStatus.OFFLINE:
        pile.status = PileStatus.IDLE
    if old != pile.status:
        pile.version += 1
        db.add(
            PileStatusLog(
                pile_id=pile.id,
                order_id=None,
                old_status=old,
                new_status=pile.status,
                source=LogSource.SIMULATOR,
                reason=f"device reported {device_status}",
                created_at=reported,
            )
        )
    pile.updated_at = reported
    db.commit()
    db.refresh(pile)
    return pile
