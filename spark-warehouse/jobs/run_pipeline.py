"""Run the complete ODS -> quality -> DWD -> DWS -> ADS pipeline."""
from __future__ import annotations

import argparse
from pathlib import Path
import subprocess

from common import validate_batch_id


def parse_args():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", required=True)
    parser.add_argument("--warehouse", required=True)
    parser.add_argument("--batch-id", required=True)
    parser.add_argument("--master", default="local[2]")
    parser.add_argument("--spark-submit", default="spark-submit")
    return parser.parse_args()


def main():
    args = parse_args()
    batch_id = validate_batch_id(args.batch_id)
    jobs = Path(__file__).resolve().parent
    roots = {layer: f"{args.warehouse.rstrip('/')}/{layer}" for layer in ("ods", "quality-reports", "dwd", "dws", "ads")}
    commands = [
        ["ingest_ods.py", "--input", args.input, "--output", roots["ods"]],
        ["detect_quality.py", "--ods", roots["ods"], "--output", roots["quality-reports"], "--batch-id", batch_id],
        ["build_dwd.py", "--ods", roots["ods"], "--quality", roots["quality-reports"], "--output", roots["dwd"], "--batch-id", batch_id],
        ["build_dws.py", "--dwd", roots["dwd"], "--quality", roots["quality-reports"], "--output", roots["dws"], "--batch-id", batch_id],
        ["build_ads.py", "--dws", roots["dws"], "--dwd", roots["dwd"], "--output", roots["ads"], "--batch-id", batch_id],
    ]
    for command in commands:
        full = [args.spark_submit, "--master", args.master, str(jobs / command[0]), *command[1:]]
        print("+", " ".join(full), flush=True)
        subprocess.run(full, check=True)


if __name__ == "__main__":
    main()
