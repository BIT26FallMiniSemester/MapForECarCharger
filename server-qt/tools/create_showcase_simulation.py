"""Clone a showcase SQLite database and add deterministic historical demo data."""
from __future__ import annotations

import argparse
from contextlib import closing
from datetime import UTC, datetime, timedelta
from pathlib import Path
import random
import sqlite3


def iso(value: datetime) -> str:
    return value.astimezone(UTC).isoformat(timespec="milliseconds").replace("+00:00", "Z")


def create_simulation(source: Path, target: Path, orders: int, users: int, days: int, seed: int) -> dict:
    source, target = source.resolve(), target.resolve()
    if not source.is_file():
        raise FileNotFoundError(f"source database does not exist: {source}")
    if target.exists():
        raise FileExistsError(f"target database already exists: {target}")
    if orders < 1 or users < 1 or days < 2:
        raise ValueError("orders/users must be positive and days must be at least 2")
    target.parent.mkdir(parents=True, exist_ok=True)
    staging = target.with_name(f".{target.name}.staging")
    if staging.exists():
        staging.unlink()

    rng = random.Random(seed)
    try:
        with closing(sqlite3.connect(f"file:{source.as_posix()}?mode=ro", uri=True)) as src, closing(sqlite3.connect(staging)) as db:
            src.backup(db)
            db.execute("PRAGMA foreign_keys=ON")
            station_prices = {row[0]: row[1] for row in db.execute(
                "SELECT id,price_cents_per_kwh FROM stations WHERE status='ACTIVE'")}
            piles = list(db.execute(
                "SELECT p.id,p.station_id FROM charging_piles p JOIN stations s ON s.id=p.station_id "
                "WHERE s.status='ACTIVE' ORDER BY p.id"))
            if not piles or not station_prices:
                raise ValueError("source database has no active stations and piles")

            now = datetime.now(UTC).replace(minute=0, second=0, microsecond=0)
            created_users = []
            for index in range(users):
                phone = f"187{(seed + index) % 100000000:08d}"
                created = now - timedelta(days=rng.randrange(days + 60))
                cursor = db.execute(
                    "INSERT INTO users(phone,nickname,balance_cents,status,created_at,updated_at) VALUES(?,?,?,?,?,?)",
                    (phone, f"演示用户{index + 1:05d}", 200000, "NORMAL", iso(created), iso(created)),
                )
                created_users.append(cursor.lastrowid)

            recharge_rows = []
            for index, user_id in enumerate(created_users):
                created = now - timedelta(days=rng.randrange(days), hours=rng.randrange(24))
                recharge_rows.append((user_id, f"SIM-RECHARGE-{seed}-{index:08d}", 200000, 200000, iso(created)))
            db.executemany(
                "INSERT INTO recharge_records(user_id,client_request_id,amount_cents,balance_after_cents,created_at) VALUES(?,?,?,?,?)",
                recharge_rows,
            )

            order_rows, log_specs = [], []
            for index in range(orders):
                pile_id, station_id = piles[rng.randrange(len(piles))]
                user_id = created_users[rng.randrange(len(created_users))]
                day_offset = rng.randrange(days)
                started = now - timedelta(days=day_offset, hours=rng.randrange(24), minutes=rng.randrange(60))
                duration = rng.randrange(600, 4 * 3600)
                stopped = started + timedelta(seconds=duration)
                paid = stopped + timedelta(seconds=rng.randrange(10, 900))
                rated_factor = rng.uniform(4.0, 55.0)
                energy_wh = max(500, round(rated_factor * duration / 3.6))
                price = station_prices[station_id]
                amount = int(energy_wh * price / 1000 + 0.5)
                created = started - timedelta(minutes=rng.randrange(1, 60))
                order_rows.append((
                    f"SIM-{seed}-{index:09d}", user_id, station_id, pile_id, "COMPLETED", price,
                    iso(created), iso(started), iso(stopped), duration, energy_wh, amount, iso(paid), iso(created), iso(paid),
                ))
                log_specs.append((pile_id, started, stopped))

            db.executemany(
                "INSERT INTO charging_orders(order_no,user_id,station_id,pile_id,status,price_cents_per_kwh,"
                "reserved_at,started_at,stopped_at,duration_seconds,energy_wh,amount_cents,paid_at,created_at,updated_at) "
                "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
                order_rows,
            )
            first_id = db.execute("SELECT min(id) FROM charging_orders WHERE order_no LIKE ?", (f"SIM-{seed}-%",)).fetchone()[0]
            status_rows = []
            for index, (pile_id, started, stopped) in enumerate(log_specs):
                order_id = first_id + index
                status_rows.extend((
                    (pile_id, order_id, "IDLE", "RESERVED", "SIMULATED_RESERVE", iso(started - timedelta(minutes=5))),
                    (pile_id, order_id, "RESERVED", "CHARGING", "SIMULATED_START", iso(started)),
                    (pile_id, order_id, "CHARGING", "IDLE", "SIMULATED_STOP", iso(stopped)),
                ))
            db.executemany(
                "INSERT INTO pile_status_logs(pile_id,order_id,old_status,new_status,reason,created_at) VALUES(?,?,?,?,?,?)",
                status_rows,
            )
            db.commit()
        staging.replace(target)
    except Exception:
        staging.unlink(missing_ok=True)
        raise
    return {"database": str(target), "users_added": users, "orders_added": orders,
            "recharges_added": users, "status_logs_added": orders * 3, "days": days, "seed": seed}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--target", required=True, type=Path)
    parser.add_argument("--orders", type=int, default=50000)
    parser.add_argument("--users", type=int, default=1000)
    parser.add_argument("--days", type=int, default=90)
    parser.add_argument("--seed", type=int, default=20260914)
    args = parser.parse_args()
    try:
        result = create_simulation(args.source, args.target, args.orders, args.users, args.days, args.seed)
    except (OSError, sqlite3.Error, ValueError) as error:
        parser.exit(1, f"Simulation failed: {error}\n")
    for key, value in result.items():
        print(f"{key}={value}")


if __name__ == "__main__":
    main()
