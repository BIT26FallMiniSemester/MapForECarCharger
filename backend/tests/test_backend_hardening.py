from datetime import datetime, timedelta

import pytest
from pydantic import ValidationError
from sqlalchemy import event, func, select
from sqlalchemy.orm import sessionmaker

from app import map_services
from app.admin_services import set_pile_status
from app.config import Settings
from app.device_services import update_heartbeat
from app.enums import LogSource, OrderStatus, PileStatus, StationStatus, UserStatus
from app.errors import ApiError
from app.helpers import to_utc_naive, utcnow
from app.models import (
    Admin,
    ChargingOrder,
    ChargingPile,
    PileStatusLog,
    RechargeRecord,
    Station,
    User,
)
from app.schemas import PileStatusUpdate
from app.services import (
    cancel_order,
    create_order,
    recharge,
    reserve_order,
    settle_order,
    stop_order,
    update_telemetry,
)
from app.user_admin_services import set_user_status


def _sessions(db_session):
    return sessionmaker(
        bind=db_session.get_bind(), autoflush=False, expire_on_commit=False
    )


def _order(db_session, status: OrderStatus, *, amount_cents: int = 0) -> int:
    now = utcnow()
    pile = db_session.get(ChargingPile, 1)
    if status == OrderStatus.CHARGING:
        pile.status = PileStatus.CHARGING
        pile.version += 1
    order = ChargingOrder(
        order_no=f"HARDEN-{status.value}",
        user_id=1,
        station_id=pile.station_id,
        pile_id=pile.id,
        status=status,
        unit_price_cents_per_kwh=125,
        energy_wh=1_000 if status == OrderStatus.UNPAID else 0,
        duration_seconds=60 if status == OrderStatus.UNPAID else 0,
        amount_cents=amount_cents,
        started_at=now - timedelta(seconds=60)
        if status in (OrderStatus.CHARGING, OrderStatus.UNPAID)
        else None,
        stopped_at=now if status == OrderStatus.UNPAID else None,
        version=1,
        created_at=now,
        updated_at=now,
    )
    db_session.add(order)
    db_session.commit()
    return order.id


def test_stale_reservation_cannot_revive_cancelled_order(db_session):
    order_id = _order(db_session, OrderStatus.PENDING)
    session_factory = _sessions(db_session)
    with session_factory() as cancelling, session_factory() as reserving:
        stale_order = reserving.get(ChargingOrder, order_id)
        _ = stale_order.pile.status
        cancel_order(cancelling, cancelling.get(ChargingOrder, order_id), "cancelled")
        with pytest.raises(ApiError) as raised:
            reserve_order(reserving, stale_order)
        assert raised.value.code == 40004
    db_session.expire_all()
    assert db_session.get(ChargingOrder, order_id).status == OrderStatus.CANCELLED
    assert db_session.get(ChargingPile, 1).status == PileStatus.IDLE


def test_stale_settlement_is_idempotent_and_deducts_once(db_session):
    order_id = _order(db_session, OrderStatus.UNPAID, amount_cents=1_000)
    session_factory = _sessions(db_session)
    with session_factory() as first, session_factory() as second:
        stale_order = second.get(ChargingOrder, order_id)
        stale_user = second.get(User, 1)
        settle_order(
            first,
            first.get(ChargingOrder, order_id),
            first.get(User, 1),
        )
        settled = settle_order(second, stale_order, stale_user)
        assert settled.status == OrderStatus.COMPLETED
    db_session.expire_all()
    assert db_session.get(User, 1).balance_cents == 49_000


def test_stale_stop_does_not_duplicate_transition_log(db_session):
    order_id = _order(db_session, OrderStatus.CHARGING)
    session_factory = _sessions(db_session)
    with session_factory() as first, session_factory() as second:
        stale_order = second.get(ChargingOrder, order_id)
        _ = stale_order.pile.status
        stop_order(first, first.get(ChargingOrder, order_id))
        repeated = stop_order(second, stale_order)
        assert repeated.status == OrderStatus.UNPAID
    log_count = db_session.scalar(
        select(func.count())
        .select_from(PileStatusLog)
        .where(
            PileStatusLog.order_id == order_id,
            PileStatusLog.source == LogSource.USER,
            PileStatusLog.new_status == PileStatus.IDLE,
        )
    )
    assert log_count == 1


def test_stale_telemetry_and_heartbeat_cannot_regress(db_session):
    order_id = _order(db_session, OrderStatus.CHARGING)
    session_factory = _sessions(db_session)
    reported = utcnow() + timedelta(seconds=10)
    with session_factory() as first, session_factory() as second:
        stale_order = second.get(ChargingOrder, order_id)
        update_telemetry(
            first,
            first.get(ChargingOrder, order_id),
            reported + timedelta(seconds=1),
            200,
        )
        with pytest.raises(ApiError) as raised:
            update_telemetry(second, stale_order, reported, 100)
        assert raised.value.code == 40004

    pile = db_session.get(ChargingPile, 2)
    pile.last_heartbeat_at = None
    db_session.commit()
    with session_factory() as first, session_factory() as second:
        stale_pile = second.get(ChargingPile, 2)
        update_heartbeat(
            first,
            first.get(ChargingPile, 2),
            reported + timedelta(seconds=1),
            "OFFLINE",
        )
        with pytest.raises(ApiError) as raised:
            update_heartbeat(second, stale_pile, reported, "ONLINE")
        assert raised.value.code == 40004
    db_session.expire_all()
    assert db_session.get(ChargingOrder, order_id).energy_wh == 200
    assert db_session.get(ChargingPile, 2).status == PileStatus.OFFLINE


def test_concurrent_recharges_use_atomic_balance_increment(db_session):
    session_factory = _sessions(db_session)
    with session_factory() as first, session_factory() as second:
        stale_user = second.get(User, 1)
        recharge(first, first.get(User, 1), 100, "hardening-recharge-1")
        record = recharge(second, stale_user, 200, "hardening-recharge-2")
        assert record.balance_before_cents == 50_100
        assert record.balance_after_cents == 50_300
    db_session.expire_all()
    assert db_session.get(User, 1).balance_cents == 50_300
    assert db_session.scalar(select(func.count()).select_from(RechargeRecord)) == 2


def test_stale_admin_status_change_cannot_override_reservation(db_session):
    order_id = _order(db_session, OrderStatus.PENDING)
    session_factory = _sessions(db_session)
    with session_factory() as user_session, session_factory() as admin_session:
        stale_pile = admin_session.get(ChargingPile, 1)
        reserve_order(user_session, user_session.get(ChargingOrder, order_id))
        with pytest.raises(ApiError) as raised:
            set_pile_status(
                admin_session,
                admin_session.get(Admin, 1),
                stale_pile,
                PileStatusUpdate(status=PileStatus.OFFLINE),
            )
        assert raised.value.code == 40003
    db_session.expire_all()
    assert db_session.get(ChargingOrder, order_id).status == OrderStatus.RESERVED
    assert db_session.get(ChargingPile, 1).status == PileStatus.RESERVED


def test_stale_user_cannot_create_order_after_freeze(db_session):
    session_factory = _sessions(db_session)
    with session_factory() as admin_session, session_factory() as user_session:
        stale_user = user_session.get(User, 1)
        set_user_status(
            admin_session,
            admin_session.get(Admin, 1),
            admin_session.get(User, 1),
            UserStatus.FROZEN,
        )
        with pytest.raises(ApiError) as raised:
            create_order(user_session, stale_user, 1, 1)
        assert raised.value.code == 40301
    db_session.expire_all()
    assert db_session.get(User, 1).status == UserStatus.FROZEN
    assert db_session.scalar(select(func.count()).select_from(ChargingOrder)) == 0


def test_timezone_cors_avatar_and_duplicate_pile_validation(
    client, user_headers, admin_headers, internal_headers
):
    value = datetime.fromisoformat("2026-09-04T08:30:00+08:00")
    assert to_utc_naive(value).isoformat() == "2026-09-04T00:30:00"

    preflight = client.options(
        "/api/v1/dashboard/overview",
        headers={
            "Origin": "http://localhost:5173",
            "Access-Control-Request-Method": "GET",
        },
    )
    assert preflight.status_code == 200
    assert preflight.headers["access-control-allow-origin"] == "*"

    invalid_avatar = client.post(
        "/api/v1/user/avatar",
        headers=user_headers,
        files={"avatar": ("avatar.png", b"not-an-image", "image/png")},
    )
    assert invalid_avatar.status_code == 400
    assert invalid_avatar.json()["code"] == 40001

    naive_heartbeat = client.post(
        "/api/v1/internal/piles/2/heartbeat",
        headers=internal_headers,
        json={"reported_at": "2026-09-04T08:30:00", "device_status": "ONLINE"},
    )
    assert naive_heartbeat.status_code == 422
    assert naive_heartbeat.json()["code"] == 40001

    duplicate_pile = client.post(
        "/api/v1/admin/stations/1/piles",
        headers=admin_headers,
        json={"pile_no": "TEST-001", "pile_type": "FAST", "rated_power_w": 60_000},
    )
    assert duplicate_pile.status_code == 409
    assert duplicate_pile.json()["code"] == 40008


def test_production_rejects_default_secrets_and_wildcard_cors():
    with pytest.raises(ValidationError):
        Settings(app_env="production", _env_file=None)
    settings = Settings(
        app_env="production",
        jwt_secret="j" * 32,
        internal_key="i" * 32,
        cors_origins="https://dashboard.example.com",
        _env_file=None,
    )
    assert settings.cors_origin_list == ["https://dashboard.example.com"]


def test_nearby_station_query_count_is_constant(
    client, user_headers, db_session, monkeypatch
):
    now = utcnow()
    db_session.add_all(
        Station(
            name=f"批量站点 {index}",
            address=f"地址 {index}",
            latitude=39.96,
            longitude=116.31,
            status=StationStatus.ACTIVE,
            created_at=now,
            updated_at=now,
        )
        for index in range(20)
    )
    db_session.commit()
    monkeypatch.setattr(
        map_services,
        "route_distances",
        lambda _latitude, _longitude, destinations: [
            {"route_distance_meters": 1000, "route_duration_seconds": 180}
            for _item in destinations
        ],
    )
    statements = []

    def count_statement(*_args):
        statements.append(1)

    engine = db_session.get_bind()
    event.listen(engine, "before_cursor_execute", count_statement)
    try:
        response = client.get(
            "/api/v1/stations/nearby",
            headers=user_headers,
            params={"latitude": 39.96, "longitude": 116.31},
        )
    finally:
        event.remove(engine, "before_cursor_execute", count_statement)
    assert response.status_code == 200
    assert len(statements) <= 4


def test_load_predictions_returns_one_latest_utc_batch(client, internal_headers):
    old = {
        "station_id": 1,
        "horizon_hours": 6,
        "model_version": "old-model",
        "generated_at": "2026-09-04T08:00:00+08:00",
        "points": [
            {
                "prediction_type": "LOAD_W",
                "predicted_for": "2026-09-04T09:00:00+08:00",
                "predicted_value": 100,
            }
        ],
    }
    new = {
        "station_id": 1,
        "horizon_hours": 6,
        "model_version": "new-model",
        "generated_at": "2026-09-04T09:00:00+08:00",
        "points": [
            {
                "prediction_type": "LOAD_W",
                "predicted_for": "2026-09-04T10:00:00+08:00",
                "predicted_value": 200,
            }
        ],
    }
    assert (
        client.post(
            "/api/v1/internal/predictions/load", headers=internal_headers, json=old
        ).status_code
        == 200
    )
    assert (
        client.post(
            "/api/v1/internal/predictions/load", headers=internal_headers, json=new
        ).status_code
        == 200
    )
    data = client.get("/api/v1/predictions/load?station_id=1&horizon_hours=6").json()[
        "data"
    ]
    assert data["model_version"] == "new-model"
    assert data["generated_at"] == "2026-09-04T01:00:00Z"
    assert data["points"] == [
        {"predicted_for": "2026-09-04T02:00:00Z", "load_w": 200.0}
    ]
