"""Generate reproducible clean and deliberately dirty charging-platform data."""
from __future__ import annotations

import argparse
import copy
import csv
from datetime import date, datetime, time, timedelta, timezone
import hashlib
import json
from pathlib import Path
import random
import tempfile
import sys

import yaml

UTC = timezone.utc
TABLES = (
    "users", "stations", "charging_piles", "charging_orders",
    "recharge_records", "pile_status_logs",
)

COLUMNS = {
    "users": ["row_id", "id", "phone", "nickname", "avatar_id", "balance_cents", "status", "created_at", "updated_at"],
    "stations": ["row_id", "id", "name", "address", "latitude", "longitude", "price_cents_per_kwh", "operator_name", "district", "data_source", "external_id", "status", "service_type", "region_scope", "location_type", "fast_connector_count", "slow_connector_count", "created_at", "updated_at"],
    "charging_piles": ["row_id", "id", "station_id", "pile_no", "charge_type", "rated_power_w", "status", "reserved_order_id", "created_at", "updated_at"],
    "charging_orders": ["row_id", "id", "order_no", "user_id", "station_id", "pile_id", "status", "price_cents_per_kwh", "reserved_at", "expires_at", "started_at", "stopped_at", "duration_seconds", "energy_wh", "amount_cents", "paid_at", "cancelled_at", "created_at", "updated_at"],
    "recharge_records": ["row_id", "id", "user_id", "client_request_id", "amount_cents", "balance_after_cents", "created_at"],
    "pile_status_logs": ["row_id", "id", "pile_id", "order_id", "old_status", "new_status", "reason", "created_at"],
}


def iso(value: datetime | None) -> str:
    return "" if value is None else value.astimezone(UTC).isoformat().replace("+00:00", "Z")


def parse_config(path: Path, profile_name: str | None) -> dict:
    config = yaml.safe_load(path.read_text(encoding="utf-8"))
    selected = profile_name or config["defaults"]["profile"]
    if selected not in config["profiles"]:
        raise ValueError(f"unknown profile: {selected}")
    result = {**config["defaults"], **config["profiles"][selected], "profile": selected}
    source = Path(config["source"]["station_catalog"])
    result["station_catalog"] = source if source.is_absolute() else (path.parent / source).resolve()
    return result


def load_rules(path: Path) -> dict:
    data = yaml.safe_load(path.read_text(encoding="utf-8"))
    return {key: value for key, value in data["rules"].items() if data["defaults"].get("enabled", True) and value.get("enabled", True)}


def row_id(table: str, identifier: int) -> str:
    return f"{table}:{identifier}"


def build_clean(config: dict) -> dict[str, list[dict]]:
    rng = random.Random(config["seed"])
    end = date.fromisoformat(str(config["end_date"]))
    start = end - timedelta(days=config["days"] - 1)
    catalog = json.loads(config["station_catalog"].read_text(encoding="utf-8"))["stations"]
    if config["stations"] > len(catalog):
        raise ValueError("station profile exceeds source catalog")
    chosen = catalog[: config["stations"]]
    base_time = datetime.combine(start, time.min, UTC)

    stations = []
    for index, source in enumerate(chosen, 1):
        stations.append({
            "row_id": row_id("stations", index), "id": index,
            "name": source["name"], "address": source["address"],
            "latitude": source["latitude"], "longitude": source["longitude"],
            "price_cents_per_kwh": source.get("price_cents_per_kwh") or config["price_cents_per_kwh"],
            "operator_name": source.get("operator_name") or "UNKNOWN",
            "district": source.get("district") or "未知区域",
            "data_source": source.get("data_source") or "SIMULATION",
            "external_id": source.get("external_id") or str(index), "status": "ACTIVE",
            "service_type": source.get("service_type") or "社会公用",
            "region_scope": source.get("region_scope") or "未知",
            "location_type": source.get("location_type") or "其他",
            "fast_connector_count": max(0, int(source.get("fast_connector_count") or 0)),
            "slow_connector_count": max(0, int(source.get("slow_connector_count") or 0)),
            "created_at": iso(base_time), "updated_at": iso(base_time),
        })

    users = []
    for identifier in range(1, config["users"] + 1):
        created = base_time + timedelta(seconds=rng.randrange(config["days"] * 86400))
        users.append({
            "row_id": row_id("users", identifier), "id": identifier,
            "phone": str(13_000_000_000 + identifier),
            "nickname": f"模拟用户{identifier}", "avatar_id": "",
            "balance_cents": rng.randrange(0, 100001),
            "status": "FROZEN" if rng.random() < 0.02 else "NORMAL",
            "created_at": iso(created), "updated_at": iso(created),
        })

    piles = []
    pile_identifier = 0
    for station in stations:
        source = chosen[station["id"] - 1]
        specifications = [
            ("FAST", max(0, int(source.get("fast_connector_count") or 0)), 60000),
            ("SLOW", max(0, int(source.get("slow_connector_count") or 0)), 7000),
        ]
        if sum(item[1] for item in specifications) == 0:
            specifications = [("SLOW", 1, 7000)]
        for charge_type, count, power in specifications:
            for sequence in range(1, count + 1):
                pile_identifier += 1
                piles.append({
                    "row_id": row_id("charging_piles", pile_identifier), "id": pile_identifier,
                    "station_id": station["id"],
                    "pile_no": f"SIM-{station['id']:04d}-{charge_type[0]}{sequence:03d}",
                    "charge_type": charge_type, "rated_power_w": power,
                    "status": "IDLE", "reserved_order_id": "",
                    "created_at": iso(base_time), "updated_at": iso(base_time),
                })

    if not piles:
        raise ValueError("no charging piles generated")
    pile_by_id = {item["id"]: item for item in piles}
    order_statuses = ["COMPLETED"] * 72 + ["CANCELLED"] * 10 + ["UNPAID"] * 6 + ["CHARGING"] * 5 + ["RESERVED"] * 4 + ["PENDING"] * 3
    orders = []
    active_users: set[int] = set()
    active_piles: set[int] = set()
    for identifier in range(1, config["orders"] + 1):
        # Weighted hour model creates morning/evening peaks without external ML.
        day = start + timedelta(days=rng.randrange(config["days"]))
        hour = rng.choice([7, 8, 9, 17, 18, 19, 20]) if rng.random() < 0.58 else rng.randrange(24)
        created = datetime.combine(day, time(hour, rng.randrange(60), rng.randrange(60)), UTC)
        pile = pile_by_id[rng.choice(piles)["id"]]
        station = stations[pile["station_id"] - 1]
        status = rng.choice(order_statuses)
        user = rng.choice(users)
        if status in {"PENDING", "RESERVED", "CHARGING", "UNPAID"} and user["id"] in active_users:
            status = "COMPLETED"
        if status in {"RESERVED", "CHARGING"} and pile["id"] in active_piles:
            status = "COMPLETED"
        if status in {"PENDING", "RESERVED", "CHARGING", "UNPAID"}:
            day = end
            created = datetime.combine(day, time(hour, rng.randrange(60), rng.randrange(60)), UTC)
            active_users.add(user["id"])
        if status in {"RESERVED", "CHARGING"}:
            active_piles.add(pile["id"])
        reserved = created + timedelta(minutes=rng.randrange(1, 11)) if status != "PENDING" else None
        started = reserved + timedelta(minutes=rng.randrange(1, 21)) if status in {"CHARGING", "UNPAID", "COMPLETED"} else None
        duration = rng.randrange(600, 10801) if started else None
        stopped = started + timedelta(seconds=duration) if status in {"UNPAID", "COMPLETED"} else None
        energy = round(pile["rated_power_w"] * duration / 3600) if duration else None
        amount = round(energy * station["price_cents_per_kwh"] / 1000) if energy is not None else None
        paid = stopped + timedelta(minutes=rng.randrange(1, 31)) if status == "COMPLETED" else None
        cancelled = reserved + timedelta(minutes=rng.randrange(1, 31)) if status == "CANCELLED" else None
        updated = paid or stopped or started or cancelled or reserved or created
        orders.append({
            "row_id": row_id("charging_orders", identifier), "id": identifier,
            "order_no": f"SO{day:%Y%m%d}{identifier:010d}", "user_id": user["id"],
            "station_id": pile["station_id"], "pile_id": pile["id"], "status": status,
            "price_cents_per_kwh": station["price_cents_per_kwh"],
            "reserved_at": iso(reserved), "expires_at": iso(reserved + timedelta(minutes=30) if reserved and status == "RESERVED" else None),
            "started_at": iso(started), "stopped_at": iso(stopped),
            "duration_seconds": "" if duration is None else duration,
            "energy_wh": "" if energy is None else energy,
            "amount_cents": "" if amount is None else amount,
            "paid_at": iso(paid), "cancelled_at": iso(cancelled),
            "created_at": iso(created), "updated_at": iso(updated),
        })
        if status in {"RESERVED", "CHARGING"}:
            pile["status"] = status
            pile["reserved_order_id"] = identifier if status == "RESERVED" else ""
            pile["updated_at"] = iso(updated)

    recharges = []
    balances: dict[int, int] = {}
    for identifier in range(1, config["recharge_records"] + 1):
        user = rng.choice(users)
        amount = rng.choice([1000, 2000, 5000, 10000, 20000])
        balances[user["id"]] = balances.get(user["id"], 0) + amount
        created = base_time + timedelta(seconds=rng.randrange(config["days"] * 86400))
        recharges.append({
            "row_id": row_id("recharge_records", identifier), "id": identifier,
            "user_id": user["id"], "client_request_id": f"SIM-R-{identifier:012d}",
            "amount_cents": amount, "balance_after_cents": balances[user["id"]],
            "created_at": iso(created),
        })

    status_logs = []
    transitions = [("IDLE", "RESERVED"), ("RESERVED", "CHARGING"), ("CHARGING", "IDLE"), ("IDLE", "FAULT"), ("FAULT", "IDLE"), ("IDLE", "OFFLINE"), ("OFFLINE", "IDLE")]
    for identifier in range(1, config["pile_status_logs"] + 1):
        pile = rng.choice(piles)
        old, new = rng.choice(transitions)
        created = base_time + timedelta(seconds=rng.randrange(config["days"] * 86400))
        status_logs.append({
            "row_id": row_id("pile_status_logs", identifier), "id": identifier,
            "pile_id": pile["id"], "order_id": "", "old_status": old,
            "new_status": new, "reason": "SIMULATION", "created_at": iso(created),
        })
    return {"users": users, "stations": stations, "charging_piles": piles,
            "charging_orders": orders, "recharge_records": recharges,
            "pile_status_logs": status_logs}


def issue_count(rows: list[dict], ratio: float) -> int:
    return min(len(rows), max(1, round(len(rows) * ratio))) if rows else 0


def choose_rows(rng: random.Random, rows: list[dict], count: int, predicate=lambda row: True) -> list[dict]:
    candidates = [row for row in rows if predicate(row)]
    if not candidates:
        raise ValueError("quality rule has no eligible row")
    return rng.sample(candidates, min(count, len(candidates)))


def inject_issues(clean: dict[str, list[dict]], rules: dict, seed: int) -> tuple[dict[str, list[dict]], list[dict]]:
    dirty = copy.deepcopy(clean)
    rng = random.Random(seed + 1)
    manifest: list[dict] = []

    def record(rule_id: str, table: str, row: dict, fields: list[str], before: dict):
        manifest.append({"rule_id": rule_id, "table": table, "row_id": row["row_id"],
                         "fields": fields, "before": before,
                         "after": {field: row.get(field) for field in fields}})

    def mutate(rule_id: str, table: str, fields: list[str], action, predicate=lambda row: True):
        count = issue_count(dirty[table], float(rules[rule_id]["ratio"]))
        for row in choose_rows(rng, dirty[table], count, predicate):
            before = {field: row.get(field) for field in fields}
            action(row)
            record(rule_id, table, row, fields, before)

    mutate("DQ001", "users", ["phone"], lambda row: row.update(phone="12345"))
    duplicate_users = choose_rows(rng, dirty["users"], issue_count(dirty["users"], rules["DQ002"]["ratio"]))
    for source in duplicate_users:
        clone = copy.deepcopy(source)
        clone["row_id"] += ":duplicate"
        clone["updated_at"] = "2026-09-14T23:59:59Z"
        dirty["users"].append(clone)
        record("DQ002", "users", clone, ["id", "phone", "updated_at"], {})
    mutate("DQ003", "stations", ["latitude", "longitude"], lambda row: row.update(latitude=181.0, longitude=95.0))
    mutate("DQ004", "stations", ["district", "operator_name"], lambda row: row.update(district="  " + row["district"] + "  ", operator_name=""))
    mutate("DQ005", "charging_piles", ["station_id"], lambda row: row.update(station_id=999999999))
    mutate("DQ006", "charging_piles", ["charge_type", "status"], lambda row: row.update(charge_type="rapid", status="unknown"))
    order_rows = dirty["charging_orders"]
    duplicate_order_no = order_rows[0]["order_no"]
    mutate("DQ007", "charging_orders", ["order_no"], lambda row: row.update(order_no=duplicate_order_no), lambda row: row is not order_rows[0])
    mutate("DQ008", "charging_orders", ["user_id", "station_id"], lambda row: row.update(user_id=999999999, station_id=999999999))
    mutate("DQ009", "charging_orders", ["started_at", "stopped_at"], lambda row: row.update(started_at="2026-09-14T12:00:00Z", stopped_at="2026-09-14T11:00:00Z"))
    mutate("DQ010", "charging_orders", ["duration_seconds", "energy_wh", "amount_cents"], lambda row: row.update(duration_seconds=-1, energy_wh=-100, amount_cents=-15))
    mutate("DQ011", "charging_orders", ["status", "paid_at", "amount_cents", "energy_wh"], lambda row: row.update(status="COMPLETED", paid_at="", amount_cents="", energy_wh=""))
    mutate("DQ012", "charging_orders", ["amount_cents"], lambda row: row.update(amount_cents=int(row["amount_cents"]) + 9999), lambda row: row["energy_wh"] != "" and row["amount_cents"] != "")
    mutate("DQ013", "charging_orders", ["status", "started_at", "stopped_at", "paid_at"], lambda row: row.update(status="PENDING", started_at="2026-09-14T10:00:00Z", stopped_at="2026-09-14T11:00:00Z", paid_at="2026-09-14T11:05:00Z"))
    mutate("DQ014", "recharge_records", ["user_id", "amount_cents"], lambda row: row.update(user_id=999999999, amount_cents=0))
    mutate("DQ015", "pile_status_logs", ["old_status", "new_status"], lambda row: row.update(old_status="BROKEN", new_status="RUNNING"))
    for table in TABLES:
        rows = dirty[table]
        count = issue_count(rows, float(rules["DQ016"]["ratio"]))
        text_field = "status" if "status" in COLUMNS[table] else ("old_status" if table == "pile_status_logs" else "created_at")
        for row in choose_rows(rng, rows, count):
            before = {text_field: row.get(text_field)}
            row[text_field] = "  " + str(row.get(text_field, "")).lower() + "  "
            record("DQ016", table, row, [text_field], before)
    completed = choose_rows(rng, order_rows, 2, lambda row: bool(row["started_at"]) and bool(row["stopped_at"]))
    anchor, overlap = completed
    before = {key: overlap[key] for key in ("pile_id", "station_id", "started_at", "stopped_at")}
    overlap.update(pile_id=anchor["pile_id"], station_id=anchor["station_id"],
                   started_at=anchor["started_at"], stopped_at=anchor["stopped_at"])
    record("DQ017", "charging_orders", overlap, list(before), before)
    return dirty, manifest


def write_csv(path: Path, table: str, rows: list[dict]):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=COLUMNS[table], extrasaction="raise")
        writer.writeheader()
        writer.writerows(rows)


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def csv_row_count(path: Path) -> int:
    with path.open(encoding="utf-8", newline="") as stream:
        return max(0, sum(1 for _ in stream) - 1)


def generate(config_path: Path, rules_path: Path, output: Path, profile: str | None) -> Path:
    config = parse_config(config_path, profile)
    rules = load_rules(rules_path)
    if set(rules) != {f"DQ{number:03d}" for number in range(1, 18)}:
        raise ValueError("quality rule set must contain DQ001-DQ017")
    batch_id = f"sim-{config['profile']}-{config['end_date']}-seed{config['seed']}"
    output = output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    target = output / batch_id
    if target.exists():
        raise FileExistsError(f"batch already exists: {target}")
    with tempfile.TemporaryDirectory(prefix=".generate-", dir=output) as temporary:
        staging = Path(temporary) / batch_id
        clean = build_clean(config)
        dirty, manifest = inject_issues(clean, rules, config["seed"])
        for variant, datasets in (("clean", clean), ("dirty", dirty)):
            for table, rows in datasets.items():
                write_csv(staging / variant / f"{table}.csv", table, rows)
        manifest_path = staging / "_injected_issues.jsonl"
        with manifest_path.open("x", encoding="utf-8", newline="\n") as stream:
            for issue in manifest:
                stream.write(json.dumps(issue, ensure_ascii=False, separators=(",", ":")) + "\n")
        files = {}
        for path in sorted(staging.rglob("*.csv")):
            files[str(path.relative_to(staging)).replace("\\", "/")] = {
                "rows": csv_row_count(path), "sha256": file_sha256(path)}
        metadata = {
            "schema_version": 1, "batch_id": batch_id, "profile": config["profile"],
            "seed": config["seed"], "start_date": str(date.fromisoformat(str(config["end_date"])) - timedelta(days=config["days"] - 1)),
            "end_date": str(config["end_date"]), "generated_at": datetime.now(UTC).isoformat(),
            "issue_manifest": "_injected_issues.jsonl", "issue_count": len(manifest),
            "rules": sorted({item["rule_id"] for item in manifest}), "files": files,
        }
        (staging / "metadata.json").write_text(
            json.dumps(metadata, ensure_ascii=False, indent=2), encoding="utf-8")
        staging.rename(target)
    return target


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, default=root / "conf/generator.yaml")
    parser.add_argument("--rules", type=Path, default=root / "conf/quality_rules.yaml")
    parser.add_argument("--profile", choices=("quick", "full"))
    parser.add_argument("--output", type=Path, default=root / "runtime/generated")
    args = parser.parse_args()
    try:
        print(generate(args.config.resolve(), args.rules.resolve(), args.output, args.profile))
    except (OSError, ValueError, KeyError, yaml.YAMLError) as error:
        sys.exit(f"Generation failed: {error}")


if __name__ == "__main__":
    main()
