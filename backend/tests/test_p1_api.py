from datetime import UTC, datetime, timedelta

from sqlalchemy import select

from app.enums import LogSource, OrderStatus, PileStatus
from app.helpers import utcnow
from app.models import ChargingOrder, ChargingPile, PileStatusLog


def test_avatar_upload_and_static_file(client, user_headers):
    response = client.post(
        "/api/v1/user/avatar",
        headers=user_headers,
        files={"avatar": ("avatar.png", b"\x89PNG\r\n\x1a\n-test", "image/png")},
    )
    assert response.status_code == 200
    avatar_url = response.json()["data"]["avatar_url"]
    assert avatar_url.startswith("/static/avatars/user-")
    image = client.get(avatar_url)
    assert image.status_code == 200
    assert image.content.startswith(b"\x89PNG")


def test_realtime_orders_and_heartbeat_log(client, user_headers, internal_headers):
    created = client.post("/api/v1/orders", headers=user_headers, json={"pile_id": 1})
    order_id = created.json()["data"]["id"]
    client.post(f"/api/v1/orders/{order_id}/reserve", headers=user_headers, json={})
    client.post(f"/api/v1/orders/{order_id}/start", headers=user_headers, json={})
    realtime = client.get("/api/v1/dashboard/realtime-orders")
    assert realtime.status_code == 200
    assert realtime.json()["data"]["items"][0]["id"] == order_id

    # A business-bound pile must not be forced offline by a heartbeat.
    heartbeat = client.post(
        "/api/v1/internal/piles/1/heartbeat",
        headers=internal_headers,
        json={
            "reported_at": (datetime.now(UTC) + timedelta(seconds=1)).isoformat(),
            "device_status": "OFFLINE",
        },
    )
    assert heartbeat.status_code == 200
    assert heartbeat.json()["data"]["status"] == "CHARGING"


def test_expired_reservation_releases_pile_and_allows_new_order(
    client, user_headers, db_session
):
    created = client.post("/api/v1/orders", headers=user_headers, json={"pile_id": 1})
    order_id = created.json()["data"]["id"]
    reserved = client.post(f"/api/v1/orders/{order_id}/reserve", headers=user_headers)
    assert reserved.status_code == 200
    assert reserved.json()["data"]["reservation_expires_at"] is not None

    order = db_session.get(ChargingOrder, order_id)
    order.reservation_expires_at = utcnow() - timedelta(seconds=1)
    db_session.commit()

    start = client.post(f"/api/v1/orders/{order_id}/start", headers=user_headers)
    assert start.status_code == 409
    assert start.json()["code"] == 40009

    db_session.expire_all()
    assert db_session.get(ChargingOrder, order_id).status == OrderStatus.CANCELLED
    assert (
        db_session.get(ChargingOrder, order_id).cancel_reason == "reservation expired"
    )
    assert db_session.get(ChargingPile, 1).status == PileStatus.IDLE
    log = db_session.scalar(
        select(PileStatusLog)
        .where(PileStatusLog.order_id == order_id)
        .order_by(PileStatusLog.id.desc())
    )
    assert log.source == LogSource.SYSTEM

    active = client.get("/api/v1/orders/active", headers=user_headers)
    assert active.status_code == 200
    assert active.json()["data"] is None
    replacement = client.post(
        "/api/v1/orders", headers=user_headers, json={"pile_id": 2}
    )
    assert replacement.status_code == 201


def test_internal_reservation_sweep_is_idempotent(
    client, user_headers, internal_headers, db_session
):
    created = client.post("/api/v1/orders", headers=user_headers, json={"pile_id": 1})
    order_id = created.json()["data"]["id"]
    client.post(f"/api/v1/orders/{order_id}/reserve", headers=user_headers)
    order = db_session.get(ChargingOrder, order_id)
    order.reservation_expires_at = utcnow() - timedelta(seconds=1)
    db_session.commit()

    first = client.post(
        "/api/v1/internal/orders/expire-reservations", headers=internal_headers
    )
    second = client.post(
        "/api/v1/internal/orders/expire-reservations", headers=internal_headers
    )
    assert first.status_code == second.status_code == 200
    assert first.json()["data"]["expired_count"] == 1
    assert first.json()["data"]["expired_order_ids"] == [order_id]
    assert second.json()["data"]["expired_count"] == 0
    start = client.post(f"/api/v1/orders/{order_id}/start", headers=user_headers)
    assert start.status_code == 409
    assert start.json()["code"] == 40009
