from datetime import UTC, datetime, timedelta


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


def test_realtime_orders_and_heartbeat_log(client, user_headers):
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
        headers={"X-Internal-Key": "development-internal-key-change-me"},
        json={
            "reported_at": (datetime.now(UTC) + timedelta(seconds=1)).isoformat(),
            "device_status": "OFFLINE",
        },
    )
    assert heartbeat.status_code == 200
    assert heartbeat.json()["data"]["status"] == "CHARGING"
