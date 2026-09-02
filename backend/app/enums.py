from enum import StrEnum


class UserStatus(StrEnum):
    NORMAL = "NORMAL"
    FROZEN = "FROZEN"


class AdminStatus(StrEnum):
    NORMAL = "NORMAL"
    DISABLED = "DISABLED"


class StationStatus(StrEnum):
    ACTIVE = "ACTIVE"
    INACTIVE = "INACTIVE"


class PileStatus(StrEnum):
    IDLE = "IDLE"
    RESERVED = "RESERVED"
    CHARGING = "CHARGING"
    FAULT = "FAULT"
    OFFLINE = "OFFLINE"


class PileType(StrEnum):
    FAST = "FAST"
    SLOW = "SLOW"


class OrderStatus(StrEnum):
    PENDING = "PENDING"
    RESERVED = "RESERVED"
    CHARGING = "CHARGING"
    UNPAID = "UNPAID"
    COMPLETED = "COMPLETED"
    CANCELLED = "CANCELLED"


class LogSource(StrEnum):
    USER = "USER"
    ADMIN = "ADMIN"
    SIMULATOR = "SIMULATOR"
    SYSTEM = "SYSTEM"


class PredictionType(StrEnum):
    LOAD_W = "LOAD_W"
    AVAILABLE_PILES = "AVAILABLE_PILES"
    CONGESTION_SCORE = "CONGESTION_SCORE"


ACTIVE_ORDER_STATUSES = (
    OrderStatus.PENDING,
    OrderStatus.RESERVED,
    OrderStatus.CHARGING,
    OrderStatus.UNPAID,
)
ONLINE_PILE_STATUSES = (
    PileStatus.IDLE,
    PileStatus.RESERVED,
    PileStatus.CHARGING,
    PileStatus.FAULT,
)
