from sqlalchemy import select
from sqlalchemy.orm import Session

from app.admin_services import operation_log
from app.enums import ACTIVE_ORDER_STATUSES, UserStatus
from app.errors import ACTIVE_ORDER_PREVENTS_FREEZE, error
from app.helpers import utcnow
from app.models import Admin, ChargingOrder, User


def set_user_status(
    db: Session, admin: Admin, user: User, target_status: UserStatus
) -> User:
    if target_status == UserStatus.FROZEN:
        active_order_id = db.scalar(
            select(ChargingOrder.id).where(
                ChargingOrder.user_id == user.id,
                ChargingOrder.status.in_(ACTIVE_ORDER_STATUSES),
            )
        )
        if active_order_id is not None:
            raise error(
                ACTIVE_ORDER_PREVENTS_FREEZE,
                {"order_id": active_order_id},
            )
    if user.status == target_status:
        return user
    user.status = target_status
    user.updated_at = utcnow()
    action = "FREEZE_USER" if target_status == UserStatus.FROZEN else "UNFREEZE_USER"
    operation_log(db, admin, action, "USER", user.id)
    db.commit()
    db.refresh(user)
    return user
