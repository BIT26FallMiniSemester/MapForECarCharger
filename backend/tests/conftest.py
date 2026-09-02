from datetime import UTC, datetime

import pytest
from app.database import Base, create_database_engine, get_db
from app.enums import AdminStatus, PileStatus, PileType, StationStatus, UserStatus
from app.main import app
from app.models import Admin, ChargingPile, Station, User
from app.security import hash_password
from fastapi.testclient import TestClient
from sqlalchemy.orm import sessionmaker


@pytest.fixture()
def db_session(tmp_path):
    engine = create_database_engine(f"sqlite:///{tmp_path / 'test.db'}")
    Base.metadata.create_all(engine)
    testing_session = sessionmaker(bind=engine, autoflush=False, expire_on_commit=False)
    session = testing_session()
    now = datetime.now(UTC).replace(tzinfo=None)
    user = User(
        phone="13900000001",
        nickname="测试用户",
        balance_cents=50_000,
        status=UserStatus.NORMAL,
        created_at=now,
        updated_at=now,
    )
    frozen = User(
        phone="13900000002",
        nickname="冻结用户",
        balance_cents=0,
        status=UserStatus.FROZEN,
        created_at=now,
        updated_at=now,
    )
    admin = Admin(
        username="admin",
        password_hash=hash_password("123456"),
        display_name="管理员",
        status=AdminStatus.NORMAL,
        created_at=now,
        updated_at=now,
    )
    station = Station(
        name="测试站",
        address="测试地址",
        latitude=39.96,
        longitude=116.31,
        price_cents_per_kwh=125,
        status=StationStatus.ACTIVE,
        created_at=now,
        updated_at=now,
    )
    session.add_all([user, frozen, admin, station])
    session.flush()
    session.add_all(
        [
            ChargingPile(
                pile_no="TEST-001",
                station_id=station.id,
                pile_type=PileType.FAST,
                rated_power_w=60_000,
                status=PileStatus.IDLE,
                version=1,
                created_at=now,
                updated_at=now,
            ),
            ChargingPile(
                pile_no="TEST-002",
                station_id=station.id,
                pile_type=PileType.SLOW,
                rated_power_w=7_000,
                status=PileStatus.IDLE,
                version=1,
                created_at=now,
                updated_at=now,
            ),
        ]
    )
    session.commit()

    def override_db():
        db = testing_session()
        try:
            yield db
        finally:
            db.close()

    app.dependency_overrides[get_db] = override_db
    yield session
    app.dependency_overrides.clear()
    session.close()
    engine.dispose()


@pytest.fixture()
def client(db_session):
    with TestClient(app, raise_server_exceptions=False) as test_client:
        yield test_client


@pytest.fixture()
def user_headers(client):
    response = client.post("/api/v1/user/login", json={"phone": "13900000001"})
    assert response.status_code == 200
    return {"Authorization": f"Bearer {response.json()['data']['access_token']}"}


@pytest.fixture()
def admin_headers(client):
    response = client.post(
        "/api/v1/admin/login", json={"username": "admin", "password": "123456"}
    )
    assert response.status_code == 200
    return {"Authorization": f"Bearer {response.json()['data']['access_token']}"}
