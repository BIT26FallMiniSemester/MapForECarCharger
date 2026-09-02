from typing import Annotated
from uuid import uuid4

from fastapi import APIRouter, Depends, File, Request, UploadFile

from app.config import BACKEND_DIR
from app.dependencies import DbSession, current_user
from app.errors import INVALID_REQUEST, error
from app.helpers import user_data, utcnow
from app.models import User
from app.responses import ApiEnvelope, success

router = APIRouter(tags=["users"])
ALLOWED_IMAGE_TYPES = {
    "image/jpeg": ".jpg",
    "image/png": ".png",
    "image/webp": ".webp",
}
MAX_AVATAR_BYTES = 2 * 1024 * 1024


@router.post("/user/avatar")
async def upload_avatar(
    request: Request,
    db: DbSession,
    user: Annotated[User, Depends(current_user)],
    avatar: Annotated[UploadFile, File(description="JPEG, PNG or WebP; maximum 2 MiB")],
) -> ApiEnvelope:
    extension = ALLOWED_IMAGE_TYPES.get(avatar.content_type or "")
    if extension is None:
        raise error(INVALID_REQUEST, {"reason": "avatar must be JPEG, PNG or WebP"})
    content = await avatar.read(MAX_AVATAR_BYTES + 1)
    if not content or len(content) > MAX_AVATAR_BYTES:
        raise error(
            INVALID_REQUEST, {"reason": "avatar must be between 1 byte and 2 MiB"}
        )
    target_dir = BACKEND_DIR / "runtime" / "avatars"
    target_dir.mkdir(parents=True, exist_ok=True)
    filename = f"user-{user.id}-{uuid4().hex}{extension}"
    (target_dir / filename).write_bytes(content)
    user.avatar_url = f"/static/avatars/{filename}"
    user.updated_at = utcnow()
    db.commit()
    db.refresh(user)
    return success(request, user_data(user))
