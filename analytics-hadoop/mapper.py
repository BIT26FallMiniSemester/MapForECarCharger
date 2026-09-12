"""Hadoop Streaming mapper: raw order snapshots -> additive metrics."""
import argparse
from datetime import date, timedelta
import json
import sys


def metrics(row, today):
    # Fail the task on malformed input instead of silently publishing partial totals.
    for field in ('station_id', 'energy_wh', 'amount_cents'):
        if type(row[field]) is not int or row[field] < 0:
            raise ValueError(f'Invalid {field}')
    if row['status'] not in {'PENDING', 'RESERVED', 'CHARGING', 'UNPAID', 'COMPLETED', 'CANCELLED'}:
        raise ValueError('Invalid order status')
    for field in ('created_day', 'started_day', 'paid_day'):
        value = row[field]
        if value is not None and date.fromisoformat(value).isoformat() != value:
            raise ValueError(f'Invalid {field}')
    if row['created_day'] is None or (row['status'] == 'COMPLETED' and row['paid_day'] is None):
        raise ValueError('Missing required order date')
    start = (today - timedelta(days=29)).isoformat()
    end = today.isoformat()
    def in_window(value):
        return value is not None and start <= value <= end
    yield 'input_orders', 1
    if in_window(row['created_day']):
        yield 'orders:' + row['created_day'], 1
    if in_window(row['started_day']):
        yield 'energy:' + row['started_day'], row['energy_wh']
    if row['status'] == 'COMPLETED':
        yield 'total_revenue', row['amount_cents']
        if in_window(row['paid_day']):
            yield 'revenue:' + row['paid_day'], row['amount_cents']
            yield 'station_revenue:' + str(row['station_id']), row['amount_cents']
            yield 'station_orders:' + str(row['station_id']), 1


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--today', type=date.fromisoformat, required=True)
    args = parser.parse_args()
    for line in sys.stdin:
        row = json.loads(line)
        for key, value in metrics(row, args.today):
            print(f'{key}\t{value}')
