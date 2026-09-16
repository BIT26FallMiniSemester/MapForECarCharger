"""DWD facts must not retain references to quarantined dimensions."""
from __future__ import annotations

import importlib.util
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "jobs"))
sys.path.insert(0, str(ROOT))

from build_dwd import build
from schemas.table_schemas import TABLE_COLUMNS, raw_schema


@unittest.skipUnless(importlib.util.find_spec("pyspark"), "PySpark is not installed")
class DwdReferenceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        from pyspark.sql import SparkSession

        cls.spark = (SparkSession.builder.master("local[1]")
                     .config("spark.ui.enabled", "false")
                     .config("spark.sql.session.timeZone", "UTC")
                     .getOrCreate())
        cls.spark.sparkContext.setLogLevel("ERROR")

    @classmethod
    def tearDownClass(cls):
        cls.spark.stop()

    def frame(self, table, rows=()):
        return self.spark.createDataFrame(list(rows), raw_schema(table))

    @staticmethod
    def row(table, identifier, **values):
        item = {column: "" for column in TABLE_COLUMNS[table]}
        item.update(row_id=f"{table}:{identifier}", id=str(identifier), **values)
        return item

    def test_quarantined_dimensions_cascade_to_facts(self):
        stamp = "2026-09-14T01:00:00Z"
        frames = {
            "users": self.frame("users", [self.row(
                "users", 1, phone="13900000000", nickname="user",
                status="NORMAL", created_at=stamp, updated_at=stamp)]),
            "stations": self.frame("stations", [
                self.row("stations", 1, name="valid", latitude="39.9", longitude="116.4",
                         status="ACTIVE", created_at=stamp, updated_at=stamp),
                self.row("stations", 2, name="blocked", latitude="181", longitude="116.4",
                         status="ACTIVE", created_at=stamp, updated_at=stamp),
            ]),
            "charging_piles": self.frame("charging_piles", [
                self.row("charging_piles", 1, station_id="1", pile_no="P1", charge_type="FAST",
                         rated_power_w="60000", status="IDLE", created_at=stamp, updated_at=stamp),
                self.row("charging_piles", 2, station_id="2", pile_no="P2", charge_type="FAST",
                         rated_power_w="60000", status="IDLE", created_at=stamp, updated_at=stamp),
                self.row("charging_piles", 3, station_id="1", pile_no="P3", charge_type="RAPID",
                         rated_power_w="60000", status="IDLE", created_at=stamp, updated_at=stamp),
            ]),
            "charging_orders": self.frame("charging_orders", [
                self.row("charging_orders", order, order_no=f"O{order}", user_id="1",
                         station_id=str(station), pile_id=str(pile), status="COMPLETED",
                         price_cents_per_kwh="150", started_at=stamp,
                         stopped_at="2026-09-14T02:00:00Z", duration_seconds="3600",
                         energy_wh="10000", amount_cents="1500",
                         paid_at="2026-09-14T02:01:00Z", created_at=stamp, updated_at=stamp)
                for order, station, pile in ((1, 1, 1), (2, 2, 2), (3, 1, 3))
            ]),
            "recharge_records": self.frame("recharge_records"),
            "pile_status_logs": self.frame("pile_status_logs"),
        }
        issues = self.spark.createDataFrame([
            ("stations", "stations:2", "DQ003"),
            ("charging_piles", "charging_piles:3", "DQ006"),
        ], ["table_name", "row_id", "rule_id"])

        outputs = build(frames, issues)

        self.assertEqual([row.order_id for row in outputs["dwd_charging_order_detail"].collect()], [1])
        quarantined = {row.row_id: set(row.quality_flags)
                       for row in outputs["dwd_quarantine"].collect()}
        self.assertIn("DWD_FK", quarantined["charging_piles:2"])
        self.assertIn("DWD_FK", quarantined["charging_orders:2"])
        self.assertIn("DWD_FK", quarantined["charging_orders:3"])


if __name__ == "__main__":
    unittest.main()
