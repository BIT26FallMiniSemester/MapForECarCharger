from app.enums import StationStatus
from app.helpers import utcnow
from app.models import Station


def test_station_catalog_mapping_requires_internal_key(client):
    response = client.get("/api/v1/internal/stations/catalog-mappings")
    assert response.status_code == 401
    assert response.json()["code"] == 40101


def test_station_catalog_mapping_list_filter_and_empty_result(
    client, internal_headers, db_session
):
    now = utcnow()
    stations = [
        Station(
            name="公共站 A",
            address="公共地址 A",
            data_source="PUBLIC_TEST",
            external_id="A-001",
            status=StationStatus.ACTIVE,
            created_at=now,
            updated_at=now,
        ),
        Station(
            name="公共站 B",
            address="公共地址 B",
            data_source="PUBLIC_TEST",
            external_id="B-002",
            status=StationStatus.ACTIVE,
            created_at=now,
            updated_at=now,
        ),
        Station(
            name="其他来源站",
            address="其他地址",
            data_source="OTHER_TEST",
            external_id="A-001",
            status=StationStatus.ACTIVE,
            created_at=now,
            updated_at=now,
        ),
    ]
    db_session.add_all(stations)
    db_session.commit()

    listing = client.get(
        "/api/v1/internal/stations/catalog-mappings",
        headers=internal_headers,
        params={"page": 1, "page_size": 2},
    )
    assert listing.status_code == 200
    listing_data = listing.json()["data"]
    assert {key: listing_data[key] for key in ("page", "page_size", "total")} == {
        "page": 1,
        "page_size": 2,
        "total": 3,
    }
    assert len(listing_data["items"]) == 2
    assert set(listing_data["items"][0]) == {
        "station_id",
        "data_source",
        "external_id",
        "station_name",
    }

    exact = client.get(
        "/api/v1/internal/stations/catalog-mappings",
        headers=internal_headers,
        params={"data_source": "PUBLIC_TEST", "external_id": "B-002"},
    )
    exact_data = exact.json()["data"]
    assert exact.status_code == 200
    assert exact_data["total"] == 1
    assert exact_data["items"] == [
        {
            "station_id": stations[1].id,
            "data_source": "PUBLIC_TEST",
            "external_id": "B-002",
            "station_name": "公共站 B",
        }
    ]

    empty = client.get(
        "/api/v1/internal/stations/catalog-mappings",
        headers=internal_headers,
        params={"data_source": "MISSING"},
    )
    assert empty.status_code == 200
    assert empty.json()["data"]["items"] == []
    assert empty.json()["data"]["total"] == 0

    invalid = client.get(
        "/api/v1/internal/stations/catalog-mappings?page_size=1001",
        headers=internal_headers,
    )
    assert invalid.status_code == 422
    assert invalid.json()["code"] == 40001
