"""Load one generated dirty-data batch into immutable ODS Parquet tables."""
from __future__ import annotations

import argparse
import json
from pathlib import Path

from common import batch_table_path, create_spark, validate_batch_id
from schemas.table_schemas import TABLE_COLUMNS, raw_schema


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", required=True, help="Generated batch directory containing dirty/*.csv")
    parser.add_argument("--output", required=True, help="ODS root, local path or hdfs:// URI")
    parser.add_argument("--batch-id", help="Expected batch id; defaults to metadata.json")
    parser.add_argument("--master", help="Spark master override, for example local[*]")
    return parser.parse_args()


def main() -> None:
    from pyspark.sql import functions as F

    args = parse_args()
    source = Path(args.input).resolve()
    metadata_path = source / "metadata.json"
    if not metadata_path.is_file():
        raise FileNotFoundError(f"missing metadata: {metadata_path}")
    metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
    batch_id = validate_batch_id(args.batch_id or metadata["batch_id"])
    if batch_id != metadata["batch_id"]:
        raise ValueError("--batch-id does not match metadata.json")

    spark = create_spark(f"MapForECar-ODS-{batch_id}", args.master)
    try:
        for table, columns in TABLE_COLUMNS.items():
            csv_path = source / "dirty" / f"{table}.csv"
            if not csv_path.is_file():
                raise FileNotFoundError(f"missing source table: {csv_path}")
            frame = spark.read.option("header", True).option("mode", "FAILFAST").schema(raw_schema(table)).csv(str(csv_path))
            if frame.columns != columns:
                raise ValueError(f"unexpected columns for {table}: {frame.columns}")
            enriched = (frame.withColumn("source_file", F.input_file_name())
                        .withColumn("ingest_time", F.current_timestamp()))
            target = batch_table_path(args.output, f"ods_{table}", batch_id)
            enriched.write.mode("errorifexists").parquet(target)
            print(f"{table}: {enriched.count()} rows -> {target}")
    finally:
        spark.stop()


if __name__ == "__main__":
    main()
