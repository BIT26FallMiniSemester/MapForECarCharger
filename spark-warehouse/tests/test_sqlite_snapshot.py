import csv
from contextlib import closing
import importlib.util
import json
from pathlib import Path
import sqlite3
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("export_sqlite_snapshot", ROOT / "generator/export_sqlite_snapshot.py")
module = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(module)


class SqliteSnapshotTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.database = self.root / "business.db"
        migrations = Path(__file__).resolve().parents[2] / "server-qt/migrations"
        with closing(sqlite3.connect(self.database)) as connection:
            connection.executescript((migrations / "001_initial.sql").read_text(encoding="utf-8"))
            connection.executescript((migrations / "002_catalog_details.sql").read_text(encoding="utf-8"))
            connection.execute("INSERT INTO users VALUES(1,'13900000000','test',NULL,10000,'NORMAL','2026-09-14T00:00:00Z','2026-09-14T00:00:00Z')")
            connection.execute("INSERT INTO stations(id,name,address,latitude,longitude,price_cents_per_kwh,status,created_at,updated_at) VALUES(1,'station','address',39.9,116.4,150,'ACTIVE','2026-09-14T00:00:00Z','2026-09-14T00:00:00Z')")
            connection.execute("INSERT INTO charging_piles VALUES(1,1,'P-1','FAST',60000,'IDLE',NULL,'2026-09-14T00:00:00Z','2026-09-14T00:00:00Z')")
            connection.execute("INSERT INTO charging_orders(id,order_no,user_id,station_id,pile_id,status,price_cents_per_kwh,started_at,stopped_at,duration_seconds,energy_wh,amount_cents,paid_at,created_at,updated_at) VALUES(1,'O-1',1,1,1,'COMPLETED',150,'2026-09-14T00:00:00Z','2026-09-14T01:00:00Z',3600,10000,1500,'2026-09-14T01:01:00Z','2026-09-14T00:00:00Z','2026-09-14T01:01:00Z')")
            connection.commit()

    def test_exports_ods_compatible_readonly_snapshot(self):
        before = self.database.read_bytes()
        batch = module.export_snapshot(self.database, self.root / "out")
        metadata = json.loads((batch / "metadata.json").read_text(encoding="utf-8"))
        self.assertEqual(metadata["source"], "sqlite-readonly-snapshot")
        self.assertEqual(set(metadata["files"]), {f"dirty/{name}.csv" for name in module.TABLE_COLUMNS})
        with (batch / "dirty/charging_orders.csv").open(encoding="utf-8", newline="") as stream:
            rows = list(csv.DictReader(stream))
        self.assertEqual(rows[0]["row_id"], "charging_orders:1")
        self.assertEqual(rows[0]["amount_cents"], "1500")
        self.assertEqual(before, self.database.read_bytes())

    def test_missing_database_is_not_created(self):
        missing = self.root / "missing.db"
        with self.assertRaises(FileNotFoundError):
            module.export_snapshot(missing, self.root / "out")
        self.assertFalse(missing.exists())


if __name__ == "__main__":
    unittest.main()
