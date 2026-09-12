"""Read-only SQLite export, actual Hadoop job submission, atomic publication.

Python 3.9+, standard library only. Local mode is explicitly a development mode.
"""
import argparse
from datetime import date, datetime, timedelta, timezone
import hashlib
import json
import os
from pathlib import Path
import sqlite3
import subprocess
import sys
import uuid

from mapper import metrics
from reducer import reduce_lines

from export_orders import SHANGHAI, now, export_bundle


def read_snapshot(directory):
    directory = Path(directory).resolve()
    metadata = json.loads((directory / 'metadata.json').read_text(encoding='utf-8'))
    if (metadata.get('schema_version') != 1 or metadata.get('orders_file') != 'orders.jsonl'
            or metadata.get('timezone') != 'Asia/Shanghai' or metadata.get('scope') != 'all_orders'):
        raise ValueError('Unsupported snapshot contract')
    if type(metadata.get('input_orders')) is not int or metadata['input_orders'] < 0:
        raise ValueError('Invalid snapshot input count')
    timestamp = datetime.fromisoformat(metadata['snapshot_at'].replace('Z', '+00:00'))
    if timestamp.tzinfo is None:
        raise ValueError('Snapshot timestamp requires a timezone')
    today = timestamp.astimezone(SHANGHAI).date()
    source = directory / 'orders.jsonl'
    digest = hashlib.sha256()
    count = 0
    with source.open('rb') as stream:
        for line in stream:
            digest.update(line)
            count += 1
    if digest.hexdigest() != metadata['orders_sha256'] or count != metadata['input_orders']:
        raise ValueError('Snapshot checksum or row count mismatch')
    station_ids = set()
    for station in metadata['stations']:
        identifier = station['station_id']
        if type(identifier) is not int or identifier in station_ids or not isinstance(station['station_name'], str):
            raise ValueError('Invalid or duplicate station metadata')
        station_ids.add(identifier)
    return source, metadata, today


def local_job(source, today):
    # Small-fixture verification only; production sorting/shuffle belongs to Hadoop.
    lines = []
    with source.open(encoding='utf-8') as stream:
        for line in stream:
            lines.extend(f'{key}\t{value}' for key, value in metrics(json.loads(line), today))
    return dict(reduce_lines(sorted(lines)))


def command(args, **kwargs):
    return subprocess.run(args, check=True, **kwargs)


def hadoop_job(source, today, run_id, args):
    if not args.streaming_jar or not Path(args.streaming_jar).is_file():
        raise ValueError('--streaming-jar must point to the installed hadoop-streaming jar')
    if not args.hdfs_root.startswith('/') or any(c in args.hdfs_root for c in '*?[]\n'):
        raise ValueError('--hdfs-root must be an absolute HDFS path without glob characters')
    root = args.hdfs_root.rstrip('/') + '/' + run_id
    command(['hdfs', 'dfs', '-mkdir', '-p', root + '/input'])
    command(['hdfs', 'dfs', '-put', str(source), root + '/input/orders.jsonl'])
    # Keep the source manifest alongside, but outside the mapper input directory.
    command(['hdfs', 'dfs', '-put', str(source.with_name('metadata.json')), root + '/metadata.json'])
    scripts = Path(__file__).resolve().parent
    files = ','.join((scripts / name).as_uri() for name in ('mapper.py', 'reducer.py'))
    command(['hadoop', 'jar', str(Path(args.streaming_jar).resolve()),
             '-D', 'mapreduce.framework.name=yarn',
             '-D', 'mapreduce.job.reduces=2',
             '-D', 'mapreduce.job.name=charger-dashboard-' + run_id,
             '-files', files,
             '-input', root + '/input', '-output', root + '/output',
             '-mapper', 'python3 mapper.py --today ' + today.isoformat(),
             '-combiner', 'python3 reducer.py', '-reducer', 'python3 reducer.py'])
    # Read all reducers, never just part-00000; fail if job completion is absent.
    command(['hdfs', 'dfs', '-test', '-e', root + '/output/_SUCCESS'])
    result = command(['hdfs', 'dfs', '-cat', root + '/output/part-*'],
                     stdout=subprocess.PIPE, encoding='utf-8')
    values = {}
    for line in result.stdout.splitlines():
        key, value = line.split('\t', 1)
        values[key] = values.get(key, 0) + int(value)
    return values, root


def materialize(values, metadata, today, engine, run_id, hdfs_path=None):
    if values.get('input_orders', 0) != metadata['input_orders']:
        raise ValueError('MapReduce input count differs from exported snapshot')
    start = today - timedelta(days=29)
    trend = []
    for offset in range(30):
        day = (start + timedelta(days=offset)).isoformat()
        trend.append({'date': day, 'order_count': values.get('orders:' + day, 0),
                      'energy_wh': values.get('energy:' + day, 0),
                      'revenue_cents': values.get('revenue:' + day, 0)})
    ranking = [{**station,
                'revenue_cents': values.get('station_revenue:' + str(station['station_id']), 0),
                'order_count': values.get('station_orders:' + str(station['station_id']), 0)}
               for station in metadata['stations']]
    ranking.sort(key=lambda row: (-row['revenue_cents'], row['station_id']))
    return {
        'schema_version': 1, 'engine': engine, 'run_id': run_id,
        'snapshot_at': metadata['snapshot_at'], 'generated_at': now(),
        'source_batch_id': metadata['batch_id'], 'source_sha256': metadata['orders_sha256'],
        'timezone': 'Asia/Shanghai', 'window_start': start.isoformat(),
        'window_end': today.isoformat(), 'input_orders': metadata['input_orders'],
        'hdfs_path': hdfs_path, 'total_revenue_cents': values.get('total_revenue', 0),
        'revenue_trend': {'days': 30, 'items': trend},
        'station_ranking': {'days': 30, 'items': ranking[:10]},
    }


def publish(payload, target):
    target.parent.mkdir(parents=True, exist_ok=True)
    temporary = target.with_name(target.name + '.' + uuid.uuid4().hex + '.tmp')
    try:
        with temporary.open('w', encoding='utf-8', newline='\n') as stream:
            json.dump(payload, stream, ensure_ascii=False, allow_nan=False)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, target)
    finally:
        temporary.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    inputs = parser.add_mutually_exclusive_group(required=True)
    inputs.add_argument('--database', type=Path, help='Export a fresh read-only snapshot')
    inputs.add_argument('--snapshot', type=Path, help='Use a completed export batch directory')
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--work-dir', type=Path, default=Path('analytics-hadoop/runtime/exports'))
    parser.add_argument('--mode', choices=['local', 'hadoop'], default='hadoop')
    parser.add_argument('--streaming-jar')
    parser.add_argument('--hdfs-root', default='/user/charger/dashboard')
    args = parser.parse_args()
    run_id = datetime.now(timezone.utc).strftime('%Y%m%dT%H%M%S') + '-' + uuid.uuid4().hex[:12]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    # Prevent a slow, older job from replacing newer published results.
    lock = args.output.with_name(args.output.name + '.lock')
    with lock.open('x', encoding='utf-8') as stream:
        stream.write(str(os.getpid()))
    try:
        snapshot = args.snapshot or export_bundle(args.database, args.work_dir)
        source, metadata, today = read_snapshot(snapshot)
        if args.output.resolve() in {source.resolve(), source.with_name('metadata.json').resolve(),
                                    args.database.resolve() if args.database else source.resolve()}:
            raise ValueError('Output must not replace the database or snapshot files')
        hdfs_path = None
        if args.mode == 'hadoop':
            values, hdfs_path = hadoop_job(source, today, run_id, args)
        else:
            values = local_job(source, today)
        payload = materialize(values, metadata, today, args.mode + '-mapreduce', run_id, hdfs_path)
        publish(payload, args.output.resolve())
        print(json.dumps({'engine': payload['engine'], 'input_orders': metadata['input_orders'],
                          'run_id': run_id, 'output': str(args.output.resolve())}))
    finally:
        lock.unlink()


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, KeyError, TypeError, sqlite3.Error, subprocess.CalledProcessError) as error:
        sys.exit(f'Analytics failed: {error}')
