from dataclasses import dataclass, field
from typing import Any


@dataclass
class ApiError(Exception):
    code: int
    message: str
    status_code: int
    details: dict[str, Any] | None = field(default=None)


VALIDATION_ERROR = (10001, "validation error", 422)
INVALID_REQUEST = (10002, "invalid request", 400)
AUTHENTICATION_REQUIRED = (20001, "authentication required", 401)
PERMISSION_DENIED = (20002, "permission denied", 403)
USER_FROZEN = (20003, "user is frozen", 403)
INVALID_CREDENTIALS = (20004, "invalid credentials", 401)
RESOURCE_NOT_FOUND = (30001, "resource not found", 404)
USER_HAS_ACTIVE_ORDER = (40001, "user has active order", 409)
PILE_NOT_AVAILABLE = (40002, "charging pile is not available", 409)
INVALID_ORDER_STATE = (40003, "invalid order state", 409)
ORDER_NOT_OWNED = (40004, "order is not owned by current user", 403)
INSUFFICIENT_BALANCE = (40005, "insufficient balance", 409)
ACTIVE_ORDER_PREVENTS_FREEZE = (40006, "active order prevents freeze", 409)
DUPLICATE_RESOURCE = (40007, "duplicate resource", 409)
TELEMETRY_OUT_OF_ORDER = (40008, "telemetry is out of order", 409)
INTERNAL_ERROR = (50000, "internal server error", 500)
SERVICE_UNAVAILABLE = (50001, "service unavailable", 503)


def error(definition: tuple[int, str, int], details: dict | None = None) -> ApiError:
    return ApiError(*definition, details=details)
