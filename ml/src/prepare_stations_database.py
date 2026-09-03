#!/usr/bin/env python3
"""Build deterministic hourly training data from stations_database.json."""
import csv
import json
import math
import random
import sys
from datetime import datetime, timedelta, timezone
from pathlib import Path


def parse_time(value):
    return datetime.fromisoformat(value.replace("Z", "+00:00")).astimezone(timezone.utc)


def main(source, output, days=28):
    data = json.loads(source.read_text(encoding="utf-8-sig"))
    pile_power = {}
    for pile in data["charging_piles"]:
        pile_power.setdefault(int(pile["station_id"]), []).append(float(pile["rated_power_w"]) / 1000.0)

    end = parse_time(data["metadata"]["generated_at"]).replace(minute=0, second=0, microsecond=0)
    start = end - timedelta(hours=days * 24 - 1)
    rng = random.Random(20260901)

    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("w", newline="") as file:
        writer = csv.writer(file, lineterminator="\n")
        writer.writerow(["timestamp_epoch", "station_id", "total_piles", "capacity_kw", "load_kw"])
        for station in data["stations"]:
            station_id = int(station["id"])
            powers = pile_power.get(station_id, [])
            total_piles = len(powers) or int(station["fast_connector_count"]) + int(station["slow_connector_count"])
            if total_piles <= 0:
                continue
            capacity_kw = sum(powers) if powers else total_piles * 7.0
            profile = 0.75 + (station_id % 17) / 50.0
            phase = (station_id % 5) - 2
            for hour_index in range(days * 24):
                now = start + timedelta(hours=hour_index)
                hour = now.hour
                weekend = now.weekday() >= 5
                morning = 0.18 * math.exp(-((hour - 8 - phase) / 3.0) ** 2)
                evening = 0.32 * math.exp(-((hour - 18 - phase) / 3.6) ** 2)
                base = 0.08 + morning + evening
                utilization = min(0.92, max(0.01, base * profile * (0.82 if weekend else 1.0) + rng.gauss(0.0, 0.025)))
                writer.writerow([int(now.timestamp()), station_id, total_piles,
                                 round(capacity_kw, 3), round(capacity_kw * utilization, 3)])


if __name__ == "__main__":
    if len(sys.argv) not in (3, 4):
        raise SystemExit("usage: prepare_stations_database.py STATIONS_JSON OUTPUT_CSV [days]")
    main(Path(sys.argv[1]), Path(sys.argv[2]), int(sys.argv[3]) if len(sys.argv) == 4 else 28)
