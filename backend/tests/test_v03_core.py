from types import SimpleNamespace

from app import map_services
from app.main import app


def test_v03_core_paths_are_canonical():
    paths = app.openapi()["paths"]
    assert "/api/v1/auth/user/login" in paths
    assert "/api/v1/auth/admin/login" in paths
    assert "/api/v1/users/me" in paths
    assert "/api/v1/wallet/recharges" in paths
    assert "/api/v1/map/geocode" in paths
    assert "/api/v1/map/route" in paths
    assert "/api/v1/user/login" not in paths
    assert "/api/v1/admin/login" not in paths
    assert "/api/v1/user/profile" not in paths


def test_create_order_requires_matching_station(client, user_headers):
    missing_station = client.post(
        "/api/v1/orders", headers=user_headers, json={"pile_id": 1}
    )
    assert missing_station.status_code == 422
    assert missing_station.json()["code"] == 40001

    mismatch = client.post(
        "/api/v1/orders",
        headers=user_headers,
        json={"station_id": 999, "pile_id": 1},
    )
    assert mismatch.status_code == 404
    assert mismatch.json()["code"] == 40401


def test_map_endpoints_and_route_distance_sorting(client, user_headers, monkeypatch):
    def fake_get_json(path, _params):
        if path == "/ws/geocoder/v1/":
            return {
                "status": 0,
                "result": {
                    "title": "北京市海淀区测试地址",
                    "location": {"lat": 39.96, "lng": 116.31},
                },
            }
        return {
            "status": 0,
            "result": {
                "routes": [
                    {
                        "distance": 2300,
                        "duration": 10,
                        "polyline": [39.96, 116.31, 1000, 2000],
                    }
                ]
            },
        }

    monkeypatch.setattr(map_services, "_get_json", fake_get_json)
    geocode = client.get(
        "/api/v1/map/geocode",
        headers=user_headers,
        params={"address": "北京市海淀区测试地址"},
    )
    assert geocode.status_code == 200
    assert geocode.json()["data"]["latitude"] == 39.96

    route = client.get(
        "/api/v1/map/route",
        headers=user_headers,
        params={
            "from_latitude": 39.96,
            "from_longitude": 116.31,
            "to_latitude": 39.97,
            "to_longitude": 116.32,
            "mode": "walking",
        },
    )
    assert route.status_code == 200
    assert route.json()["data"]["duration_seconds"] == 600
    assert route.json()["data"]["route_points"][1] == {
        "latitude": 39.961,
        "longitude": 116.312,
    }

    monkeypatch.setattr(
        map_services,
        "route_distances",
        lambda _latitude, _longitude, destinations: [
            {"route_distance_meters": 2300, "route_duration_seconds": 620}
            for _item in destinations
        ],
    )
    nearby = client.get(
        "/api/v1/stations/nearby",
        headers=user_headers,
        params={"latitude": 39.96, "longitude": 116.31},
    )
    data = nearby.json()["data"]
    assert nearby.status_code == 200
    assert data["page"] == 1
    assert data["total"] == 1
    assert data["items"][0]["route_distance_meters"] == 2300


def test_map_key_failure_is_explicit(client, user_headers, monkeypatch):
    monkeypatch.setattr(
        map_services,
        "get_settings",
        lambda: SimpleNamespace(tencent_map_key=None, tencent_map_timeout_seconds=5),
    )
    response = client.get(
        "/api/v1/map/geocode",
        headers=user_headers,
        params={"address": "北京市海淀区测试地址"},
    )
    assert response.status_code == 503
    assert response.json()["code"] == 50301


def test_distance_matrix_discards_failed_destinations(monkeypatch):
    monkeypatch.setattr(
        map_services,
        "_get_json",
        lambda _path, _params: {
            "status": 0,
            "result": {
                "rows": [
                    {
                        "elements": [
                            {"distance": 1200, "duration": 240},
                            {"distance": 300, "status": 4},
                        ]
                    }
                ]
            },
        },
    )
    assert map_services.route_distances(
        39.96, 116.31, [(39.97, 116.32), (39.98, 116.33)]
    ) == [
        {"route_distance_meters": 1200, "route_duration_seconds": 240},
        None,
    ]
