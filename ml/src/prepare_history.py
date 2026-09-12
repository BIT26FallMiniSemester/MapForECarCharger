"""Anonymized order/device CSV -> reproducible hourly training data (stdlib only)."""
import argparse
import csv
import json
import math
import random
from collections import Counter, defaultdict
from datetime import datetime, timezone, timedelta
from pathlib import Path

LOCAL = timezone(timedelta(hours=8))


def epoch(value):
    value = str(value)
    if value.isdigit():
        return int(value)
    parsed = datetime.fromisoformat(value.replace("Z", "+00:00"))
    if parsed.tzinfo is None:
        raise ValueError("timestamp requires timezone")
    return int(parsed.timestamp())


def save_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2, allow_nan=False) + "\n", encoding="utf-8")


def write_csv(path, fields, rows):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", newline="", encoding="utf-8") as out:
        writer = csv.DictWriter(out, fields)
        writer.writeheader()
        writer.writerows(rows)


def catalog_index(catalog):
    stations = {int(s["id"]): s for s in catalog["stations"]}
    piles = {int(p["id"]): p for p in catalog["charging_piles"]}
    if len(stations) != len(catalog["stations"]) or len(piles) != len(catalog["charging_piles"]):
        raise ValueError("duplicate catalog ID")
    for sid in stations:
        if sid <= 0:
            raise ValueError("invalid station ID")
    for pid, pile in piles.items():
        power = float(pile["rated_power_w"])
        if pid <= 0 or int(pile["station_id"]) not in stations or not math.isfinite(power) or power <= 0:
            raise ValueError("invalid pile or power")
    return stations, piles


def generate(output, days=45, seed=42, end=1788220800):
    if days < 15 or end % 3600:
        raise ValueError("days >= 15 and an hourly cutoff are required")
    rng = random.Random(seed)
    stations = [{"id": s, "name": f"Synthetic station {s}", "latitude": 39.9 + s * .01,
                 "longitude": 116.4 + s * .01, "data_source": "ML_SYNTHETIC",
                 "external_id": str(s)} for s in range(1, 4)]
    piles = [{"id": s * 10 + p, "station_id": s, "rated_power_w": 7000}
             for s in range(1, 4) for p in range(4)]
    start = end - days * 86400
    orders, logs = [], []
    for stamp in range(start, end, 3600):
        local = datetime.fromtimestamp(stamp, LOCAL)
        probability = .15 + .6 * math.exp(-((local.hour - 18) / 4) ** 2)
        for pile in piles:
            status = rng.choices(["IDLE", "FAULT", "OFFLINE"], [.96, .02, .02])[0]
            if status == "IDLE" and rng.random() < probability:
                status = "CHARGING"
                duration = rng.randint(1800, 3300)
                begin = stamp + rng.randint(0, 200)
                orders.append({"id": len(orders) + 1, "station_id": pile["station_id"],
                               "pile_id": pile["id"], "started_at": begin,
                               "stopped_at": begin + duration, "duration_seconds": duration,
                               "energy_wh": round(7000 * duration / 3600 * rng.uniform(.5, .95))})
            logs.append({"pile_id": pile["id"], "reported_at": stamp, "status": status})
    save_json(output / "catalog.json", {"metadata": {"simulated": True, "seed": seed,
              "start_epoch": start, "end_epoch": end}, "stations": stations, "charging_piles": piles})
    write_csv(output / "orders.csv", ["id", "station_id", "pile_id", "started_at", "stopped_at",
                                      "duration_seconds", "energy_wh"], orders)
    write_csv(output / "devices.csv", ["pile_id", "reported_at", "status"], logs)
    # Deliberate synthetic holiday, NOT an official Chinese holiday calendar.
    save_json(output / "holidays.json", [datetime.fromtimestamp(start + 10 * 86400, LOCAL).date().isoformat()])


def clean(catalog, orders_path, devices_path, output, start, end, holidays=()):
    # ponytail: in-memory aggregation; partition by month if multi-year exports exceed RAM.
    if not isinstance(holidays, (list, tuple, set)):
        raise ValueError("holiday calendar must be a list of local dates")
    for day in holidays:
        if not isinstance(day, str) or datetime.strptime(day, "%Y-%m-%d").date().isoformat() != day:
            raise ValueError("invalid holiday date")
    stations, piles = catalog_index(catalog)
    if start % 3600 or end % 3600 or not start < end or end - start > 3660 * 86400:
        raise ValueError("invalid hourly [start,end) interval")
    counts, rejected = Counter(), []
    orders, seen = [], {}
    with orders_path.open(encoding="utf-8-sig", newline="") as source:
        for line, row in enumerate(csv.DictReader(source), 2):
            try:
                identity = str(row["id"]).strip()
                if not identity:
                    raise ValueError("missing order ID")
                sid, pid = int(row["station_id"]), int(row["pile_id"])
                begin, finish = epoch(row["started_at"]), epoch(row["stopped_at"])
                energy = float(row["energy_wh"])
                if (pid not in piles or sid != int(piles[pid]["station_id"]) or
                    finish <= begin or finish - begin > 7 * 86400 or not math.isfinite(energy) or energy < 0 or
                    energy > float(piles[pid]["rated_power_w"]) * (finish - begin) / 3600 + 1):
                    raise ValueError("invalid order interval, identity or energy")
                value = (sid, pid, begin, finish, energy)
                if identity in seen:
                    if seen[identity] != value:
                        raise ValueError("conflicting duplicate order")
                    counts["duplicate_orders"] += 1
                    continue
                seen[identity] = value
                orders.append(value)
            except (KeyError, ValueError, TypeError) as exc:
                counts["rejected_orders"] += 1
                rejected.append({"file": "orders", "line": line, "reason": str(exc)})
    energy_hours, occupied_hours = defaultdict(float), defaultdict(float)
    last_end = {}
    for sid, pid, begin, finish, energy in sorted(orders, key=lambda r: (r[1], r[2])):
        if begin < last_end.get(pid, begin):
            counts["overlapping_orders"] += 1
            continue
        last_end[pid] = finish
        counts["accepted_orders"] += 1
        for stamp in range(max(start, begin // 3600 * 3600), min(end, finish), 3600):
            overlap = max(0, min(finish, stamp + 3600) - max(begin, stamp))
            energy_hours[sid, stamp] += energy * overlap / (finish - begin) / 1000
            occupied_hours[sid, stamp] += overlap / 3600
    events = {}
    with devices_path.open(encoding="utf-8-sig", newline="") as source:
        for line, row in enumerate(csv.DictReader(source), 2):
            try:
                pid, stamp, status = int(row["pile_id"]), epoch(row["reported_at"]), row["status"]
                if pid not in piles or status not in {"IDLE", "CHARGING", "FAULT", "OFFLINE"}:
                    raise ValueError("invalid device event")
                key = (pid, stamp)
                if key in events:
                    if events[key] != status:
                        raise ValueError("conflicting duplicate device event")
                    counts["duplicate_events"] += 1
                    continue
                events[key] = status
            except (KeyError, ValueError, TypeError) as exc:
                counts["rejected_events"] += 1
                rejected.append({"file": "devices", "line": line, "reason": str(exc)})
    by_pile = defaultdict(list)
    for (pid, stamp), status in events.items():
        by_pile[pid].append((stamp, status))
    unavailable, faults, unknown = Counter(), Counter(), Counter()
    for pid, pile in piles.items():
        sequence, index = sorted(by_pile[pid]), -1
        for stamp in range(start, end, 3600):
            while index + 1 < len(sequence) and sequence[index + 1][0] <= stamp:
                index += 1
            status = sequence[index][1] if index >= 0 and stamp - sequence[index][0] <= 7200 else "UNKNOWN"
            key = int(pile["station_id"]), stamp
            unavailable[key] += status in {"FAULT", "OFFLINE", "UNKNOWN"}
            faults[key] += status == "FAULT"
            unknown[key] += status == "UNKNOWN"
    rows = []
    for sid in sorted(stations):
        station_piles = [p for p in piles.values() if int(p["station_id"]) == sid]
        if not station_piles:
            counts["stations_without_piles"] += 1
            continue
        total, capacity = len(station_piles), sum(float(p["rated_power_w"]) for p in station_piles) / 1000
        for stamp in range(start, end, 3600):
            key = sid, stamp
            load = energy_hours[key]
            if load > capacity + 1e-6:
                raise ValueError("aggregated load exceeds station capacity")
            rows.append({"timestamp_epoch": stamp, "station_id": sid, "total_piles": total,
                         "capacity_kw": capacity, "load_kw": round(min(load, capacity), 6),
                         "occupied_piles": round(min(total, occupied_hours[key]), 6),
                         "is_holiday": int(datetime.fromtimestamp(stamp, LOCAL).date().isoformat() in holidays),
                         "unavailable_piles": unavailable[key], "fault_piles": faults[key],
                         "unknown_piles": unknown[key]})
    if not rows:
        raise ValueError("no usable stations")
    write_csv(output, list(rows[0]), rows)
    report = {"counts": dict(counts), "rows": len(rows), "rejected": rejected,
              "simulated": bool(catalog.get("metadata", {}).get("simulated", False)),
              "calendar_supplied": bool(holidays),
              "assumptions": ["Complete order export required; no accepted orders in an hour means zero load.",
                              "Order energy spread uniformly over duration; occupancy is hourly average.",
                              "Device state carried forward at most 2h; unknown devices treated unavailable."]}
    save_json(output.with_suffix(".quality.json"), report)
    return rows, report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    gen = commands.add_parser("generate")
    gen.add_argument("output", type=Path)
    gen.add_argument("--days", type=int, default=45)
    gen.add_argument("--seed", type=int, default=42)
    gen.add_argument("--end", type=epoch, default=1788220800)
    prep = commands.add_parser("clean")
    for name in ("catalog", "orders", "devices", "output"):
        prep.add_argument(name, type=Path)
    prep.add_argument("--start", type=epoch, required=True)
    prep.add_argument("--end", type=epoch, required=True)
    prep.add_argument("--holidays", type=Path)
    args = parser.parse_args()
    if args.command == "generate":
        generate(args.output, args.days, args.seed, args.end)
    else:
        clean(json.loads(args.catalog.read_text(encoding="utf-8-sig")), args.orders, args.devices,
              args.output, args.start, args.end,
              json.loads(args.holidays.read_text()) if args.holidays else [])
