#!/usr/bin/env python3
"""Resolve catalog IDs and post predictions through documented internal APIs."""
import json
import os
import sys
from pathlib import Path
from urllib.parse import urlencode
from urllib.request import Request, urlopen

from export_predictions_api import PREDICTION_ENDPOINT

PAGE_SIZE = 1000


def api_base(base_url):
    base_url = base_url.rstrip("/")
    return base_url if base_url.endswith("/api/v1") else base_url + "/api/v1"


def request_json(request, opener=urlopen):
    with opener(request, timeout=15) as response:
        body = json.load(response)
    if body.get("code") != 0:
        raise RuntimeError(f"backend error: {body.get('code')} {body.get('message')}")
    return body["data"]


def fetch_catalog_mappings(base_url, key, data_sources, opener=urlopen):
    mappings = {}
    for data_source in sorted(data_sources):
        page = 1
        while True:
            query = urlencode({"data_source": data_source, "page": page, "page_size": PAGE_SIZE})
            request = Request(
                f"{api_base(base_url)}/internal/stations/catalog-mappings?{query}",
                headers={"X-Internal-Key": key}, method="GET",
            )
            items = request_json(request, opener)["items"]
            previous_count = len(mappings)
            for item in items:
                identity = (str(item["data_source"]), str(item["external_id"]))
                station_id = int(item["station_id"])
                if station_id <= 0:
                    raise ValueError("invalid backend station ID")
                if identity in mappings and mappings[identity] != station_id:
                    raise ValueError(f"duplicate catalog mapping: {identity}")
                mappings[identity] = station_id
            if len(items) < PAGE_SIZE:
                break
            if len(mappings) == previous_count:
                raise ValueError("catalog pagination did not advance")
            page += 1
    return mappings


def resolve_payloads(entries, mappings):
    payloads, missing = [], []
    for entry in entries:
        reference = entry["station_ref"]
        identity = (str(reference["data_source"]), str(reference["external_id"]))
        station_id = mappings.get(identity)
        if station_id is None:
            missing.append(identity)
            continue
        payload = dict(entry["payload"])
        payload["station_id"] = station_id
        payloads.append(payload)
    if missing:
        raise ValueError(f"missing {len(missing)} catalog mappings; first: {missing[0]}")
    return payloads


def main(source, base_url, dry_run=False, opener=urlopen):
    key = os.environ.get("INTERNAL_KEY")
    if not key:
        raise RuntimeError("INTERNAL_KEY is required for catalog lookup and prediction writes")
    document = json.loads(source.read_text(encoding="utf-8"))
    if document.get("prediction_endpoint") != PREDICTION_ENDPOINT:
        raise ValueError("unexpected prediction endpoint")
    entries = document["entries"]
    if not entries:
        raise ValueError("prediction batch must not be empty")
    data_sources = {entry["station_ref"]["data_source"] for entry in entries}
    mappings = fetch_catalog_mappings(base_url, key, data_sources, opener)
    payloads = resolve_payloads(entries, mappings)
    prediction_url = api_base(base_url) + document["prediction_endpoint"]
    if dry_run:
        print(f"dry run OK: resolved {len(payloads)} payloads for {prediction_url}")
        return
    headers = {"Content-Type": "application/json; charset=utf-8", "X-Internal-Key": key}
    for payload in payloads:
        request = Request(prediction_url, data=json.dumps(payload).encode(),
                          headers=headers, method="POST")
        request_json(request, opener)
    print(f"posted {len(payloads)} prediction payloads")


if __name__ == "__main__":
    if len(sys.argv) not in (3, 4) or (len(sys.argv) == 4 and sys.argv[3] != "--dry-run"):
        raise SystemExit("usage: post_predictions_api.py BATCH_JSON API_BASE_URL [--dry-run]")
    main(Path(sys.argv[1]), sys.argv[2], len(sys.argv) == 4)
