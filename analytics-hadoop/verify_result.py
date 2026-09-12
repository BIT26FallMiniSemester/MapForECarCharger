"""Verify published statistics against independent SQLite queries on the input batch."""
import argparse
from datetime import timedelta
import json
from pathlib import Path
import sqlite3

from pipeline import read_snapshot


def verify(batch, result_file):
    source, metadata, today = read_snapshot(batch)
    result = json.loads(Path(result_file).read_text(encoding='utf-8'))
    checks = 0
    def equal(actual, expected, label):
        nonlocal checks
        if actual != expected:
            raise ValueError(f'Mismatch: {label}')
        checks += 1
    db = sqlite3.connect(':memory:')
    try:
        db.executescript('CREATE TABLE orders(station_id INTEGER,status TEXT,created_at TEXT,started_at TEXT,paid_at TEXT,amount_cents INTEGER,energy_wh INTEGER); CREATE TABLE stations(id INTEGER,name TEXT);')
        with source.open(encoding='utf-8') as stream:
            db.executemany('INSERT INTO orders VALUES(?,?,?,?,?,?,?)',
                (tuple(row[field] for field in ('station_id','status','created_at','started_at','paid_at','amount_cents','energy_wh'))
                 for row in map(json.loads, stream)))
        db.executemany('INSERT INTO stations VALUES(?,?)',
                       ((row['station_id'],row['station_name']) for row in metadata['stations']))
        equal(result['source_sha256'], metadata['orders_sha256'], 'source hash')
        equal(result['source_batch_id'], metadata['batch_id'], 'source batch')
        equal(result['input_orders'], db.execute('SELECT count(*) FROM orders').fetchone()[0], 'order count')
        start = (today - timedelta(days=29)).isoformat()
        equal(result['window_start'], start, 'window start')
        equal(result['window_end'], today.isoformat(), 'window end')
        expected = []
        for offset in range(30):
            day = (today - timedelta(days=29-offset)).isoformat()
            count = db.execute("SELECT count(*) FROM orders WHERE date(created_at,'+8 hours')=?", (day,)).fetchone()[0]
            energy = db.execute("SELECT coalesce(sum(energy_wh),0) FROM orders WHERE date(started_at,'+8 hours')=?", (day,)).fetchone()[0]
            revenue = db.execute("SELECT coalesce(sum(amount_cents),0) FROM orders WHERE status='COMPLETED' AND date(paid_at,'+8 hours')=?", (day,)).fetchone()[0]
            expected.append(dict(date=day, order_count=count, energy_wh=energy, revenue_cents=revenue))
        equal(result['revenue_trend'], {'days':30,'items':expected}, 'daily trend')
        ranking = db.execute("SELECT s.id,s.name,coalesce(sum(o.amount_cents),0),count(o.station_id) FROM stations s LEFT JOIN orders o ON o.station_id=s.id AND o.status='COMPLETED' AND date(o.paid_at,'+8 hours') BETWEEN ? AND ? GROUP BY s.id,s.name ORDER BY 3 DESC,s.id LIMIT 10", (start,today.isoformat())).fetchall()
        equal(result['station_ranking'], {'days':30,'items':[dict(zip(('station_id','station_name','revenue_cents','order_count'), row)) for row in ranking]}, 'station ranking')
        equal(result['total_revenue_cents'], db.execute("SELECT coalesce(sum(amount_cents),0) FROM orders WHERE status='COMPLETED'").fetchone()[0], 'total revenue')
    finally:
        db.close()
    return {'verified': True, 'checks': checks, 'input_orders': result['input_orders'],
            'engine': result['engine'], 'run_id': result['run_id']}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--snapshot', required=True, type=Path)
    parser.add_argument('--result', required=True, type=Path)
    args = parser.parse_args()
    print(json.dumps(verify(args.snapshot, args.result)))
