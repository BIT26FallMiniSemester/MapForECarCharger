"""Shared command-line and Spark helpers for warehouse jobs."""
from __future__ import annotations

import re
from pathlib import Path
import sys

WAREHOUSE_ROOT = Path(__file__).resolve().parents[1]
if str(WAREHOUSE_ROOT) not in sys.path:
    sys.path.insert(0, str(WAREHOUSE_ROOT))

BATCH_PATTERN = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]{2,127}$")


def validate_batch_id(value: str) -> str:
    if not BATCH_PATTERN.fullmatch(value):
        raise ValueError("batch_id must be 3-128 safe filename characters")
    return value


def create_spark(app_name: str, master: str | None = None):
    from pyspark.sql import SparkSession

    builder = SparkSession.builder.appName(app_name)
    if master:
        builder = builder.master(master)
    return builder.config("spark.sql.session.timeZone", "UTC").getOrCreate()


def parse_timestamp(column):
    """Parse ISO-8601 timestamps with optional fractional seconds."""
    from pyspark.sql import functions as F

    return F.to_timestamp(F.trim(column), "yyyy-MM-dd'T'HH:mm:ss[.SSSSSSSSS]X")


def batch_table_path(root: str, layer_table: str, batch_id: str) -> str:
    # Bare paths belong to the local warehouse, regardless of Hadoop fs.defaultFS.
    # Preserve explicit storage URIs such as hdfs:// and file://.
    storage_root = root if "://" in root else Path(root).resolve().as_uri()
    return f"{storage_root.rstrip('/')}/{layer_table}/batch_id={validate_batch_id(batch_id)}"
