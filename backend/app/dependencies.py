import hmac
from typing import Annotated

import jwt
from fastapi import Depends, Header
from fastapi.security import HTTPAuthorizationCredentials, HTTPBearer
from sqlalchemy.orm import Session

from app.config import get_settings
from app.database import get_db
from app.enums import AdminStatus, UserStatus
from app.errors import AUTHENTICATION_REQUIRED, PERMISSION_DENIED, USER_FROZEN, error
from app.models import Admin, User
from app.security import decode_access_token

DbSession = Annotated[Session, Depends(get_db)]
bearer = HTTPBearer(auto_error=False)


def _token_payload(credentials: HTTPAuthorizationCredentials | None) -> dict:
    if credentials is None or credentials.scheme.lower() != "bearer":
        raise error(AUTHENTICATION_REQUIRED)
    try:
        return decode_access_token(credentials.credentials)
    except jwt.PyJWTError:
        raise error(AUTHENTICATION_REQUIRED)


def _subject_id(payload: dict) -> int:
    try:
        return int(payload["sub"])
    except (KeyError, TypeError, ValueError):
        raise error(AUTHENTICATION_REQUIRED) from None


def current_user(
    db: DbSession,
    credentials: Annotated[HTTPAuthorizationCredentials | None, Depends(bearer)],
) -> User:
    payload = _token_payload(credentials)
    if payload.get("role") != "USER":
        raise error(PERMISSION_DENIED)
    user = db.get(User, _subject_id(payload))
    if user is None:
        raise error(AUTHENTICATION_REQUIRED)
    if user.status == UserStatus.FROZEN:
        raise error(USER_FROZEN)
    return user


def current_admin(
    db: DbSession,
    credentials: Annotated[HTTPAuthorizationCredentials | None, Depends(bearer)],
) -> Admin:
    payload = _token_payload(credentials)
    if payload.get("role") != "ADMIN":
        raise error(PERMISSION_DENIED)
    admin = db.get(Admin, _subject_id(payload))
    if admin is None or admin.status != AdminStatus.NORMAL:
        raise error(AUTHENTICATION_REQUIRED)
    return admin


def require_internal_key(
    x_internal_key: Annotated[str | None, Header()] = None,
) -> None:
    expected = get_settings().internal_key
    if x_internal_key is None or not hmac.compare_digest(x_internal_key, expected):
        raise error(AUTHENTICATION_REQUIRED)
