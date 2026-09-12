"""Export a consistent, read-only SQLite snapshot. Python 3.9+, stdlib only."""
import argparse
from contextlib import closing
from datetime import datetime, timedelta, timezone
import hashlib
import json
import os
from pathlib import Path
import sqlite3
import tempfile
import uuid

SHANGHAI = timezone(timedelta(hours=8))
STATUSES = {'PENDING', 'RESERVED', 'CHARGING', 'UNPAID', 'COMPLETED', 'CANCELLED'}


def now():
    return datetime.now(timezone.utc).isoformat()


def local_day(value):
    if value is None:
        return None
    parsed = datetime.fromisoformat(value.replace('Z', '+00:00'))
    if parsed.tzinfo is None:
        raise ValueError('Order timestamp must include a timezone')
    return parsed.astimezone(SHANGHAI).date().isoformat()


def record_for(row):
    if row['status'] not in STATUSES:
        raise ValueError('Unsupported order status')
    if row['created_at'] is None:
        raise ValueError('Order creation time is required')
    if row['status'] == 'COMPLETED' and row['paid_at'] is None:
        raise ValueError('Completed order has no payment timestamp')
    for field in ('station_id', 'energy_wh', 'amount_cents'):
        value = row[field]
        if value is not None and (type(value) is not int or value < 0):
            raise ValueError(f'{field} must be a nonnegative integer')
    if row['station_id'] is None:
        raise ValueError('station_id is required')
    return {
        'station_id': row['station_id'], 'status': row['status'],
        'created_at': row['created_at'], 'started_at': row['started_at'],
        'paid_at': row['paid_at'],
        'created_day': local_day(row['created_at']),
        'started_day': local_day(row['started_at']),
        'paid_day': local_day(row['paid_at']),
        'energy_wh': row['energy_wh'] or 0,
        'amount_cents': row['amount_cents'] or 0,
    }


def export_snapshot(database, target):
    """Write a new JSONL file, return metadata from the same read transaction.

    Caller publishes the file and metadata together. Never replace an existing file.
    """
    target = Path(target)
    uri = Path(database).resolve().as_uri() + '?mode=ro'
    digest = hashlib.sha256()
    with closing(sqlite3.connect(uri, uri=True, timeout=10)) as connection:
        connection.row_factory = sqlite3.Row
        connection.execute('PRAGMA query_only=ON')
        connection.execute('BEGIN')
        # The first SELECT pins the snapshot, including committed WAL frames.
        started_at = now()
        stations = [dict(row) for row in connection.execute(
            "SELECT id station_id,name station_name FROM stations WHERE status='ACTIVE' ORDER BY id")]
        count = 0
        with target.open('xb') as output:
            for row in connection.execute(
                'SELECT station_id,status,created_at,started_at,paid_at,energy_wh,amount_cents '
                'FROM charging_orders ORDER BY id'):
                record = record_for(row)
                line = (json.dumps(record, ensure_ascii=False, allow_nan=False) + '\n').encode('utf-8')
                output.write(line)
                digest.update(line)
                count += 1
            output.flush()
            os.fsync(output.fileno())
        connection.rollback()
    return {
        'schema_version': 1, 'snapshot_at': started_at, 'exported_at': now(),
        'timezone': 'Asia/Shanghai', 'input_orders': count, 'stations': stations,
        'orders_file': target.name, 'orders_sha256': digest.hexdigest(),
        'scope': 'all_orders', 'null_numeric_policy': 'zero',
        'units': {'amount_cents': 'cents', 'energy_wh': 'Wh'},
    }


def export_bundle(database, output_dir):
    """Publish a unique directory only after JSONL and metadata are complete."""
    output_dir = Path(output_dir).resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    name = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%SZ-') + uuid.uuid4().hex
    destination = output_dir / name
    with tempfile.TemporaryDirectory(prefix='.export-', dir=output_dir) as temporary:
        staging = Path(temporary) / 'snapshot'
        staging.mkdir()
        metadata = export_snapshot(database, staging / 'orders.jsonl')
        metadata['batch_id'] = name
        with (staging / 'metadata.json').open('x', encoding='utf-8', newline='\n') as stream:
            json.dump(metadata, stream, ensure_ascii=False, indent=2, allow_nan=False)
            stream.flush()
            os.fsync(stream.fileno())
        staging.rename(destination)
    return destination


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--database', required=True, type=Path)
    parser.add_argument('--output-dir', required=True, type=Path)
    args = parser.parse_args()
    try:
        result = export_bundle(args.database, args.output_dir)
    except (OSError, sqlite3.Error, ValueError) as error:
        parser.exit(1, f'Export failed: {error}\n')
    print(str(result))


if __name__ == '__main__':
    main()
