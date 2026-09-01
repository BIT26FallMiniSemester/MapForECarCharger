"""
Geocode station addresses with Tencent Map WebService and update project JSON.

Usage examples:

  python tools/geocode_stations_tencent.py \
    --input data/stations_seed.json \
    --output data/stations_seed_geocoded.json \
    --key YOUR_TENCENT_MAP_KEY

For Windows PowerShell:

  python tools/geocode_stations_tencent.py `
    --input data/stations_seed.json `
    --output data/stations_seed_geocoded.json `
    --key YOUR_TENCENT_MAP_KEY

The script is designed for large public datasets:
- uses a cache file so repeated runs do not waste API quota
- supports request delay and retry
- records geocode_status for each station
- supports --limit for small trial runs
- can prefix addresses with a city, default: 北京市

Tencent API document endpoint:
  https://apis.map.qq.com/ws/geocoder/v1/
"""

from __future__ import annotations

import argparse
import json
import time
import urllib.parse
import urllib.request
from pathlib import Path
from typing import Any, Dict, Optional, Tuple

API_URL = "https://apis.map.qq.com/ws/geocoder/v1/"


def read_json(path: Path) -> Dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def write_json(path: Path, data: Dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding="utf-8")


def normalize_address(address: str, city_prefix: str) -> str:
    address = str(address or "").strip()
    if not address:
        return ""
    if city_prefix and not address.startswith(city_prefix):
        return f"{city_prefix}{address}"
    return address


def load_cache(path: Path) -> Dict[str, Any]:
    if not path.exists():
        return {}
    try:
        return read_json(path)
    except json.JSONDecodeError:
        return {}


def save_cache(path: Path, cache: Dict[str, Any]) -> None:
    write_json(path, cache)


def call_tencent_geocoder(query: str, key: str, timeout: int) -> Dict[str, Any]:
    params = urllib.parse.urlencode({"address": query, "key": key})
    url = f"{API_URL}?{params}"
    req = urllib.request.Request(url, headers={"User-Agent": "MiniSemester-Geocoder/1.0"})
    with urllib.request.urlopen(req, timeout=timeout) as response:
        body = response.read().decode("utf-8")
    return json.loads(body)


def geocode(
    query: str,
    key: str,
    cache: Dict[str, Any],
    timeout: int,
    retries: int,
    retry_delay: float,
) -> Tuple[str, Optional[float], Optional[float], Dict[str, Any]]:
    if query in cache:
        item = cache[query]
        return item.get("status", "CACHED"), item.get("latitude"), item.get("longitude"), item

    last_error = ""
    for attempt in range(1, retries + 2):
        try:
            payload = call_tencent_geocoder(query, key, timeout)
            status_code = payload.get("status")
            message = payload.get("message", "")

            if status_code == 0 and payload.get("result", {}).get("location"):
                location = payload["result"]["location"]
                lat = float(location["lat"])
                lng = float(location["lng"])
                cached = {
                    "status": "OK",
                    "latitude": lat,
                    "longitude": lng,
                    "raw_status": status_code,
                    "message": message,
                    "title": payload.get("result", {}).get("title"),
                    "ad_info": payload.get("result", {}).get("ad_info"),
                }
                cache[query] = cached
                return "OK", lat, lng, cached

            last_error = f"Tencent status={status_code}, message={message}"
        except Exception as exc:  # noqa: BLE001 - keep CLI robust for batch conversion
            last_error = str(exc)

        if attempt <= retries:
            time.sleep(retry_delay)

    cached = {
        "status": "FAILED",
        "latitude": None,
        "longitude": None,
        "error": last_error,
    }
    cache[query] = cached
    return "FAILED", None, None, cached


def update_station(station: Dict[str, Any], query: str, status: str, lat: Optional[float], lng: Optional[float], detail: Dict[str, Any]) -> None:
    station["geocode_query"] = query
    station["geocode_status"] = status
    station["geocode_source"] = "tencent"

    if status == "OK" and lat is not None and lng is not None:
        station["latitude"] = round(lat, 7)
        station["longitude"] = round(lng, 7)
        if detail.get("title"):
            station["geocode_title"] = detail["title"]
        if detail.get("ad_info"):
            station["geocode_ad_info"] = detail["ad_info"]
    else:
        station["geocode_error"] = detail.get("error") or detail.get("message") or "unknown geocode error"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Replace demo station coordinates with Tencent Map geocoded coordinates.")
    parser.add_argument("--input", required=True, help="Input project JSON, such as data/stations_seed.json")
    parser.add_argument("--output", required=True, help="Output JSON with geocoded coordinates")
    parser.add_argument("--key", required=True, help="Tencent Map WebService key")
    parser.add_argument("--cache", default="data/geocode_cache_tencent.json", help="Cache file path")
    parser.add_argument("--city-prefix", default="北京市", help="Prefix added when address does not start with it")
    parser.add_argument("--limit", type=int, default=None, help="Only process first N stations, useful for trial runs")
    parser.add_argument("--delay", type=float, default=0.12, help="Delay seconds between uncached API calls")
    parser.add_argument("--timeout", type=int, default=10, help="HTTP timeout seconds")
    parser.add_argument("--retries", type=int, default=2, help="Retry count for each failed address")
    parser.add_argument("--retry-delay", type=float, default=1.0, help="Delay seconds before retry")
    parser.add_argument("--save-every", type=int, default=50, help="Save cache every N processed stations")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    input_path = Path(args.input)
    output_path = Path(args.output)
    cache_path = Path(args.cache)

    data = read_json(input_path)
    stations = data.get("stations", [])
    cache = load_cache(cache_path)

    total = len(stations) if args.limit is None else min(args.limit, len(stations))
    ok_count = 0
    failed_count = 0
    cached_count = 0

    for index, station in enumerate(stations[:total], start=1):
        query = normalize_address(station.get("address", ""), args.city_prefix)
        if not query:
            update_station(station, query, "FAILED", None, None, {"error": "empty address"})
            failed_count += 1
            continue

        was_cached = query in cache
        status, lat, lng, detail = geocode(
            query=query,
            key=args.key,
            cache=cache,
            timeout=args.timeout,
            retries=args.retries,
            retry_delay=args.retry_delay,
        )
        update_station(station, query, status, lat, lng, detail)

        if status == "OK":
            ok_count += 1
        else:
            failed_count += 1
        if was_cached:
            cached_count += 1
        else:
            time.sleep(args.delay)

        if index % args.save_every == 0:
            save_cache(cache_path, cache)
            print(f"Processed {index}/{total}: OK={ok_count}, FAILED={failed_count}, CACHED={cached_count}")

    save_cache(cache_path, cache)

    metadata = data.setdefault("metadata", {})
    metadata["geocode"] = {
        "source": "tencent",
        "city_prefix": args.city_prefix,
        "processed_stations": total,
        "ok_count": ok_count,
        "failed_count": failed_count,
        "cached_count": cached_count,
        "cache_file": str(cache_path),
    }

    write_json(output_path, data)
    print(f"Finished: OK={ok_count}, FAILED={failed_count}, CACHED={cached_count}")
    print(f"Output: {output_path}")
    print(f"Cache: {cache_path}")


if __name__ == "__main__":
    main()
