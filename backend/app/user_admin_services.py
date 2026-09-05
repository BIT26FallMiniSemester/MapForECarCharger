from sqlalchemy import select, update
from sqlalchemy.orm import Session

from app.admin_services import operation_log
from app.enums import ACTIVE_ORDER_STATUSES, UserStatus
from app.errors import ACTIVE_ORDER_PREVENTS_FREEZE, error
from app.helpers import utcnow
from app.models import Admin, ChargingOrder, User


def set_user_status(
    db: Session, admin: Admin, user: User, target_status: UserStatus
) -> User:
    if user.status == target_status:
        return user
    now = utcnow()
    filters = [User.id == user.id, User.status != target_status]
    if target_status == UserStatus.FROZEN:
        filters.append(
            ~select(ChargingOrder.id)
            .where(
                ChargingOrder.user_id == user.id,
                ChargingOrder.status.in_(ACTIVE_ORDER_STATUSES),
            )
            .exists()
        )
    transitioned = db.execute(
        update(User)
        .where(*filters)
        .values(status=target_status, updated_at=now)
        .execution_options(synchronize_session=False)
    )
    if transitioned.rowcount != 1:
        db.rollback()
        db.refresh(user)
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
    action = "FREEZE_USER" if target_status == UserStatus.FROZEN else "UNFREEZE_USER"
    operation_log(db, admin, action, "USER", user.id)
    db.commit()
    db.refresh(user)
    return user
