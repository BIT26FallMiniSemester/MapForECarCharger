import csv
import io
import json
import math
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).parents[1] / "src"))
from prepare_history import clean, generate, write_csv
from workflow import run, analyze
from export_predictions_api import build_entries, catalog_references
from post_predictions_api import main as post


class WorkflowTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        root = Path(__file__).parents[1] / "outputs"
        root.mkdir(exist_ok=True)
        cls.temp = tempfile.TemporaryDirectory(prefix="test-", dir=root)
        cls.addClassCleanup(cls.temp.cleanup)
        cls.root = Path(cls.temp.name)
        cls.out = cls.root / "demo"
        run(cls.out)
        cls.catalog = json.loads((cls.out / "data/catalog.json").read_text())
        cls.predictions = json.loads((cls.out / "predictions.json").read_text())
        with (cls.out / "data/history.csv").open() as source:
            cls.history = list(csv.DictReader(source))

    def test_reproducible_raw_history_and_features(self):
        other = self.root / "repeat"
        generate(other)
        for name in ("catalog.json", "orders.csv", "devices.csv", "holidays.json"):
            self.assertEqual((other / name).read_bytes(), (self.out / "data" / name).read_bytes())
        self.assertEqual(len(self.history), 3 * 45 * 24)
        self.assertTrue(any(int(r["is_holiday"]) for r in self.history))
        self.assertTrue(any(int(r["unavailable_piles"]) for r in self.history))
        manifest = json.loads((self.out / "manifest.json").read_text())
        self.assertEqual(len(manifest["features"]), 16)
        self.assertTrue(manifest["simulated"])
        self.assertEqual(manifest["model_version"], self.predictions["model_version"])
        split = json.loads((self.out / "metrics.json.evaluation.csv.split.json").read_text())
        self.assertLess(split["train_target_max"], split["validation_origin_min"])
        self.assertLess(split["validation_target_max"], split["test_origin_min"])

    def test_hourly_forecast_and_resource_bounds(self):
        end = self.catalog["metadata"]["end_epoch"]
        rows = self.predictions["predictions"]
        self.assertEqual(len(rows), 72)
        for sid in (1, 2, 3):
            sequence = [p for p in rows if p["station_id"] == sid]
            self.assertEqual([p["horizon_hours"] for p in sequence], list(range(1, 25)))
            self.assertEqual([p["predicted_for_epoch"] for p in sequence],
                             list(range(end, end + 24 * 3600, 3600)))
            for p in sequence:
                self.assertTrue(0 <= p["predicted_load_kw"] <= 28)
                self.assertTrue(0 <= p["predicted_available_piles"] <= 4)
                self.assertTrue(0 <= p["congestion_ratio"] <= 1)

    def test_windows_export_and_mock_post(self):
        batch = json.loads((self.out / "predictions_api.json").read_text())
        self.assertEqual(len(batch["entries"]), 9)
        for entry in batch["entries"]:
            payload = entry["payload"]
            self.assertEqual(len(payload["points"]), 3 * payload["horizon_hours"])
            self.assertLessEqual(len(payload["points"]), 500)
            self.assertTrue(1 <= len(payload["model_version"]) <= 64)
            self.assertEqual({p["prediction_type"] for p in payload["points"]},
                             {"LOAD_W", "AVAILABLE_PILES", "CONGESTION_SCORE"})
            self.assertTrue(all(p["predicted_for"].endswith("Z") for p in payload["points"]))
        sent = []
        def opener(request, timeout):
            self.assertEqual(request.headers["X-internal-key"], "test-only")
            if request.method == "GET":
                body = {"items": [{"station_id": 100 + s["id"], "data_source": "ML_SYNTHETIC",
                                   "external_id": str(s["id"])} for s in self.catalog["stations"]]}
            else:
                self.assertTrue(request.full_url.endswith("/api/v1/internal/predictions/load"))
                payload = json.loads(request.data)
                self.assertIn(payload["station_id"], [101, 102, 103])
                sent.append(payload)
                body = {"points_written": len(payload["points"])}
            return io.BytesIO(json.dumps({"code": 0, "data": body}).encode())
        with patch.dict("os.environ", {"INTERNAL_KEY": "test-only"}):
            post(self.out / "predictions_api.json", "http://127.0.0.1:8000", True, opener)
            self.assertEqual(sent, [])
            post(self.out / "predictions_api.json", "http://127.0.0.1:8000", False, opener)
        self.assertEqual(len(sent), 9)

    def test_upload_preflight_prevents_writes(self):
        calls = []
        def empty_catalog(request, timeout):
            calls.append(request.method)
            return io.BytesIO(b'{"code":0,"data":{"items":[]}}')
        with patch.dict("os.environ", {}, clear=True):
            with self.assertRaises(RuntimeError):
                post(self.out / "predictions_api.json", "http://127.0.0.1:8000", False, empty_catalog)
        self.assertEqual(calls, [])
        with patch.dict("os.environ", {"INTERNAL_KEY": "test-only"}):
            with self.assertRaises(ValueError):
                post(self.out / "predictions_api.json", "http://127.0.0.1:8000", False, empty_catalog)
        self.assertEqual(calls, ["GET"])

    def test_recommendations_alerts_and_chart(self):
        result = analyze(self.catalog, self.history, self.predictions, 39.9, 116.4, threshold=.01)
        self.assertEqual(len(result["peaks"]), 3)
        self.assertTrue(any(a["type"] == "HIGH_LOAD" for a in result["alerts"]))
        self.assertTrue(all(len(r["reasons"]) == 3 for r in result["recommendations"]))
        self.assertEqual([r["score"] for r in result["recommendations"]],
                         sorted(r["score"] for r in result["recommendations"]))
        altered = [dict(row) for row in self.history]
        altered[-1].update(unavailable_piles="4", fault_piles="1", unknown_piles="1")
        result = analyze(self.catalog, altered, self.predictions, 39.9, 116.4)
        self.assertTrue(any(a["type"] == "DEVICE_ABNORMAL" for a in result["alerts"]))
        self.assertNotIn(3, [r["station_id"] for r in result["recommendations"]])
        from xml.etree import ElementTree
        ElementTree.parse(self.out / "evaluation.svg")
        report = json.loads((self.out / "evaluation.json").read_text())
        self.assertEqual(len(report["all_station_metrics"]), 24)
        self.assertEqual(set(report["sample_error_analysis"]), {"1", "6", "24"})
        self.assertTrue(all(math.isfinite(v["model_rmse"]) for v in report["all_station_metrics"].values()))

    def test_cleaning_duplicates_missing_outliers_and_hour_overlap(self):
        root = self.root / "dirty"
        start = 1788220800
        order = {"id": "1", "station_id": 1, "pile_id": 10, "started_at": start + 1800,
                 "stopped_at": start + 5400, "energy_wh": 7000}
        dirty = [order, dict(order), dict(order, id="2", energy_wh="nan"),
                 dict(order, id="3", energy_wh=-1), dict(order, id="4", stopped_at="")]
        write_csv(root / "orders.csv", list(order), dirty)
        write_csv(root / "devices.csv", ["pile_id", "reported_at", "status"],
                  [{"pile_id": 10, "reported_at": start + 3600, "status": "FAULT"}])
        history, report = clean(self.catalog, root / "orders.csv", root / "devices.csv",
                                root / "history.csv", start, start + 7200)
        self.assertEqual(report["counts"]["duplicate_orders"], 1)
        self.assertEqual(report["counts"]["rejected_orders"], 3)
        self.assertEqual(history[0]["load_kw"], 3.5)
        self.assertEqual(history[1]["load_kw"], 3.5)
        self.assertEqual(history[0]["fault_piles"], 0) # Never backfill from future events.
        self.assertEqual(history[0]["unknown_piles"], 4)
        self.assertEqual(history[1]["fault_piles"], 1)

    def test_reject_bad_predictions_and_model(self):
        predictions = [dict(p) for p in self.predictions["predictions"]]
        refs = catalog_references(self.catalog)
        with self.assertRaises(ValueError):
            build_entries(predictions[:-1], 1788220800, "test", refs)
        predictions[0]["predicted_load_kw"] = float("nan")
        with self.assertRaises(ValueError):
            build_entries(predictions, 1788220800, "test", refs)
        model = (self.out / "model.txt").read_text().splitlines()
        scales = model[3].split()
        scales[1] = "0"
        model[3] = " ".join(scales)
        bad = self.root / "invalid-model.txt"
        bad.write_text("\n".join(model))
        process = subprocess.run([str(self.out / "pklot_ml"), "predict", str(self.out / "data/history.csv"),
                                  str(bad), str(self.root / "invalid.json")], capture_output=True, text=True)
        self.assertNotEqual(process.returncode, 0)
        self.assertIn("invalid model scale", process.stderr)
        bad_csv = self.root / "invalid.csv"
        bad_csv.write_text("timestamp_epoch,station_id,total_piles,capacity_kw,load_kw\n1788220800,1oops,4,28,7\n")
        process = subprocess.run([str(self.out / "pklot_ml"), "predict", str(bad_csv),
                                  str(self.out / "model.txt"), str(self.root / "invalid.json")],
                                 capture_output=True, text=True)
        self.assertNotEqual(process.returncode, 0)
        self.assertIn("invalid integer field", process.stderr)


if __name__ == "__main__":
    unittest.main()
