from decimal import Decimal

from sqlalchemy import func, select

from app.models import ChargingPile, Station
from app.station_import import DATA_SOURCE, import_public_stations


def test_public_station_import_is_idempotent(db_session):
    piles_before = db_session.scalar(select(func.count()).select_from(ChargingPile))
    first = import_public_stations(db_session)
    second = import_public_stations(db_session)
    assert first.total_rows == 2614
    assert first.inserted == 2614
    assert first.districts_unresolved == 680
    assert second.inserted == 0
    assert second.unchanged == 2614
    imported = db_session.scalar(
        select(func.count())
        .select_from(Station)
        .where(Station.data_source == DATA_SOURCE)
    )
    assert imported == 2614
    assert (
        db_session.scalar(select(func.count()).select_from(ChargingPile))
        == piles_before
    )
    first_station = db_session.scalar(
        select(Station).where(
            Station.data_source == DATA_SOURCE, Station.external_id == "1"
        )
    )
    assert first_station.latitude == Decimal("40.0029050")
    assert first_station.longitude == Decimal("116.5001470")


def test_public_station_filters_and_cleaned_location(
    client, user_headers, db_session, monkeypatch
):
    import_public_stations(db_session)
    filtered = client.get(
        "/api/v1/stations",
        headers=user_headers,
        params={"operator_name": "特来电", "page_size": 100},
    )
    assert filtered.status_code == 200
    assert filtered.json()["data"]["total"] == 214
    station = filtered.json()["data"]["items"][0]
    assert station["operator_name"] == "特来电"
    assert station["latitude"] is not None
    assert station["longitude"] is not None
    assert station["price_cents_per_kwh"] is None
    assert station["total_connector_count"] == (
        station["fast_connector_count"] + station["slow_connector_count"]
    )
    assert station["is_bookable"] is False

    options = client.get("/api/v1/stations/filter-options", headers=user_headers)
    assert options.status_code == 200
    data = options.json()["data"]
    assert data["station_count"] == 2615
    assert data["with_coordinates_count"] == 2615
    assert data["without_coordinates_count"] == 0
    assert "特来电" in data["operator_names"]
    assert "朝阳区" in data["districts"]

    monkeypatch.setattr(
        "app.api_orders.map_services.route_distances",
        lambda _latitude, _longitude, destinations: [
            {"route_distance_meters": 1000, "route_duration_seconds": 180}
            for _item in destinations
        ],
    )
    nearby = client.get(
        "/api/v1/stations/nearby",
        headers=user_headers,
        params={"latitude": 39.96, "longitude": 116.31},
    )
    assert nearby.status_code == 200
    data = nearby.json()["data"]
    assert data["total"] == 1
    assert data["items"][0]["route_distance_meters"] == 1000
