"""Explicit raw schemas shared by ODS ingestion and quality jobs.

ODS deliberately keeps every source value as text. Casting belongs to DWD so
that malformed source values remain observable by the quality job.
"""
from __future__ import annotations

TABLE_COLUMNS = {
    "users": ["row_id", "id", "phone", "nickname", "avatar_id", "balance_cents", "status", "created_at", "updated_at"],
    "stations": ["row_id", "id", "name", "address", "latitude", "longitude", "price_cents_per_kwh", "operator_name", "district", "data_source", "external_id", "status", "service_type", "region_scope", "location_type", "fast_connector_count", "slow_connector_count", "created_at", "updated_at"],
    "charging_piles": ["row_id", "id", "station_id", "pile_no", "charge_type", "rated_power_w", "status", "reserved_order_id", "created_at", "updated_at"],
    "charging_orders": ["row_id", "id", "order_no", "user_id", "station_id", "pile_id", "status", "price_cents_per_kwh", "reserved_at", "expires_at", "started_at", "stopped_at", "duration_seconds", "energy_wh", "amount_cents", "paid_at", "cancelled_at", "created_at", "updated_at"],
    "recharge_records": ["row_id", "id", "user_id", "client_request_id", "amount_cents", "balance_after_cents", "created_at"],
    "pile_status_logs": ["row_id", "id", "pile_id", "order_id", "old_status", "new_status", "reason", "created_at"],
}


def raw_schema(table: str):
    """Return a non-nullable-header, nullable-value StringType schema."""
    from pyspark.sql.types import StringType, StructField, StructType

    try:
        columns = TABLE_COLUMNS[table]
    except KeyError as error:
        raise ValueError(f"unknown table: {table}") from error
    return StructType([StructField(column, StringType(), True) for column in columns])
