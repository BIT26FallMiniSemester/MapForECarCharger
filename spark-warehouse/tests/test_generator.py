import csv
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

import yaml

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("generate_all", ROOT / "generator/generate_all.py")
generator = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(generator)


class GeneratorTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.output = Path(self.temp.name)
        self.batch = generator.generate(ROOT / "conf/generator.yaml", ROOT / "conf/quality_rules.yaml", self.output, "quick")
        self.metadata = json.loads((self.batch / "metadata.json").read_text(encoding="utf-8"))

    def rows(self, variant, table):
        with (self.batch / variant / f"{table}.csv").open(encoding="utf-8", newline="") as stream:
            return list(csv.DictReader(stream))

    def test_expected_counts_and_all_rules(self):
        expected = {"users":100,"stations":50,"charging_orders":3000,
                    "recharge_records":500,"pile_status_logs":5000}
        for table, count in expected.items():
            self.assertEqual(len(self.rows("clean", table)), count)
        self.assertEqual(self.metadata["rules"], [f"DQ{i:03d}" for i in range(1,18)])
        self.assertGreater(self.metadata["issue_count"], 0)
        self.assertGreater(len(self.rows("dirty", "users")), len(self.rows("clean", "users")))

    def test_clean_primary_foreign_and_active_constraints(self):
        datasets = {table:self.rows("clean", table) for table in generator.TABLES}
        for table, rows in datasets.items():
            identifiers = [row["id"] for row in rows]
            self.assertEqual(len(identifiers), len(set(identifiers)), table)
        user_ids = {row["id"] for row in datasets["users"]}
        self.assertTrue(all(len(row["phone"]) == 11 and row["phone"].startswith("1") for row in datasets["users"]))
        station_ids = {row["id"] for row in datasets["stations"]}
        piles = {row["id"]:row for row in datasets["charging_piles"]}
        active_users, active_piles = set(), set()
        for order in datasets["charging_orders"]:
            self.assertIn(order["user_id"], user_ids)
            self.assertIn(order["station_id"], station_ids)
            self.assertEqual(piles[order["pile_id"]]["station_id"], order["station_id"])
            if order["status"] in {"PENDING","RESERVED","CHARGING","UNPAID"}:
                self.assertNotIn(order["user_id"], active_users)
                active_users.add(order["user_id"])
            if order["status"] in {"RESERVED","CHARGING"}:
                self.assertNotIn(order["pile_id"], active_piles)
                active_piles.add(order["pile_id"])

    def test_completed_order_amount_and_timeline(self):
        for row in self.rows("clean", "charging_orders"):
            if row["status"] == "COMPLETED":
                self.assertTrue(row["started_at"] < row["stopped_at"] < row["paid_at"])
                expected = round(int(row["energy_wh"]) * int(row["price_cents_per_kwh"]) / 1000)
                self.assertEqual(int(row["amount_cents"]), expected)

    def test_manifest_points_to_dirty_rows_and_keeps_before_after(self):
        issues = [json.loads(line) for line in (self.batch / "_injected_issues.jsonl").read_text(encoding="utf-8").splitlines()]
        dirty = {table:{row["row_id"]:row for row in self.rows("dirty",table)} for table in generator.TABLES}
        for issue in issues:
            self.assertIn(issue["row_id"], dirty[issue["table"]])
            self.assertTrue(issue["fields"])
            self.assertIn("before", issue)
            self.assertIn("after", issue)

    def test_reproducible_data_files(self):
        other = self.output / "second"
        second = generator.generate(ROOT / "conf/generator.yaml", ROOT / "conf/quality_rules.yaml", other, "quick")
        second_meta = json.loads((second / "metadata.json").read_text(encoding="utf-8"))
        self.assertEqual(self.metadata["files"], second_meta["files"])
        self.assertEqual((self.batch / "_injected_issues.jsonl").read_bytes(), (second / "_injected_issues.jsonl").read_bytes())

    def test_existing_batch_is_not_overwritten(self):
        marker = self.batch / "metadata.json"
        before = marker.read_bytes()
        with self.assertRaises(FileExistsError):
            generator.generate(ROOT / "conf/generator.yaml", ROOT / "conf/quality_rules.yaml", self.output, "quick")
        self.assertEqual(marker.read_bytes(), before)


if __name__ == "__main__":
    unittest.main()
