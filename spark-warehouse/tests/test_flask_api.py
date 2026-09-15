from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
import sqlite3
from datetime import datetime, timezone

ROOT = Path(__file__).resolve().parents[1]
APP_PATH = ROOT / "flask-api" / "app.py"


def load_app_module():
    spec = importlib.util.spec_from_file_location("warehouse_flask_app", APP_PATH)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class FlaskApiTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        try:
            cls.module = load_app_module()
        except ModuleNotFoundError as error:
            if error.name == "flask":
                raise unittest.SkipTest("Flask is not installed") from error
            raise

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.batch = "batch-001"
        meta = {"batch_id": self.batch, "generated_at": "2026-09-14T02:00:00Z", "data_as_of": "2026-09-14"}
        datasets = {
            "ads_overview": [{**meta, "total_revenue_cents": 1000, "today_revenue_cents": 300, "today_order_count": 2, "station_count": 1, "pile_count": 2}],
            "ads_revenue_trend_30d": [{**meta, "biz_date": "2026-09-14", "revenue_cents": 300, "order_count": 2}],
            "ads_station_ranking_30d": [{**meta, "station_id": 1, "station_name": "测试站", "revenue_cents": 300}],
            "ads_district_distribution": [{**meta, "district": "海淀区", "revenue_cents": 300}],
            "ads_station_distribution": [{**meta, "station_id": 1, "station_name": "测试站", "latitude": 39.9, "longitude": 116.3}],
            "ads_pile_status": [{**meta, "status": "IDLE", "count": 2}],
            "ads_pile_utilization": [{**meta, "pile_id": 1, "utilization_rate": 0.2}],
            "ads_quality_overview": [{**meta, "quality_score": 98.0}],
            "ads_quality_rules": [{**meta, "rule_id": "DQ001", "issue_count": 1}],
        }
        for name, rows in datasets.items():
            target = self.root / name / f"batch_id={self.batch}" / "json"
            target.mkdir(parents=True)
            (target / "part-00000.json").write_text("\n".join(json.dumps(row) for row in rows), encoding="utf-8")
        self.ml_path = self.root / "predictions.json"
        self.ml_path.write_text(json.dumps({"model_version": "test-v1", "predictions": [
            {"station_id": 1, "horizon_hours": 1, "predicted_for_epoch": 1789347600,
             "predicted_load_kw": 12.5, "predicted_available_piles": 3, "predicted_occupied_piles": 1},
            {"station_id": 2, "horizon_hours": 1, "predicted_for_epoch": 1789347600,
             "predicted_load_kw": 7.5, "predicted_available_piles": 2, "predicted_occupied_piles": 2},
        ]}), encoding="utf-8")
        self.database = self.root / "live.db"
        connection = sqlite3.connect(self.database)
        connection.executescript("""
            CREATE TABLE stations(id INTEGER PRIMARY KEY,name TEXT,status TEXT);
            CREATE TABLE charging_piles(id INTEGER PRIMARY KEY,station_id INTEGER,pile_no TEXT,status TEXT,rated_power_w INTEGER);
            CREATE TABLE charging_orders(id INTEGER PRIMARY KEY,order_no TEXT,status TEXT,station_id INTEGER,pile_id INTEGER,amount_cents INTEGER,energy_wh INTEGER,created_at TEXT,paid_at TEXT,stopped_at TEXT);
            INSERT INTO stations VALUES(1,'实时站','ACTIVE');
            INSERT INTO charging_piles VALUES(1,1,'P-1','CHARGING',60000);
        """)
        now = datetime.now(timezone.utc).isoformat(timespec="milliseconds").replace("+00:00", "Z")
        connection.execute("INSERT INTO charging_orders VALUES(1,'LIVE-1','CHARGING',1,1,450,3000,?,NULL,NULL)", (now,))
        connection.commit()
        connection.close()
        app = self.module.create_app({"TESTING": True, "ADS_ROOT": str(self.root), "ADS_BATCH_ID": self.batch,
                                      "ADS_CACHE_SECONDS": 0, "ML_PREDICTIONS_PATH": str(self.ml_path),
                                      "DATABASE_PATH": str(self.database)})
        self.client = app.test_client()

    def tearDown(self):
        self.temp.cleanup()

    def test_health_and_v1_endpoints(self):
        self.assertEqual(self.client.get("/health").status_code, 200)
        self.assertEqual(self.client.get("/api/v1/overview").json["total_revenue_cents"], 1000)
        self.assertEqual(self.client.get("/api/v1/trends/revenue?days=1").json["items"][0]["order_count"], 2)
        self.assertEqual(self.client.get("/api/v1/quality/rules").json["items"][0]["rule_id"], "DQ001")

    def test_compatibility_endpoints(self):
        dashboard = self.client.get("/api/dashboard").json
        self.assertEqual(dashboard["today_orders"], 1)
        self.assertEqual(dashboard["load_prediction"]["model_version"], "test-v1")
        self.assertEqual(dashboard["load_prediction"]["points"][0]["load_w"], 20000.0)
        self.assertEqual(dashboard["load_prediction"]["points"][0]["available_piles"], 5)
        self.assertEqual(dashboard["realtime_orders"][0]["order_no"], "LIVE-1")
        self.assertEqual(dashboard["pile_status"], [{"name": "CHARGING", "value": 1}])
        analytics = self.client.get("/api/analytics").json
        self.assertTrue(analytics["available"])
        self.assertEqual(analytics["data"]["engine"], "spark-sql")

    def test_missing_dataset_returns_503(self):
        for path in (self.root / "ads_quality_rules" / f"batch_id={self.batch}" / "json").glob("*.json"):
            path.unlink()
        response = self.client.get("/api/v1/quality/rules")
        self.assertEqual(response.status_code, 503)
        self.assertEqual(response.json["error"], "ads_not_ready")


if __name__ == "__main__":
    unittest.main()
