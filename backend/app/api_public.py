from typing import Annotated

from fastapi import APIRouter, Depends, Query, Request
from sqlalchemy import func, select, text
from sqlalchemy.exc import IntegrityError

from app.config import get_settings
from app.dependencies import DbSession, current_user
from app.enums import AdminStatus, UserStatus
from app.errors import INVALID_CREDENTIALS, USER_FROZEN, error
from app.helpers import as_utc, page_data, recharge_data, user_data, utcnow
from app.models import Admin, RechargeRecord, User
from app.responses import ApiEnvelope, success
from app.schemas import AdminLogin, ProfileUpdate, RechargeCreate, UserLogin
from app.security import create_access_token, verify_password
from app.services import recharge

router = APIRouter(tags=["system and users"])


@router.get("/health")
def health(request: Request, db: DbSession) -> ApiEnvelope:
    db.execute(text("SELECT 1"))
    settings = get_settings()
    return success(
        request,
        {
            "status": "ok",
            "database": "ok",
            "version": settings.app_version,
            "time": as_utc(utcnow()),
        },
    )


@router.post("/auth/user/login")
@router.post("/user/login", include_in_schema=False)
def user_login(payload: UserLogin, request: Request, db: DbSession) -> ApiEnvelope:
    user = db.scalar(select(User).where(User.phone == payload.phone))
    is_new = user is None
    if user is None:
        now = utcnow()
        user = User(
            phone=payload.phone,
            nickname=f"用户{payload.phone[-4:]}",
            balance_cents=0,
            status=UserStatus.NORMAL,
            created_at=now,
            updated_at=now,
        )
        db.add(user)
        try:
            db.commit()
        except IntegrityError:
            db.rollback()
            user = db.scalar(select(User).where(User.phone == payload.phone))
            is_new = False
        else:
            db.refresh(user)
    if user.status == UserStatus.FROZEN:
        raise error(USER_FROZEN)
    settings = get_settings()
    token = create_access_token(user.id, "USER", settings.user_token_expire_seconds)
    return success(
        request,
        {
            "access_token": token,
            "token_type": "bearer",
            "expires_in": settings.user_token_expire_seconds,
            "is_new_user": is_new,
            "user": user_data(user),
        },
    )


@router.post("/auth/admin/login")
@router.post("/admin/login", include_in_schema=False)
def admin_login(payload: AdminLogin, request: Request, db: DbSession) -> ApiEnvelope:
    admin = db.scalar(select(Admin).where(Admin.username == payload.username))
    if (
        admin is None
        or admin.status != AdminStatus.NORMAL
        or not verify_password(payload.password, admin.password_hash)
    ):
        raise error(INVALID_CREDENTIALS)
    admin.last_login_at = utcnow()
    admin.updated_at = admin.last_login_at
    db.commit()
    settings = get_settings()
    token = create_access_token(admin.id, "ADMIN", settings.admin_token_expire_seconds)
    return success(
        request,
        {
            "access_token": token,
            "token_type": "bearer",
            "expires_in": settings.admin_token_expire_seconds,
            "admin": {
                "id": admin.id,
                "username": admin.username,
                "display_name": admin.display_name,
            },
        },
    )


@router.get("/users/me")
@router.get("/user/profile", include_in_schema=False)
def profile(
    request: Request, user: Annotated[User, Depends(current_user)]
) -> ApiEnvelope:
    return success(request, user_data(user))


@router.patch("/users/me")
@router.put("/user/profile", include_in_schema=False)
def update_profile(
    payload: ProfileUpdate,
    request: Request,
    db: DbSession,
    user: Annotated[User, Depends(current_user)],
) -> ApiEnvelope:
    user.nickname = payload.nickname
    user.updated_at = utcnow()
    db.commit()
    db.refresh(user)
    return success(request, user_data(user))


@router.post("/wallet/recharges")
@router.post("/user/recharge", include_in_schema=False)
def create_recharge(
    payload: RechargeCreate,
    request: Request,
    db: DbSession,
    user: Annotated[User, Depends(current_user)],
) -> ApiEnvelope:
    record = recharge(db, user, payload.amount_cents, payload.client_request_id)
    return success(request, recharge_data(record))


@router.get("/wallet/recharges")
@router.get("/user/recharge-records", include_in_schema=False)
def recharge_records(
    request: Request,
    db: DbSession,
    user: Annotated[User, Depends(current_user)],
    page: Annotated[int, Query(ge=1)] = 1,
    page_size: Annotated[int, Query(ge=1, le=100)] = 20,
) -> ApiEnvelope:
    where = RechargeRecord.user_id == user.id
    total = (
        db.scalar(select(func.count()).select_from(RechargeRecord).where(where)) or 0
    )
    records = db.scalars(
        select(RechargeRecord)
        .where(where)
        .order_by(RechargeRecord.created_at.desc())
        .offset((page - 1) * page_size)
        .limit(page_size)
    ).all()
    return success(
        request,
        page_data([recharge_data(item) for item in records], page, page_size, total),
    )
