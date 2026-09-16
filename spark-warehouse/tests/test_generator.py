import csv
from collections import Counter
from datetime import date, datetime, time
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
        expected = {"users":250,"stations":100,"charging_orders":10000,
                    "recharge_records":1500,"pile_status_logs":15000}
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

    def test_clean_sessions_do_not_overlap_on_one_pile(self):
        sessions = {}
        for order in self.rows("clean", "charging_orders"):
            if order["started_at"] and order["stopped_at"]:
                sessions.setdefault(order["pile_id"], []).append((order["started_at"], order["stopped_at"]))
        for pile_id, intervals in sessions.items():
            previous_stop = ""
            for start, stop in sorted(intervals):
                self.assertGreaterEqual(start, previous_stop, pile_id)
                previous_stop = max(previous_stop, stop)

    def test_faulty_piles_have_final_events_and_power_varies(self):
        piles = self.rows("clean", "charging_piles")
        statuses = Counter(pile["status"] for pile in piles)
        self.assertEqual(statuses["FAULT"], round(len(piles) * 0.10))
        self.assertEqual(statuses["OFFLINE"], round(len(piles) * 0.02))
        config = generator.parse_config(ROOT / "conf/generator.yaml", "quick")
        end = date.fromisoformat(str(config["end_date"]))
        terminal_stamp = generator.iso(datetime.combine(end, time.max, generator.BEIJING))
        final_events = {int(log["pile_id"]): log for log in self.rows("clean", "pile_status_logs")
                        if log["reason"] in {"SIMULATED_FAILURE", "SIMULATED_NETWORK_LOSS"}
                        and log["created_at"] == terminal_stamp}
        for pile in piles:
            if pile["status"] in {"FAULT", "OFFLINE"}:
                event = final_events[int(pile["id"])]
                self.assertEqual((event["old_status"], event["new_status"], event["created_at"]),
                                 ("IDLE", pile["status"], pile["updated_at"]))
                self.assertEqual(pile["reserved_order_id"], "")
        for charge_type in ("FAST", "SLOW"):
            self.assertGreater(len({pile["rated_power_w"] for pile in piles if pile["charge_type"] == charge_type}), 1)

    def test_order_number_uses_beijing_day(self):
        orders = self.rows("clean", "charging_orders")
        daily = Counter()
        for order in orders:
            utc = datetime.fromisoformat(order["created_at"].replace("Z", "+00:00"))
            local_day = utc.astimezone(generator.BEIJING).strftime("%Y%m%d")
            self.assertEqual(order["order_no"][2:10], local_day)
            daily[local_day] += 1
        self.assertLess(max(daily.values()), len(orders) / len(daily) * 1.3)

    def test_default_end_date_is_today_and_no_timestamp_is_in_future(self):
        today = datetime.now(generator.BEIJING).date()
        self.assertEqual(date.fromisoformat(self.metadata["end_date"]), today)
        terminal = datetime.combine(today, time.max, generator.BEIJING).astimezone(generator.UTC)
        timestamp_fields = ("created_at", "reserved_at", "expires_at", "started_at", "stopped_at",
                            "paid_at", "cancelled_at", "updated_at")
        for order in self.rows("clean", "charging_orders"):
            for field in timestamp_fields:
                if order[field]:
                    value = datetime.fromisoformat(order[field].replace("Z", "+00:00"))
                    self.assertLessEqual(value, terminal, (order["id"], field, order[field]))
        for table, field in (("users", "created_at"), ("recharge_records", "created_at"),
                             ("pile_status_logs", "created_at")):
            for row in self.rows("clean", table):
                value = datetime.fromisoformat(row[field].replace("Z", "+00:00"))
                self.assertLessEqual(value, terminal, (table, row["id"], row[field]))

    def test_realtime_orders_are_extra_and_match_pile_events(self):
        config = generator.parse_config(ROOT / "conf/generator.yaml", "quick")
        config.update(orders=1000, realtime_charging_orders=5)
        datasets = generator.build_clean(config)
        orders = datasets["charging_orders"]
        realtime = [order for order in orders if order["status"] == "CHARGING"]
        self.assertEqual((len(orders), len(realtime)), (1005, 5))
        self.assertEqual([order["id"] for order in realtime], list(range(1001, 1006)))
        piles = {pile["id"]: pile for pile in datasets["charging_piles"]}
        events = {event["order_id"]: event for event in datasets["pile_status_logs"]
                  if event["reason"] == "SIMULATED_CHARGING_START"}
        self.assertEqual(len(events), 5)
        self.assertEqual(len({order["pile_id"] for order in realtime}), 5)
        for order in realtime:
            self.assertEqual(piles[order["pile_id"]]["status"], "CHARGING")
            self.assertTrue(order["started_at"])
            self.assertEqual((order["stopped_at"], order["paid_at"]), ("", ""))
            self.assertEqual(events[order["id"]]["created_at"], order["started_at"])
            self.assertEqual(int(order["energy_wh"]), round(
                int(piles[order["pile_id"]]["rated_power_w"]) * int(order["duration_seconds"]) / 3600))
            for prior in orders[:1000]:
                if prior["pile_id"] == order["pile_id"] and prior["stopped_at"]:
                    self.assertLessEqual(prior["stopped_at"], order["created_at"])
        dirty, manifest = generator.inject_issues(
            datasets, generator.load_rules(ROOT / "conf/quality_rules.yaml"), config["seed"])
        dirty_orders = {order["id"]: order for order in dirty["charging_orders"]}
        self.assertTrue(all(dirty_orders[order["id"]] == order for order in realtime))
        realtime_ids = {order["id"] for order in realtime}
        protected = {
            "users": {generator.row_id("users", order["user_id"]) for order in realtime},
            "stations": {generator.row_id("stations", order["station_id"]) for order in realtime},
            "charging_piles": {generator.row_id("charging_piles", order["pile_id"]) for order in realtime},
            "charging_orders": {order["row_id"] for order in realtime},
            "pile_status_logs": {row["row_id"] for row in datasets["pile_status_logs"]
                                 if row["order_id"] in realtime_ids},
        }
        manifest_rows = {(issue["table"], issue["row_id"]) for issue in manifest}
        for table, identifiers in protected.items():
            clean_rows = {row["row_id"]: row for row in datasets[table]}
            dirty_rows = {row["row_id"]: row for row in dirty[table]}
            for identifier in identifiers:
                self.assertEqual(dirty_rows[identifier], clean_rows[identifier])
                self.assertNotIn((table, identifier), manifest_rows)

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
