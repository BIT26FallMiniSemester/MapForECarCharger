"""Spark timestamp compatibility tests."""
from __future__ import annotations

import importlib.util
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "jobs"))

from common import parse_timestamp


@unittest.skipUnless(importlib.util.find_spec("pyspark"), "PySpark is not installed")
class TimestampCompatibilityTests(unittest.TestCase):
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

    def test_accepts_seconds_and_fractional_seconds(self):
        rows = self.spark.createDataFrame([
            ("2026-09-14T01:02:03Z",),
            ("2026-09-14T01:02:03.107Z",),
            ("2026-09-14T01:02:03.107123Z",),
            ("not-a-timestamp",),
        ], ["value"]).select(parse_timestamp("value").alias("parsed")).collect()

        self.assertTrue(all(row.parsed is not None for row in rows[:3]))
        self.assertIsNone(rows[3].parsed)


if __name__ == "__main__":
    unittest.main()
