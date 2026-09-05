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


def _matches_image_type(content_type: str, content: bytes) -> bool:
    if content_type == "image/jpeg":
        return content.startswith(b"\xff\xd8\xff")
    if content_type == "image/png":
        return content.startswith(b"\x89PNG\r\n\x1a\n")
    if content_type == "image/webp":
        return (
            len(content) >= 12
            and content.startswith(b"RIFF")
            and content[8:12] == b"WEBP"
        )
    return False


@router.post("/users/me/avatar")
@router.post("/user/avatar", include_in_schema=False)
async def upload_avatar(
    request: Request,
    db: DbSession,
    user: Annotated[User, Depends(current_user)],
    avatar: Annotated[UploadFile, File(description="JPEG, PNG or WebP; maximum 2 MiB")],
) -> ApiEnvelope:
    content_type = avatar.content_type or ""
    extension = ALLOWED_IMAGE_TYPES.get(content_type)
    if extension is None:
        raise error(INVALID_REQUEST, {"reason": "avatar must be JPEG, PNG or WebP"})
    content = await avatar.read(MAX_AVATAR_BYTES + 1)
    if not content or len(content) > MAX_AVATAR_BYTES:
        raise error(
            INVALID_REQUEST, {"reason": "avatar must be between 1 byte and 2 MiB"}
        )
    if not _matches_image_type(content_type, content):
        raise error(
            INVALID_REQUEST, {"reason": "avatar content does not match its type"}
        )
    target_dir = BACKEND_DIR / "runtime" / "avatars"
    target_dir.mkdir(parents=True, exist_ok=True)
    filename = f"user-{user.id}-{uuid4().hex}{extension}"
    target = target_dir / filename
    target.write_bytes(content)
    user.avatar_url = f"/static/avatars/{filename}"
    user.updated_at = utcnow()
    try:
        db.commit()
    except Exception:
        target.unlink(missing_ok=True)
        raise
    db.refresh(user)
    return success(request, user_data(user))
