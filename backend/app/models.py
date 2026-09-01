from datetime import datetime
from decimal import Decimal

from sqlalchemy import (
    BigInteger,
    CheckConstraint,
    DateTime,
    Enum,
    ForeignKey,
    Index,
    Integer,
    Numeric,
    String,
    Text,
    UniqueConstraint,
    text,
)
from sqlalchemy.orm import Mapped, mapped_column, relationship

from app.database import Base
from app.enums import (
    AdminStatus,
    LogSource,
    OrderStatus,
    PileStatus,
    PileType,
    PredictionType,
    StationStatus,
    UserStatus,
)


def enum_type(enum_class, name: str) -> Enum:
    return Enum(
        enum_class,
        name=name,
        native_enum=False,
        create_constraint=True,
        values_callable=lambda values: [item.value for item in values],
    )


class TimestampMixin:
    created_at: Mapped[datetime] = mapped_column(DateTime, nullable=False)
    updated_at: Mapped[datetime] = mapped_column(DateTime, nullable=False)


class User(TimestampMixin, Base):
    __tablename__ = "users"
    __table_args__ = (
        CheckConstraint("balance_cents >= 0", name="ck_users_balance_nonnegative"),
        Index("idx_users_status", "status"),
    )
    id: Mapped[int] = mapped_column(Integer, primary_key=True, autoincrement=True)
    phone: Mapped[str] = mapped_column(String(11), nullable=False, unique=True)
    nickname: Mapped[str] = mapped_column(String(32), nullable=False)
    avatar_url: Mapped[str | None] = mapped_column(String(255))
    balance_cents: Mapped[int] = mapped_column(BigInteger, nullable=False, default=0)
    status: Mapped[UserStatus] = mapped_column(
        enum_type(UserStatus, "user_status"), nullable=False, default=UserStatus.NORMAL
    )
    orders: Mapped[list["ChargingOrder"]] = relationship(back_populates="user")
    recharge_records: Mapped[list["RechargeRecord"]] = relationship(
        back_populates="user"
    )


class Admin(TimestampMixin, Base):
    __tablename__ = "admins"
    id: Mapped[int] = mapped_column(Integer, primary_key=True, autoincrement=True)
    username: Mapped[str] = mapped_column(String(32), nullable=False, unique=True)
    password_hash: Mapped[str] = mapped_column(String(255), nullable=False)
    display_name: Mapped[str] = mapped_column(String(32), nullable=False)
    status: Mapped[AdminStatus] = mapped_column(
        enum_type(AdminStatus, "admin_status"),
        nullable=False,
        default=AdminStatus.NORMAL,
    )
    last_login_at: Mapped[datetime | None] = mapped_column(DateTime)


class Station(TimestampMixin, Base):
    __tablename__ = "stations"
    __table_args__ = (
        CheckConstraint(
            "latitude IS NULL OR (latitude >= -90 AND latitude <= 90)",
            name="ck_station_latitude",
        ),
        CheckConstraint(
            "longitude IS NULL OR (longitude >= -180 AND longitude <= 180)",
            name="ck_station_longitude",
        ),
        CheckConstraint(
            "price_cents_per_kwh IS NULL OR price_cents_per_kwh > 0",
            name="ck_station_price_positive",
        ),
        CheckConstraint(
            "fast_connector_count >= 0",
            name="ck_station_fast_connectors_nonnegative",
        ),
        CheckConstraint(
            "slow_connector_count >= 0",
            name="ck_station_slow_connectors_nonnegative",
        ),
        UniqueConstraint(
            "data_source", "external_id", name="uq_stations_source_external_id"
        ),
        Index("idx_stations_status", "status"),
        Index("idx_stations_location", "latitude", "longitude"),
        Index("idx_stations_operator", "operator_name"),
        Index("idx_stations_district", "district"),
        Index("idx_stations_source", "data_source"),
    )
    id: Mapped[int] = mapped_column(Integer, primary_key=True, autoincrement=True)
    name: Mapped[str] = mapped_column(String(64), nullable=False)
    address: Mapped[str] = mapped_column(String(255), nullable=False)
    latitude: Mapped[Decimal | None] = mapped_column(Numeric(10, 7))
    longitude: Mapped[Decimal | None] = mapped_column(Numeric(10, 7))
    price_cents_per_kwh: Mapped[int | None] = mapped_column(Integer)
    operator_name: Mapped[str | None] = mapped_column(String(64))
    service_type: Mapped[str | None] = mapped_column(String(32))
    district: Mapped[str | None] = mapped_column(String(32))
    region_scope: Mapped[str | None] = mapped_column(String(32))
    location_type: Mapped[str | None] = mapped_column(String(64))
    fast_connector_count: Mapped[int] = mapped_column(
        Integer, nullable=False, default=0, server_default=text("0")
    )
    slow_connector_count: Mapped[int] = mapped_column(
        Integer, nullable=False, default=0, server_default=text("0")
    )
    data_source: Mapped[str | None] = mapped_column(String(64))
    external_id: Mapped[str | None] = mapped_column(String(64))
    status: Mapped[StationStatus] = mapped_column(
        enum_type(StationStatus, "station_status"),
        nullable=False,
        default=StationStatus.ACTIVE,
    )
    piles: Mapped[list["ChargingPile"]] = relationship(back_populates="station")


class ChargingPile(TimestampMixin, Base):
    __tablename__ = "charging_piles"
    __table_args__ = (
        CheckConstraint("rated_power_w > 0", name="ck_piles_power_positive"),
        Index("idx_piles_station_status", "station_id", "status"),
    )
    id: Mapped[int] = mapped_column(Integer, primary_key=True, autoincrement=True)
    pile_no: Mapped[str] = mapped_column(String(32), nullable=False, unique=True)
    station_id: Mapped[int] = mapped_column(
        ForeignKey("stations.id", ondelete="RESTRICT"), nullable=False
    )
    pile_type: Mapped[PileType] = mapped_column(
        enum_type(PileType, "pile_type"), nullable=False
    )
    rated_power_w: Mapped[int] = mapped_column(Integer, nullable=False)
    status: Mapped[PileStatus] = mapped_column(
        enum_type(PileStatus, "pile_status"), nullable=False, default=PileStatus.IDLE
    )
    last_heartbeat_at: Mapped[datetime | None] = mapped_column(DateTime)
    version: Mapped[int] = mapped_column(Integer, nullable=False, default=1)
    station: Mapped[Station] = relationship(back_populates="piles")
    orders: Mapped[list["ChargingOrder"]] = relationship(back_populates="pile")


class ChargingOrder(TimestampMixin, Base):
    __tablename__ = "charging_orders"
    __table_args__ = (
        CheckConstraint("energy_wh >= 0", name="ck_orders_energy_nonnegative"),
        CheckConstraint("duration_seconds >= 0", name="ck_orders_duration_nonnegative"),
        CheckConstraint("amount_cents >= 0", name="ck_orders_amount_nonnegative"),
        Index("idx_orders_user_created", "user_id", "created_at"),
        Index("idx_orders_status_created", "status", "created_at"),
        Index("idx_orders_station_settled", "station_id", "settled_at"),
        Index("idx_orders_pile_started", "pile_id", "started_at"),
        Index(
            "uq_orders_one_active_per_user",
            "user_id",
            unique=True,
            sqlite_where=text("status IN ('PENDING','RESERVED','CHARGING','UNPAID')"),
        ),
        Index(
            "uq_orders_one_active_per_pile",
            "pile_id",
            unique=True,
            sqlite_where=text("status IN ('RESERVED','CHARGING')"),
        ),
    )
    id: Mapped[int] = mapped_column(Integer, primary_key=True, autoincrement=True)
    order_no: Mapped[str] = mapped_column(String(32), nullable=False, unique=True)
    user_id: Mapped[int] = mapped_column(
        ForeignKey("users.id", ondelete="RESTRICT"), nullable=False
    )
    station_id: Mapped[int] = mapped_column(
        ForeignKey("stations.id", ondelete="RESTRICT"), nullable=False
    )
    pile_id: Mapped[int] = mapped_column(
        ForeignKey("charging_piles.id", ondelete="RESTRICT"), nullable=False
    )
    status: Mapped[OrderStatus] = mapped_column(
        enum_type(OrderStatus, "order_status"),
        nullable=False,
        default=OrderStatus.PENDING,
    )
    unit_price_cents_per_kwh: Mapped[int] = mapped_column(Integer, nullable=False)
    energy_wh: Mapped[int] = mapped_column(BigInteger, nullable=False, default=0)
    duration_seconds: Mapped[int] = mapped_column(
        Integer, nullable=False, default=0, server_default=text("0")
    )
    amount_cents: Mapped[int] = mapped_column(BigInteger, nullable=False, default=0)
    reserved_at: Mapped[datetime | None] = mapped_column(DateTime)
    reservation_expires_at: Mapped[datetime | None] = mapped_column(DateTime)
    started_at: Mapped[datetime | None] = mapped_column(DateTime)
    stopped_at: Mapped[datetime | None] = mapped_column(DateTime)
    settled_at: Mapped[datetime | None] = mapped_column(DateTime)
    cancelled_at: Mapped[datetime | None] = mapped_column(DateTime)
    cancel_reason: Mapped[str | None] = mapped_column(String(255))
    version: Mapped[int] = mapped_column(Integer, nullable=False, default=1)
    user: Mapped[User] = relationship(back_populates="orders")
    station: Mapped[Station] = relationship()
    pile: Mapped[ChargingPile] = relationship(back_populates="orders")


class RechargeRecord(Base):
    __tablename__ = "recharge_records"
    __table_args__ = (
        CheckConstraint("amount_cents > 0", name="ck_recharge_amount_positive"),
        Index("idx_recharge_user_created", "user_id", "created_at"),
    )
    id: Mapped[int] = mapped_column(Integer, primary_key=True, autoincrement=True)
    recharge_no: Mapped[str] = mapped_column(String(32), nullable=False, unique=True)
    client_request_id: Mapped[str] = mapped_column(
        String(64), nullable=False, unique=True
    )
    user_id: Mapped[int] = mapped_column(
        ForeignKey("users.id", ondelete="RESTRICT"), nullable=False
    )
    amount_cents: Mapped[int] = mapped_column(BigInteger, nullable=False)
    balance_before_cents: Mapped[int] = mapped_column(BigInteger, nullable=False)
    balance_after_cents: Mapped[int] = mapped_column(BigInteger, nullable=False)
    channel: Mapped[str] = mapped_column(
        String(16), nullable=False, default="SIMULATED"
    )
    status: Mapped[str] = mapped_column(String(16), nullable=False, default="SUCCESS")
    created_at: Mapped[datetime] = mapped_column(DateTime, nullable=False)
    user: Mapped[User] = relationship(back_populates="recharge_records")


class PileStatusLog(Base):
    __tablename__ = "pile_status_logs"
    __table_args__ = (Index("idx_pile_logs_pile_created", "pile_id", "created_at"),)
    id: Mapped[int] = mapped_column(Integer, primary_key=True, autoincrement=True)
    pile_id: Mapped[int] = mapped_column(
        ForeignKey("charging_piles.id", ondelete="RESTRICT"), nullable=False
    )
    order_id: Mapped[int | None] = mapped_column(
        ForeignKey("charging_orders.id", ondelete="RESTRICT")
    )
    old_status: Mapped[PileStatus] = mapped_column(
        enum_type(PileStatus, "pile_log_old_status"), nullable=False
    )
    new_status: Mapped[PileStatus] = mapped_column(
        enum_type(PileStatus, "pile_log_new_status"), nullable=False
    )
    source: Mapped[LogSource] = mapped_column(
        enum_type(LogSource, "pile_log_source"), nullable=False
    )
    reason: Mapped[str | None] = mapped_column(String(255))
    created_at: Mapped[datetime] = mapped_column(DateTime, nullable=False)


class OperationLog(Base):
    __tablename__ = "operation_logs"
    __table_args__ = (
        Index("idx_operation_logs_admin_created", "admin_id", "created_at"),
        Index("idx_operation_logs_target", "target_type", "target_id"),
    )
    id: Mapped[int] = mapped_column(Integer, primary_key=True, autoincrement=True)
    admin_id: Mapped[int] = mapped_column(
        ForeignKey("admins.id", ondelete="RESTRICT"), nullable=False
    )
    action: Mapped[str] = mapped_column(String(64), nullable=False)
    target_type: Mapped[str] = mapped_column(String(32), nullable=False)
    target_id: Mapped[int] = mapped_column(Integer, nullable=False)
    request_json: Mapped[str | None] = mapped_column(Text)
    result: Mapped[str] = mapped_column(String(16), nullable=False)
    created_at: Mapped[datetime] = mapped_column(DateTime, nullable=False)


class LoadPrediction(Base):
    __tablename__ = "load_predictions"
    __table_args__ = (
        UniqueConstraint(
            "station_id",
            "prediction_type",
            "horizon_hours",
            "predicted_for",
            "model_version",
            name="uq_load_predictions_key",
        ),
        CheckConstraint("horizon_hours IN (1, 6, 24)", name="ck_prediction_horizon"),
        Index("idx_predictions_lookup", "station_id", "horizon_hours", "predicted_for"),
    )
    id: Mapped[int] = mapped_column(Integer, primary_key=True, autoincrement=True)
    station_id: Mapped[int | None] = mapped_column(
        ForeignKey("stations.id", ondelete="RESTRICT")
    )
    prediction_type: Mapped[PredictionType] = mapped_column(
        enum_type(PredictionType, "prediction_type"), nullable=False
    )
    horizon_hours: Mapped[int] = mapped_column(Integer, nullable=False)
    predicted_for: Mapped[datetime] = mapped_column(DateTime, nullable=False)
    predicted_value: Mapped[Decimal] = mapped_column(Numeric(16, 4), nullable=False)
    model_version: Mapped[str] = mapped_column(String(64), nullable=False)
    generated_at: Mapped[datetime] = mapped_column(DateTime, nullable=False)
