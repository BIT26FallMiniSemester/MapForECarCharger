from dataclasses import dataclass, field
from typing import Any


@dataclass
class ApiError(Exception):
    code: int
    message: str
    status_code: int
    details: dict[str, Any] | None = field(default=None)


VALIDATION_ERROR = (40001, "请求参数不正确", 422)
INVALID_REQUEST = (40001, "请求内容不正确", 400)
USER_HAS_ACTIVE_ORDER = (40002, "用户已有未完成订单", 409)
PILE_NOT_AVAILABLE = (40003, "该电桩刚刚被其他用户预约，请重新选择", 409)
INVALID_ORDER_STATE = (40004, "当前订单状态不允许该操作", 409)
RESERVATION_EXPIRED = (40005, "预约已过期", 409)
INSUFFICIENT_BALANCE = (40006, "余额不足", 409)
ACTIVE_ORDER_PREVENTS_FREEZE = (40007, "用户还有未完成订单，暂时不能冻结", 409)
DUPLICATE_RESOURCE = (40008, "资源编号重复", 409)
TELEMETRY_OUT_OF_ORDER = (40004, "设备数据顺序不正确", 409)
AUTHENTICATION_REQUIRED = (40101, "未登录或登录已失效", 401)
INVALID_CREDENTIALS = (40101, "账号或密码不正确", 401)
PERMISSION_DENIED = (40301, "没有权限", 403)
USER_FROZEN = (40301, "用户已被冻结", 403)
ORDER_NOT_OWNED = (40301, "订单不属于当前用户", 403)
RESOURCE_NOT_FOUND = (40401, "数据不存在", 404)
TENCENT_MAP_UNAVAILABLE = (50301, "腾讯地图服务暂时不可用", 503)
INTERNAL_ERROR = (50000, "internal server error", 500)
SERVICE_UNAVAILABLE = (50001, "service unavailable", 503)


def error(definition: tuple[int, str, int], details: dict | None = None) -> ApiError:
    return ApiError(*definition, details=details)
