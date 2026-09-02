import argparse
from datetime import timedelta
from decimal import ROUND_HALF_UP, Decimal

from sqlalchemy import delete, select

from app.database import SessionLocal
from app.enums import (
    AdminStatus,
    LogSource,
    OrderStatus,
    PileStatus,
    PileType,
    PredictionType,
    StationStatus,
    UserStatus,
)
from app.helpers import utcnow
from app.models import (
    Admin,
    ChargingOrder,
    ChargingPile,
    LoadPrediction,
    OperationLog,
    PileStatusLog,
    RechargeRecord,
    Station,
    User,
)
from app.security import hash_password
from app.station_import import import_public_stations

TABLES_IN_DELETE_ORDER = [
    LoadPrediction,
    OperationLog,
    PileStatusLog,
    RechargeRecord,
    ChargingOrder,
    ChargingPile,
    Station,
    Admin,
    User,
]


def seed(reset: bool = False) -> None:
    with SessionLocal() as db:
        if reset:
            for model in TABLES_IN_DELETE_ORDER:
                db.execute(delete(model))
            db.commit()
        if db.scalar(select(Admin.id).limit(1)) is not None:
            print("Seed data already exists; use --reset to rebuild it.")
            return
        now = utcnow()
        db.add(
            Admin(
                username="admin",
                password_hash=hash_password("123456"),
                display_name="系统管理员",
                status=AdminStatus.NORMAL,
                created_at=now,
                updated_at=now,
            )
        )
        users = []
        for index in range(1, 7):
            user = User(
                phone=f"1380013800{index}",
                nickname=f"演示用户{index}",
                balance_cents=100_000,
                status=UserStatus.FROZEN if index == 6 else UserStatus.NORMAL,
                created_at=now - timedelta(days=40 - index),
                updated_at=now,
            )
            db.add(user)
            users.append(user)
        station_specs = [
            (
                "中关村充电站",
                "北京市海淀区中关村南大街 5 号",
                Decimal("39.9600000"),
                Decimal("116.3100000"),
                125,
            ),
            (
                "理工大学充电站",
                "北京市海淀区中关村南大街 7 号",
                Decimal("39.9580000"),
                Decimal("116.3150000"),
                138,
            ),
            (
                "西直门充电站",
                "北京市西城区西直门外大街 1 号",
                Decimal("39.9400000"),
                Decimal("116.3500000"),
                115,
            ),
        ]
        stations, piles = [], []
        for station_index, spec in enumerate(station_specs, 1):
            station = Station(
                name=spec[0],
                address=spec[1],
                latitude=spec[2],
                longitude=spec[3],
                price_cents_per_kwh=spec[4],
                status=StationStatus.ACTIVE,
                created_at=now,
                updated_at=now,
            )
            db.add(station)
            db.flush()
            stations.append(station)
            for pile_index in range(1, 5):
                pile_type = PileType.FAST if pile_index <= 2 else PileType.SLOW
                pile_status = PileStatus.IDLE
                if station_index == 1 and pile_index == 3:
                    pile_status = PileStatus.FAULT
                if station_index == 1 and pile_index == 4:
                    pile_status = PileStatus.OFFLINE
                pile = ChargingPile(
                    pile_no=f"BJ-{station_index:02d}-{pile_index:03d}",
                    station_id=station.id,
                    pile_type=pile_type,
                    rated_power_w=60_000 if pile_type == PileType.FAST else 7_000,
                    status=pile_status,
                    version=1,
                    last_heartbeat_at=now
                    if pile_status != PileStatus.OFFLINE
                    else now - timedelta(hours=3),
                    created_at=now,
                    updated_at=now,
                )
                db.add(pile)
                piles.append(pile)
        db.flush()
        for day in range(30):
            for user_index in range(5):
                station = stations[(day + user_index) % len(stations)]
                available = [item for item in piles if item.station_id == station.id]
                pile = available[(day + user_index) % len(available)]
                settled = now - timedelta(days=day, hours=user_index + 1)
                energy = 8_000 + (day * 137 + user_index * 421) % 12_000
                amount = int(
                    (
                        Decimal(energy) * station.price_cents_per_kwh / Decimal(1000)
                    ).quantize(Decimal(1), rounding=ROUND_HALF_UP)
                )
                db.add(
                    ChargingOrder(
                        order_no=f"SEED-CO-{day:02d}-{user_index:02d}",
                        user_id=users[user_index].id,
                        station_id=station.id,
                        pile_id=pile.id,
                        status=OrderStatus.COMPLETED,
                        unit_price_cents_per_kwh=station.price_cents_per_kwh,
                        energy_wh=energy,
                        duration_seconds=1800 + user_index * 300,
                        amount_cents=amount,
                        reserved_at=settled - timedelta(minutes=40),
                        started_at=settled - timedelta(minutes=35),
                        stopped_at=settled - timedelta(minutes=5),
                        settled_at=settled,
                        version=5,
                        created_at=settled - timedelta(minutes=45),
                        updated_at=settled,
                    )
                )
        active_pile = piles[4]
        active_pile.status = PileStatus.CHARGING
        active_pile.version += 1
        db.add(
            ChargingOrder(
                order_no="SEED-CO-ACTIVE",
                user_id=users[0].id,
                station_id=active_pile.station_id,
                pile_id=active_pile.id,
                status=OrderStatus.CHARGING,
                unit_price_cents_per_kwh=stations[1].price_cents_per_kwh,
                energy_wh=3200,
                duration_seconds=0,
                amount_cents=0,
                reserved_at=now - timedelta(minutes=25),
                started_at=now - timedelta(minutes=20),
                version=3,
                created_at=now - timedelta(minutes=30),
                updated_at=now - timedelta(minutes=1),
            )
        )
        db.add_all(
            [
                PileStatusLog(
                    pile_id=piles[2].id,
                    order_id=None,
                    old_status=PileStatus.IDLE,
                    new_status=PileStatus.FAULT,
                    source=LogSource.SIMULATOR,
                    reason="simulated fault",
                    created_at=now - timedelta(minutes=15),
                ),
                PileStatusLog(
                    pile_id=piles[3].id,
                    order_id=None,
                    old_status=PileStatus.IDLE,
                    new_status=PileStatus.OFFLINE,
                    source=LogSource.SIMULATOR,
                    reason="simulated offline",
                    created_at=now - timedelta(minutes=10),
                ),
                PileStatusLog(
                    pile_id=piles[0].id,
                    order_id=None,
                    old_status=PileStatus.FAULT,
                    new_status=PileStatus.IDLE,
                    source=LogSource.SIMULATOR,
                    reason="simulated recovery",
                    created_at=now - timedelta(minutes=5),
                ),
            ]
        )
        for index, user in enumerate(users[:5]):
            db.add(
                RechargeRecord(
                    recharge_no=f"SEED-RC-{index:03d}",
                    client_request_id=f"seed-request-{index:03d}",
                    user_id=user.id,
                    amount_cents=100_000,
                    balance_before_cents=0,
                    balance_after_cents=100_000,
                    channel="SIMULATED",
                    status="SUCCESS",
                    created_at=now - timedelta(days=35),
                )
            )
        for hour in range(1, 7):
            for prediction_type, value in (
                (PredictionType.LOAD_W, Decimal(120000 + hour * 8000)),
                (PredictionType.AVAILABLE_PILES, Decimal(max(0, 4 - hour // 2))),
                (
                    PredictionType.CONGESTION_SCORE,
                    Decimal("0.35") + Decimal(hour) / Decimal(20),
                ),
            ):
                db.add(
                    LoadPrediction(
                        station_id=stations[0].id,
                        prediction_type=prediction_type,
                        horizon_hours=6,
                        predicted_for=now + timedelta(hours=hour),
                        predicted_value=value,
                        model_version="baseline-v1",
                        generated_at=now,
                    )
                )
        import_result = import_public_stations(db, commit=False)
        db.commit()
        print(
            "Seed data created: admin, users, stations, piles, 30-day orders, "
            f"predictions and {import_result.total_rows} public station records."
        )


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--reset", action="store_true")
    arguments = parser.parse_args()
    seed(arguments.reset)
