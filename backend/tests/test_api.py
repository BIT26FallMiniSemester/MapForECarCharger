from datetime import UTC, datetime, timedelta

import jwt

from app.config import get_settings
from app.helpers import utcnow
from app.models import ChargingOrder


def test_health_and_uniform_validation(client):
    health = client.get("/api/v1/health")
    assert health.status_code == 200
    assert health.json()["data"]["database"] == "ok"
    invalid = client.post("/api/v1/auth/user/login", json={"phone": "123"})
    assert invalid.status_code == 422
    assert invalid.json()["code"] == 40001
    assert invalid.json()["request_id"]


def test_authentication_and_frozen_user(client):
    missing = client.get("/api/v1/users/me")
    assert missing.status_code == 401
    assert missing.json()["code"] == 40101
    frozen = client.post("/api/v1/auth/user/login", json={"phone": "13900000002"})
    assert frozen.status_code == 403
    assert frozen.json()["code"] == 40301
    bad_admin = client.post(
        "/api/v1/auth/admin/login", json={"username": "admin", "password": "wrong"}
    )
    assert bad_admin.status_code == 401
    assert bad_admin.json()["code"] == 40101


def test_malformed_token_subject_is_rejected(client):
    settings = get_settings()
    token = jwt.encode(
        {"sub": "not-an-integer", "role": "USER"},
        settings.jwt_secret,
        algorithm=settings.jwt_algorithm,
    )
    response = client.get(
        "/api/v1/users/me",
        headers={"Authorization": f"Bearer {token}"},
    )
    assert response.status_code == 401
    assert response.json()["code"] == 40101


def test_recharge_is_idempotent(client, user_headers):
    payload = {"amount_cents": 5000, "client_request_id": "pytest-recharge-0001"}
    first = client.post("/api/v1/wallet/recharges", headers=user_headers, json=payload)
    second = client.post("/api/v1/wallet/recharges", headers=user_headers, json=payload)
    assert first.status_code == second.status_code == 200
    assert first.json()["data"] == second.json()["data"]
    profile = client.get("/api/v1/users/me", headers=user_headers)
    assert profile.json()["data"]["balance_cents"] == 55_000


def test_complete_charging_flow_and_idempotent_settlement(
    client, user_headers, db_session
):
    created = client.post(
        "/api/v1/orders",
        headers=user_headers,
        json={"station_id": 1, "pile_id": 1},
    )
    assert created.status_code == 201
    order_id = created.json()["data"]["id"]

    duplicate = client.post(
        "/api/v1/orders",
        headers=user_headers,
        json={"station_id": 1, "pile_id": 2},
    )
    assert duplicate.status_code == 409
    assert duplicate.json()["code"] == 40002

    reserved = client.post(
        f"/api/v1/orders/{order_id}/reserve", headers=user_headers, json={}
    )
    assert reserved.json()["data"]["status"] == "RESERVED"
    started = client.post(
        f"/api/v1/orders/{order_id}/start", headers=user_headers, json={}
    )
    assert started.json()["data"]["status"] == "CHARGING"
    order = db_session.get(ChargingOrder, order_id)
    order.started_at = utcnow() - timedelta(minutes=30)
    db_session.commit()
    progress = client.get(f"/api/v1/orders/{order_id}", headers=user_headers)
    assert progress.json()["data"]["estimated"] is True
    assert progress.json()["data"]["energy_wh"] >= 30_000

    stopped = client.post(
        f"/api/v1/orders/{order_id}/stop", headers=user_headers, json={}
    )
    assert stopped.json()["data"]["status"] == "UNPAID"
    assert stopped.json()["data"]["estimated"] is False
    repeated_stop = client.post(
        f"/api/v1/orders/{order_id}/stop", headers=user_headers, json={}
    )
    assert repeated_stop.status_code == 200
    assert repeated_stop.json()["data"] == stopped.json()["data"]
    amount_cents = stopped.json()["data"]["amount_cents"]

    first = client.post(
        f"/api/v1/orders/{order_id}/settle", headers=user_headers, json={}
    )
    second = client.post(
        f"/api/v1/orders/{order_id}/settle", headers=user_headers, json={}
    )
    assert first.status_code == second.status_code == 200
    assert first.json()["data"]["balance_cents"] == 50_000 - amount_cents
    assert second.json()["data"]["balance_cents"] == 50_000 - amount_cents
    assert second.json()["data"]["order"]["status"] == "COMPLETED"


def test_cancel_releases_reserved_pile(client, user_headers):
    created = client.post(
        "/api/v1/orders",
        headers=user_headers,
        json={"station_id": 1, "pile_id": 2},
    )
    order_id = created.json()["data"]["id"]
    client.post(f"/api/v1/orders/{order_id}/reserve", headers=user_headers, json={})
    cancelled = client.post(
        f"/api/v1/orders/{order_id}/cancel",
        headers=user_headers,
        json={"reason": "test"},
    )
    assert cancelled.json()["data"]["status"] == "CANCELLED"
    pile = client.get("/api/v1/piles/2", headers=user_headers)
    assert pile.json()["data"]["status"] == "IDLE"


def test_admin_and_dashboard_endpoints(client, admin_headers):
    users = client.get("/api/v1/admin/users", headers=admin_headers)
    assert users.status_code == 200
    assert users.json()["data"]["total"] == 2
    station = client.post(
        "/api/v1/admin/stations",
        headers=admin_headers,
        json={
            "name": "新增站",
            "address": "新增地址",
            "latitude": 39.9,
            "longitude": 116.4,
            "price_cents_per_kwh": 130,
            "piles": [
                {"pile_no": "NEW-001", "pile_type": "FAST", "rated_power_w": 120000}
            ],
        },
    )
    assert station.status_code == 201
    overview = client.get("/api/v1/dashboard/overview")
    assert overview.status_code == 200
    assert overview.json()["data"]["station_count"] == 2
    assert overview.json()["data"]["user_count"] == 2
    assert overview.json()["data"]["total_energy_wh"] == 0


def test_prediction_upsert_and_read(client, internal_headers):
    payload = {
        "station_id": 1,
        "horizon_hours": 6,
        "model_version": "test-v1",
        "generated_at": datetime.now(UTC).isoformat(),
        "points": [
            {
                "prediction_type": "LOAD_W",
                "predicted_for": (datetime.now(UTC) + timedelta(hours=1)).isoformat(),
                "predicted_value": 123000,
            }
        ],
    }
    first = client.post(
        "/api/v1/internal/predictions/load", headers=internal_headers, json=payload
    )
    payload["points"][0]["predicted_value"] = 125000
    second = client.post(
        "/api/v1/internal/predictions/load", headers=internal_headers, json=payload
    )
    assert first.status_code == second.status_code == 200
    result = client.get("/api/v1/predictions/load?station_id=1&horizon_hours=6")
    assert result.status_code == 200
    assert result.json()["data"]["points"][0]["load_w"] == 125000.0
