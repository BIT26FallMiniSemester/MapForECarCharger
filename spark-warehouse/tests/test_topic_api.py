"""Topic aggregates and bounded device pagination against the actual Qt schema."""
import importlib.util
import json
from pathlib import Path
import sqlite3
import tempfile
import unittest
from datetime import datetime, timedelta, timezone

ROOT = Path(__file__).resolve().parents[2]


class TopicApiTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.database = Path(self.temp.name) / "business.db"
        now = datetime.now(timezone.utc).isoformat(timespec="milliseconds").replace("+00:00", "Z")
        earlier = (datetime.now(timezone.utc) - timedelta(minutes=20)).isoformat(timespec="milliseconds").replace("+00:00", "Z")
        with sqlite3.connect(self.database) as db:
            db.executescript((ROOT / "server-qt/migrations/001_initial.sql").read_text(encoding="utf-8"))
            db.execute("CREATE TABLE schema_migrations(version INTEGER PRIMARY KEY,applied_at TEXT)")
            db.execute("INSERT INTO schema_migrations VALUES(1,?)", (now,))
            db.execute("INSERT INTO stations(id,name,address,latitude,longitude,district,created_at,updated_at) VALUES(1,'PRIVATE_STATION','address',39.9,116.4,'海淀区',?,?)", (now, now))
            for user in (1, 2):
                db.execute("INSERT INTO users(id,phone,nickname,balance_cents,created_at,updated_at) VALUES(?,?,?,5000,?,?)", (user, f"1390000000{user}", "PRIVATE_USER", now, now))
            for pile in range(1, 122):
                db.execute("INSERT INTO charging_piles(id,station_id,pile_no,charge_type,rated_power_w,status,created_at,updated_at) VALUES(?,1,?,'FAST',60000,?,?,?)", (pile, f"P-{pile}", "OFFLINE" if pile == 121 else "CHARGING" if pile == 2 else "IDLE", now, now))
            db.execute("INSERT INTO charging_orders(id,order_no,user_id,station_id,pile_id,status,price_cents_per_kwh,reserved_at,started_at,stopped_at,paid_at,duration_seconds,energy_wh,amount_cents,created_at,updated_at) VALUES(1,'O-1',1,1,1,'COMPLETED',150,?,?,?, ?,1200,20000,3000,?,?)", (earlier, earlier, now, now, now, now))
            db.execute("INSERT INTO charging_orders(id,order_no,user_id,station_id,pile_id,status,price_cents_per_kwh,started_at,created_at,updated_at) VALUES(2,'O-2',2,1,2,'CHARGING',150,?,?,?)", (now, now, now))
            db.execute("INSERT INTO recharge_records(user_id,client_request_id,amount_cents,balance_after_cents,created_at) VALUES(1,'R-1',5000,5000,?)", (now,))
            db.execute("INSERT INTO pile_status_logs(pile_id,old_status,new_status,reason,created_at) VALUES(2,'IDLE','CHARGING','START',?)", (now,))
        spec = importlib.util.spec_from_file_location("topic_flask", ROOT / "spark-warehouse/flask-api/app.py")
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        self.client = module.create_app({"TESTING": True, "ADS_BATCH_ID": "test-001", "DATABASE_PATH": str(self.database)}).test_client()

    def tearDown(self):
        self.temp.cleanup()

    def test_topic_values_and_anonymous_users(self):
        response = self.client.get("/api/v1/topics")
        self.assertEqual(response.status_code, 200)
        data = response.json
        self.assertEqual(data["orders"]["total_orders"], 2)
        self.assertEqual(data["orders"]["funnel"]["paid"], 1)
        self.assertEqual(data["users"]["active_30d"], 2)
        self.assertEqual(data["users"]["top"][0]["amount_cents"], 3000)
        self.assertEqual(data["energy"]["month_revenue_cents"], 3000)
        self.assertEqual(data["stations"][0]["pile_count"], 121)
        self.assertEqual(data["stations"][0]["busy_pile_count"], 1)
        self.assertEqual(data["pile_logs"][0]["new_status"], "CHARGING")
        self.assertIsNone(data["system"]["socket_connections"])
        self.assertNotIn("PRIVATE_USER", json.dumps(data))
        self.assertNotIn("13900000001", json.dumps(data))
        with sqlite3.connect(self.database) as db:
            self.assertEqual(db.execute("SELECT count(*) FROM charging_orders").fetchone()[0], 2)

    def test_pagination_and_filter(self):
        first = self.client.get("/api/v1/piles?page=1").json
        second = self.client.get("/api/v1/piles?page=2").json
        self.assertEqual(first["total"], 121)
        self.assertEqual(len(first["items"]), 120)
        self.assertEqual(second["items"][0]["id"], 121)
        offline = self.client.get("/api/v1/piles?status=OFFLINE").json
        self.assertEqual(offline["total"], 1)
        self.assertEqual(offline["items"][0]["id"], 121)

    def test_invalid_parameters(self):
        for query in ("page=0", "page=abc", "page_size=10000", "status=INVALID", "page_size=0"):
            self.assertEqual(self.client.get("/api/v1/piles?" + query).status_code, 400)
