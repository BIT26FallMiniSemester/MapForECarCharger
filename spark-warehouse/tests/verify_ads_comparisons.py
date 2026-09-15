"""Check both two-dimensional ADS comparisons against cleaned DWD facts."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "jobs"))
from common import batch_table_path, create_spark, validate_batch_id


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--warehouse", required=True)
    parser.add_argument("--batch-id", required=True)
    parser.add_argument("--master", default="local[2]")
    args = parser.parse_args()
    batch = validate_batch_id(args.batch_id)
    root = args.warehouse.rstrip("/")
    spark = create_spark("MapForECar-ADS-verification", args.master)
    try:
        from pyspark.sql import functions as F
        def read(layer, name, json_output=False):
            path = batch_table_path(f"{root}/{layer}", name, batch)
            return spark.read.json(f"{path}/json") if json_output else spark.read.parquet(path)

        orders = read("dwd", "dwd_charging_order_detail")
        districts = read("ads", "ads_district_charge_type_30d", True)
        hours = read("ads", "ads_day_type_hour_30d", True)
        data_as_of = districts.select("data_as_of").first()[0]
        window = orders.where((F.col("status") == "COMPLETED") &
                              (F.col("biz_date") > F.date_sub(F.lit(data_as_of), 30)))
        def measures(frame):
            row = frame.agg(F.sum("order_count"), F.sum("energy_wh"), F.sum("revenue_cents")).first()
            return tuple(int(value or 0) for value in row)
        expected = window.agg(F.count("*"), F.sum("energy_wh"), F.sum("amount_cents")).first()
        expected = tuple(int(value or 0) for value in expected)
        district_actual = measures(districts)
        hour_actual = measures(hours)
        if district_actual != expected or hour_actual != expected:
            station_ids = read("dwd", "dim_station").select("station_id")
            piles = read("dwd", "dim_pile").select("pile_id", "station_id")
            unmatched_station = window.join(station_ids, "station_id", "left_anti").count()
            unmatched_pile = window.join(piles, ["pile_id", "station_id"], "left_anti").count()
            raise AssertionError({"expected_dwd": expected, "district_charge_type": district_actual,
                                  "day_type_hour": hour_actual, "unmatched_station": unmatched_station,
                                  "unmatched_pile": unmatched_pile})
        issues = read("quality-reports", "details").where(F.col("rule_id") == "DQ018").select("row_id")
        quarantined = read("dwd", "dwd_quarantine").where(F.col("table_name") == "charging_orders").select("row_id")
        unquarantined = issues.join(quarantined, "row_id", "left_anti").count()
        if unquarantined:
            raise AssertionError(f"{unquarantined} DQ018 orders escaped quarantine")
        print(json.dumps({"batch_id": batch, "cleaned_completed_orders_30d": expected[0],
                          "district_charge_type": district_actual,
                          "day_type_hour": hour_actual,
                          "dq018_issues": issues.count(), "dq018_unquarantined": unquarantined}))
    finally:
        spark.stop()


if __name__ == "__main__":
    main()
