"""Topic aggregates and bounded pile pagination from Spark ADS snapshots."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class TopicApiTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.batch = "test-001"
        topic = {"batch_id": self.batch, "source": "Spark DWD / DWS / ADS",
                 "orders": {"total_orders": 2, "funnel": {"paid": 1}},
                 "users": {"active_30d": 2}, "energy": {"month_revenue_cents": 3000},
                 "stations": [{"station_id": 1, "pile_count": 121, "busy_pile_count": 1}],
                 "pile_logs": [{"id": 1, "new_status": "CHARGING"}],
                 "system": {"storage": "Spark ADS", "tables": []}}
        meta = {"batch_id": self.batch, "generated_at": "2026-09-16T00:00:00Z", "data_as_of": "2026-09-16"}
        datasets = {
            "ads_topics": [{**meta, "payload_json": json.dumps(topic)}],
            "ads_pile_inventory": [{**meta, "id": pile, "station_id": 1, "pile_no": f"P-{pile}",
                "status": "OFFLINE" if pile == 121 else "CHARGING" if pile == 2 else "IDLE", "rated_power_w": 60000}
                for pile in range(1, 122)],
        }
        for name, rows in datasets.items():
            target = self.root / name / f"batch_id={self.batch}" / "json"
            target.mkdir(parents=True)
            (target / "part-00000.json").write_text("\n".join(json.dumps(row) for row in rows), encoding="utf-8")
        spec = importlib.util.spec_from_file_location("topic_flask", ROOT / "flask-api/app.py")
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        self.client = module.create_app({"TESTING": True, "ADS_ROOT": str(self.root),
                                         "ADS_BATCH_ID": self.batch, "ADS_CACHE_SECONDS": 0}).test_client()

    def tearDown(self):
        self.temp.cleanup()

    def test_topics_are_published_spark_payload(self):
        response = self.client.get("/api/v1/topics")
        self.assertEqual(response.status_code, 200)
        data = response.json
        self.assertEqual(data["source"], "Spark DWD / DWS / ADS")
        self.assertEqual(data["orders"]["total_orders"], 2)
        self.assertEqual(data["stations"][0]["pile_count"], 121)
        self.assertNotIn("SQLite", json.dumps(data))

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


if __name__ == "__main__":
    unittest.main()
