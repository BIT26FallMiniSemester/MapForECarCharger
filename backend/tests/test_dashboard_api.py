from datetime import timedelta

from app.enums import LogSource, OrderStatus, PileStatus
from app.helpers import utcnow
from app.models import ChargingOrder, ChargingPile, PileStatusLog, Station


def test_dashboard_empty_aggregates(client, db_session):
    station = db_session.get(Station, 1)
    station.latitude = None
    station.longitude = None
    db_session.commit()

    stations = client.get("/api/v1/dashboard/stations-map")
    assert stations.status_code == 200
    assert stations.json()["data"]["items"] == []

    hourly = client.get("/api/v1/dashboard/hourly-demand?days=7")
    assert hourly.status_code == 200
    assert hourly.json()["data"]["days"] == 7
    assert hourly.json()["data"]["items"] == [
        {"hour": hour, "order_count": 0, "energy_wh": 0} for hour in range(24)
    ]

    alerts = client.get("/api/v1/dashboard/alerts")
    assert alerts.status_code == 200
    assert alerts.json()["data"]["items"] == []

    invalid = client.get("/api/v1/dashboard/hourly-demand?days=0")
    assert invalid.status_code == 422
    assert invalid.json()["code"] == 10001


def test_dashboard_map_hourly_demand_and_alert_aggregates(client, db_session):
    now = utcnow()
    today = now.replace(hour=0, minute=0, second=0, microsecond=0)
    pile_two = db_session.get(ChargingPile, 2)
    pile_two.status = PileStatus.CHARGING
    pile_two.version += 1
    pile_two.updated_at = now

    orders = [
        ChargingOrder(
            order_no="DASHBOARD-RECENT-03",
            user_id=1,
            station_id=1,
            pile_id=1,
            status=OrderStatus.COMPLETED,
            unit_price_cents_per_kwh=125,
            energy_wh=500,
            duration_seconds=600,
            amount_cents=63,
            started_at=today - timedelta(days=1) + timedelta(hours=3),
            stopped_at=today - timedelta(days=1) + timedelta(hours=3, minutes=10),
            settled_at=today - timedelta(days=1) + timedelta(hours=3, minutes=11),
            version=5,
            created_at=today - timedelta(days=1) + timedelta(hours=2, minutes=55),
            updated_at=today - timedelta(days=1) + timedelta(hours=3, minutes=11),
        ),
        ChargingOrder(
            order_no="DASHBOARD-RECENT-15",
            user_id=1,
            station_id=1,
            pile_id=2,
            status=OrderStatus.COMPLETED,
            unit_price_cents_per_kwh=125,
            energy_wh=700,
            duration_seconds=900,
            amount_cents=88,
            started_at=today - timedelta(days=1) + timedelta(hours=15),
            stopped_at=today - timedelta(days=1) + timedelta(hours=15, minutes=15),
            settled_at=today - timedelta(days=1) + timedelta(hours=15, minutes=16),
            version=5,
            created_at=today - timedelta(days=1) + timedelta(hours=14, minutes=55),
            updated_at=today - timedelta(days=1) + timedelta(hours=15, minutes=16),
        ),
        ChargingOrder(
            order_no="DASHBOARD-OLD",
            user_id=1,
            station_id=1,
            pile_id=1,
            status=OrderStatus.COMPLETED,
            unit_price_cents_per_kwh=125,
            energy_wh=900,
            duration_seconds=1200,
            amount_cents=113,
            started_at=today - timedelta(days=40) + timedelta(hours=3),
            stopped_at=today - timedelta(days=40) + timedelta(hours=3, minutes=20),
            settled_at=today - timedelta(days=40) + timedelta(hours=3, minutes=21),
            version=5,
            created_at=today - timedelta(days=40) + timedelta(hours=2, minutes=55),
            updated_at=today - timedelta(days=40) + timedelta(hours=3, minutes=21),
        ),
    ]
    logs = [
        PileStatusLog(
            pile_id=1,
            old_status=PileStatus.IDLE,
            new_status=PileStatus.FAULT,
            source=LogSource.ADMIN,
            reason="test fault",
            created_at=now - timedelta(minutes=3),
        ),
        PileStatusLog(
            pile_id=2,
            old_status=PileStatus.IDLE,
            new_status=PileStatus.OFFLINE,
            source=LogSource.SIMULATOR,
            reason="test offline",
            created_at=now - timedelta(minutes=2),
        ),
        PileStatusLog(
            pile_id=1,
            old_status=PileStatus.FAULT,
            new_status=PileStatus.IDLE,
            source=LogSource.ADMIN,
            reason="test recovery",
            created_at=now - timedelta(minutes=1),
        ),
        PileStatusLog(
            pile_id=1,
            old_status=PileStatus.IDLE,
            new_status=PileStatus.RESERVED,
            source=LogSource.USER,
            reason="not an alert",
            created_at=now,
        ),
    ]
    db_session.add_all([*orders, *logs])
    db_session.commit()

    overview = client.get("/api/v1/dashboard/overview").json()["data"]
    assert overview["user_count"] == 2
    assert overview["total_energy_wh"] == 2100

    stations = client.get("/api/v1/dashboard/stations-map").json()["data"]["items"]
    assert len(stations) == 1
    assert stations[0] == {
        "station_id": 1,
        "station_name": "测试站",
        "latitude": 39.96,
        "longitude": 116.31,
        "total_piles": 2,
        "available_piles": 1,
        "reserved_piles": 0,
        "charging_piles": 1,
        "fault_piles": 0,
        "offline_piles": 0,
        "online_rate": 100.0,
        "utilization_rate": 50.0,
    }

    hourly = client.get("/api/v1/dashboard/hourly-demand?days=30").json()["data"]
    assert len(hourly["items"]) == 24
    assert hourly["items"][3] == {"hour": 3, "order_count": 1, "energy_wh": 500}
    assert hourly["items"][15] == {
        "hour": 15,
        "order_count": 1,
        "energy_wh": 700,
    }
    assert sum(item["order_count"] for item in hourly["items"]) == 2

    alerts = client.get("/api/v1/dashboard/alerts?limit=20").json()["data"]["items"]
    assert [item["type"] for item in alerts] == [
        "PILE_RECOVERED",
        "PILE_OFFLINE",
        "PILE_FAULT",
    ]
    assert [item["level"] for item in alerts] == ["INFO", "WARNING", "ERROR"]
    assert all(item["station_id"] == 1 for item in alerts)
    assert (
        client.get("/api/v1/dashboard/alerts?limit=2").json()["data"]["items"]
        == alerts[:2]
    )
