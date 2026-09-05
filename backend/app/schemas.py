from decimal import Decimal
from typing import Annotated

from pydantic import AwareDatetime, BaseModel, Field, StringConstraints, field_validator

from app.enums import PileStatus, PileType, PredictionType, StationStatus

Phone = Annotated[str, StringConstraints(pattern=r"^1[3-9]\d{9}$")]
NonBlank32 = Annotated[
    str, StringConstraints(strip_whitespace=True, min_length=1, max_length=32)
]


class UserLogin(BaseModel):
    phone: Phone


class AdminLogin(BaseModel):
    username: Annotated[str, StringConstraints(min_length=1, max_length=32)]
    password: Annotated[str, StringConstraints(min_length=1, max_length=128)]


class ProfileUpdate(BaseModel):
    nickname: NonBlank32


class RechargeCreate(BaseModel):
    amount_cents: int = Field(ge=1, le=1_000_000)
    client_request_id: Annotated[str, StringConstraints(min_length=8, max_length=64)]


class OrderCreate(BaseModel):
    station_id: int = Field(gt=0)
    pile_id: int = Field(gt=0)


class CancelOrder(BaseModel):
    reason: Annotated[
        str | None, StringConstraints(strip_whitespace=True, max_length=255)
    ] = None


class StationPileCreate(BaseModel):
    pile_no: Annotated[
        str, StringConstraints(strip_whitespace=True, min_length=1, max_length=32)
    ]
    pile_type: PileType
    rated_power_w: int = Field(gt=0)


class StationCreate(BaseModel):
    name: Annotated[
        str, StringConstraints(strip_whitespace=True, min_length=1, max_length=64)
    ]
    address: Annotated[
        str, StringConstraints(strip_whitespace=True, min_length=1, max_length=255)
    ]
    latitude: Decimal = Field(ge=-90, le=90)
    longitude: Decimal = Field(ge=-180, le=180)
    price_cents_per_kwh: int = Field(gt=0)
    operator_name: Annotated[
        str | None,
        StringConstraints(strip_whitespace=True, min_length=1, max_length=64),
    ] = None
    service_type: Annotated[
        str | None,
        StringConstraints(strip_whitespace=True, min_length=1, max_length=32),
    ] = None
    district: Annotated[
        str | None,
        StringConstraints(strip_whitespace=True, min_length=1, max_length=32),
    ] = None
    region_scope: Annotated[
        str | None,
        StringConstraints(strip_whitespace=True, min_length=1, max_length=32),
    ] = None
    location_type: Annotated[
        str | None,
        StringConstraints(strip_whitespace=True, min_length=1, max_length=64),
    ] = None
    fast_connector_count: int = Field(default=0, ge=0)
    slow_connector_count: int = Field(default=0, ge=0)
    piles: list[StationPileCreate] = Field(default_factory=list, max_length=100)


class StationUpdate(BaseModel):
    name: Annotated[
        str | None,
        StringConstraints(strip_whitespace=True, min_length=1, max_length=64),
    ] = None
    address: Annotated[
        str | None,
        StringConstraints(strip_whitespace=True, min_length=1, max_length=255),
    ] = None
    latitude: Decimal | None = Field(default=None, ge=-90, le=90)
    longitude: Decimal | None = Field(default=None, ge=-180, le=180)
    price_cents_per_kwh: int | None = Field(default=None, gt=0)
    operator_name: Annotated[
        str | None,
        StringConstraints(strip_whitespace=True, min_length=1, max_length=64),
    ] = None
    service_type: Annotated[
        str | None,
        StringConstraints(strip_whitespace=True, min_length=1, max_length=32),
    ] = None
    district: Annotated[
        str | None,
        StringConstraints(strip_whitespace=True, min_length=1, max_length=32),
    ] = None
    region_scope: Annotated[
        str | None,
        StringConstraints(strip_whitespace=True, min_length=1, max_length=32),
    ] = None
    location_type: Annotated[
        str | None,
        StringConstraints(strip_whitespace=True, min_length=1, max_length=64),
    ] = None
    fast_connector_count: int | None = Field(default=None, ge=0)
    slow_connector_count: int | None = Field(default=None, ge=0)
    status: StationStatus | None = None

    @field_validator(
        "status",
        "name",
        "address",
        "latitude",
        "longitude",
        "price_cents_per_kwh",
        "operator_name",
        "service_type",
        "district",
        "region_scope",
        "location_type",
        "fast_connector_count",
        "slow_connector_count",
    )
    @classmethod
    def values_cannot_be_null(cls, value):
        if value is None:
            raise ValueError("explicit null is not allowed")
        return value


class PileStatusUpdate(BaseModel):
    status: PileStatus
    reason: Annotated[
        str | None, StringConstraints(strip_whitespace=True, max_length=255)
    ] = None


class HeartbeatCreate(BaseModel):
    reported_at: AwareDatetime
    device_status: Annotated[str, StringConstraints(pattern=r"^(ONLINE|OFFLINE)$")]


class TelemetryCreate(BaseModel):
    reported_at: AwareDatetime
    energy_wh: int = Field(ge=0)


class PredictionPointCreate(BaseModel):
    prediction_type: PredictionType
    predicted_for: AwareDatetime
    predicted_value: Decimal


class PredictionsCreate(BaseModel):
    station_id: int | None = Field(default=None, gt=0)
    horizon_hours: int
    model_version: Annotated[str, StringConstraints(min_length=1, max_length=64)]
    generated_at: AwareDatetime
    points: list[PredictionPointCreate] = Field(min_length=1, max_length=500)

    @field_validator("horizon_hours")
    @classmethod
    def validate_horizon(cls, value: int) -> int:
        if value not in {1, 6, 24}:
            raise ValueError("horizon_hours must be 1, 6, or 24")
        return value
