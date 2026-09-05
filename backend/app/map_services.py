import json
from collections.abc import Sequence
from urllib.parse import urlencode
from urllib.request import Request, urlopen

from app.config import get_settings
from app.errors import TENCENT_MAP_UNAVAILABLE, error

TENCENT_MAP_BASE_URL = "https://apis.map.qq.com"
MATRIX_BATCH_SIZE = 200


def _get_json(path: str, params: dict[str, str]) -> dict:
    settings = get_settings()
    if not settings.tencent_map_key:
        raise error(TENCENT_MAP_UNAVAILABLE, {"reason": "map key is not configured"})
    query = urlencode({**params, "key": settings.tencent_map_key})
    request = Request(
        f"{TENCENT_MAP_BASE_URL}{path}?{query}",
        headers={"Accept": "application/json"},
    )
    try:
        with urlopen(request, timeout=settings.tencent_map_timeout_seconds) as response:
            payload = json.loads(response.read().decode("utf-8"))
    except (OSError, UnicodeDecodeError, ValueError) as exc:
        raise error(TENCENT_MAP_UNAVAILABLE, {"reason": "map request failed"}) from exc
    if not isinstance(payload, dict) or payload.get("status") != 0:
        raise error(
            TENCENT_MAP_UNAVAILABLE,
            {
                "upstream_status": payload.get("status")
                if isinstance(payload, dict)
                else None,
                "reason": str(payload.get("message", "invalid map response"))[:200]
                if isinstance(payload, dict)
                else "invalid map response",
            },
        )
    return payload


def geocode_address(address: str) -> dict:
    payload = _get_json("/ws/geocoder/v1/", {"address": address})
    try:
        result = payload["result"]
        location = result["location"]
        return {
            "latitude": float(location["lat"]),
            "longitude": float(location["lng"]),
            "formatted_address": result.get("title") or address,
        }
    except (KeyError, TypeError, ValueError) as exc:
        raise error(
            TENCENT_MAP_UNAVAILABLE, {"reason": "invalid geocode response"}
        ) from exc


def route_distances(
    latitude: float,
    longitude: float,
    destinations: Sequence[tuple[float, float]],
) -> list[dict | None]:
    results: list[dict | None] = []
    origin = f"{latitude},{longitude}"
    for start in range(0, len(destinations), MATRIX_BATCH_SIZE):
        batch = destinations[start : start + MATRIX_BATCH_SIZE]
        payload = _get_json(
            "/ws/distance/v1/matrix",
            {
                "mode": "driving",
                "from": origin,
                "to": ";".join(f"{lat},{lng}" for lat, lng in batch),
            },
        )
        try:
            elements = payload["result"]["rows"][0]["elements"]
        except (KeyError, IndexError, TypeError) as exc:
            raise error(
                TENCENT_MAP_UNAVAILABLE, {"reason": "invalid distance response"}
            ) from exc
        if not isinstance(elements, list) or len(elements) != len(batch):
            raise error(
                TENCENT_MAP_UNAVAILABLE, {"reason": "incomplete distance response"}
            )
        try:
            for element in elements:
                if not isinstance(element, dict):
                    raise TypeError("distance element must be an object")
                if element.get("status", 0) != 0 or "duration" not in element:
                    results.append(None)
                    continue
                results.append(
                    {
                        "route_distance_meters": int(element["distance"]),
                        "route_duration_seconds": int(element["duration"]),
                    }
                )
        except (KeyError, TypeError, ValueError) as exc:
            raise error(
                TENCENT_MAP_UNAVAILABLE, {"reason": "invalid distance response"}
            ) from exc
    return results


def _decode_polyline(polyline: list) -> list[dict[str, float]]:
    if len(polyline) < 2 or len(polyline) % 2:
        raise error(TENCENT_MAP_UNAVAILABLE, {"reason": "invalid route polyline"})
    coordinates = [float(value) for value in polyline]
    for index in range(2, len(coordinates)):
        coordinates[index] = coordinates[index - 2] + coordinates[index] / 1_000_000
    return [
        {"latitude": coordinates[index], "longitude": coordinates[index + 1]}
        for index in range(0, len(coordinates), 2)
    ]


def route_plan(
    from_latitude: float,
    from_longitude: float,
    to_latitude: float,
    to_longitude: float,
    mode: str,
) -> dict:
    payload = _get_json(
        f"/ws/direction/v1/{mode}",
        {
            "from": f"{from_latitude},{from_longitude}",
            "to": f"{to_latitude},{to_longitude}",
        },
    )
    try:
        route = payload["result"]["routes"][0]
        return {
            "mode": mode,
            "distance_meters": int(route["distance"]),
            "duration_seconds": round(float(route["duration"]) * 60),
            "route_points": _decode_polyline(route["polyline"]),
        }
    except (KeyError, IndexError, TypeError, ValueError) as exc:
        raise error(
            TENCENT_MAP_UNAVAILABLE, {"reason": "invalid route response"}
        ) from exc
