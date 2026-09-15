"""Tests for job contracts that do not require a Spark installation."""
from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "jobs"))
sys.path.insert(0, str(ROOT))

from common import batch_table_path, validate_batch_id
from generator.generate_all import COLUMNS
from schemas.table_schemas import TABLE_COLUMNS


class JobContractTests(unittest.TestCase):
    def test_schema_matches_generator_exactly(self):
        self.assertEqual(COLUMNS, TABLE_COLUMNS)

    def test_all_jobs_import_without_pyspark_installed(self):
        for name in ("ingest_ods", "detect_quality", "build_dwd", "build_dws", "build_ads", "run_pipeline"):
            path = ROOT / "jobs" / f"{name}.py"
            spec = importlib.util.spec_from_file_location(name, path)
            module = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(module)

    def test_batch_id_validation(self):
        self.assertEqual(validate_batch_id("sim-quick-2026-09-14"), "sim-quick-2026-09-14")
        for invalid in ("", "ab", "../escape", "contains space", "x/child"):
            with self.assertRaises(ValueError):
                validate_batch_id(invalid)

    def test_batch_path(self):
        self.assertEqual(
            batch_table_path("hdfs:///warehouse/", "ods_users", "batch-001"),
            "hdfs:///warehouse/ods_users/batch_id=batch-001",
        )

    def test_local_batch_path_is_explicit_even_with_hadoop_config(self):
        root = ROOT / "runtime" / "warehouse"
        self.assertEqual(
            batch_table_path(str(root), "ods_users", "batch-001"),
            root.resolve().as_uri() + "/ods_users/batch_id=batch-001",
        )
        self.assertEqual(
            batch_table_path("file:///tmp/warehouse/", "ods_users", "batch-001"),
            "file:///tmp/warehouse/ods_users/batch_id=batch-001",
        )

    def test_vscode_tasks_are_valid_json(self):
        tasks = json.loads((ROOT.parent / ".vscode" / "tasks.json").read_text(encoding="utf-8"))
        self.assertEqual(tasks["version"], "2.0.0")
        labels = {task["label"] for task in tasks["tasks"]}
        self.assertIn("验证：主机完整检查", labels)
        self.assertIn("启动：Web 大屏", labels)


if __name__ == "__main__":
    unittest.main()
