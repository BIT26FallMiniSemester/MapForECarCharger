import importlib.util
from contextlib import closing
from pathlib import Path
import sqlite3
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("create_showcase_simulation", ROOT / "server-qt/tools/create_showcase_simulation.py")
module = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(module)


class ShowcaseSimulationTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.source = Path(self.temp.name) / "source.db"
        self.target = Path(self.temp.name) / "target.db"
        with closing(sqlite3.connect(self.source)) as db:
            for name in ("001_initial.sql", "002_catalog_details.sql"):
                db.executescript((ROOT / "server-qt/migrations" / name).read_text(encoding="utf-8"))
            db.execute("INSERT INTO stations(id,name,address,latitude,longitude,price_cents_per_kwh,status,created_at,updated_at) VALUES(1,'S','A',39.9,116.4,150,'ACTIVE','2026-01-01T00:00:00Z','2026-01-01T00:00:00Z')")
            db.execute("INSERT INTO charging_piles(id,station_id,pile_no,charge_type,rated_power_w,status,created_at,updated_at) VALUES(1,1,'P1','FAST',60000,'IDLE','2026-01-01T00:00:00Z','2026-01-01T00:00:00Z')")
            db.commit()

    def test_clones_source_and_adds_consistent_history(self):
        source_before = self.source.read_bytes()
        result = module.create_simulation(self.source, self.target, orders=20, users=4, days=30, seed=7)
        self.assertEqual(result["status_logs_added"], 60)
        with closing(sqlite3.connect(self.target)) as db:
            self.assertEqual(db.execute("SELECT count(*) FROM charging_orders").fetchone()[0], 20)
            self.assertEqual(db.execute("SELECT count(*) FROM recharge_records").fetchone()[0], 4)
            self.assertEqual(db.execute("SELECT count(*) FROM pile_status_logs").fetchone()[0], 60)
            self.assertEqual(db.execute("PRAGMA foreign_key_check").fetchall(), [])
            self.assertEqual(db.execute("SELECT count(*) FROM charging_orders WHERE amount_cents!=round(energy_wh*price_cents_per_kwh/1000.0)").fetchone()[0], 0)
        self.assertEqual(self.source.read_bytes(), source_before)

    def test_refuses_to_overwrite_target(self):
        self.target.write_text("keep", encoding="utf-8")
        with self.assertRaises(FileExistsError):
            module.create_simulation(self.source, self.target, 1, 1, 2, 1)
        self.assertEqual(self.target.read_text(encoding="utf-8"), "keep")


if __name__ == "__main__":
    unittest.main()
