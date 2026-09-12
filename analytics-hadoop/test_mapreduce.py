from datetime import date, timedelta
import hashlib
import json
from pathlib import Path
import sqlite3
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

from export_orders import export_bundle
from mapper import metrics
from pipeline import hadoop_job, local_job, materialize, read_snapshot

HERE = Path(__file__).resolve().parent
TODAY = date(2026, 9, 12)


class MapReduceTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.database = self.root / 'source.db'
        self.db = sqlite3.connect(self.database)
        self.addCleanup(self.db.close)
        for migration in sorted((HERE.parent / 'server-qt/migrations').glob('*.sql')):
            self.db.executescript(migration.read_text(encoding='utf-8'))
        for station in range(1, 14):
            self.db.execute('INSERT INTO stations(id,name,address,latitude,longitude,status,created_at,updated_at) VALUES(?,?,?,?,?,?,?,?)',
                (station, f'站点{station}', '地址', 39, 116, 'INACTIVE' if station == 13 else 'ACTIVE',
                 '2026-01-01T00:00:00Z', '2026-01-01T00:00:00Z'))
        self.db.commit()
        self.sequence = 0

    def order(self, station=1, status='COMPLETED', created='2026-09-12T00:00:00Z',
              started='2026-09-12T00:00:00Z', paid='2026-09-12T00:00:00Z', amount=150, energy=1000):
        self.sequence += 1
        self.db.execute('INSERT INTO charging_orders(id,order_no,user_id,station_id,pile_id,status,'
                        'price_cents_per_kwh,created_at,updated_at,started_at,paid_at,amount_cents,energy_wh) '
                        'VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?)',
                        (self.sequence, str(self.sequence), self.sequence, station, self.sequence, status,
                         150, created, created, started, paid, amount, energy))
        self.db.commit()

    def snapshot(self):
        with patch('export_orders.now', return_value='2026-09-12T03:00:00+00:00'):
            batch = export_bundle(self.database, self.root / 'exports')
        return batch

    def result(self):
        source, metadata, today = read_snapshot(self.snapshot())
        return materialize(local_job(source, today), metadata, today, 'local-mapreduce', 'test')

    def test_matches_sqlite_for_every_day_and_top_ten(self):
        # Before/start/end/after window, midnight UTC+8 and month boundary.
        for timestamp in ['2026-08-13T15:59:59Z', '2026-08-13T16:00:00Z',
                          '2026-08-31T16:00:00Z', '2026-09-12T15:59:59Z', '2026-09-12T16:00:00Z']:
            self.order(created=timestamp, started=timestamp, paid=timestamp)
        for station in range(1, 14):
            self.order(station=station, amount=900 if station == 13 else 300)
        for status in ['PENDING','RESERVED','CHARGING','UNPAID','CANCELLED']:
            self.order(status=status, paid=None, amount=99999, energy=None)
        self.order(created='2020-01-01T00:00:00Z', started='2020-01-01T00:00:00Z', amount=77)
        self.order(paid='2020-01-01T00:00:00Z', amount=88)
        result = self.result()
        for offset, actual in enumerate(result['revenue_trend']['items']):
            day = (TODAY - timedelta(days=29) + timedelta(days=offset)).isoformat()
            count = self.db.execute("SELECT count(*) FROM charging_orders WHERE date(created_at,'+8 hours')=?", (day,)).fetchone()[0]
            energy = self.db.execute("SELECT coalesce(sum(energy_wh),0) FROM charging_orders WHERE date(started_at,'+8 hours')=?", (day,)).fetchone()[0]
            revenue = self.db.execute("SELECT coalesce(sum(amount_cents),0) FROM charging_orders WHERE status='COMPLETED' AND date(paid_at,'+8 hours')=?", (day,)).fetchone()[0]
            self.assertEqual(actual, {'date':day,'order_count':count,'energy_wh':energy,'revenue_cents':revenue})
        expected = self.db.execute("SELECT s.id,s.name,coalesce(sum(o.amount_cents),0),count(o.id) FROM stations s LEFT JOIN charging_orders o ON o.station_id=s.id AND o.status='COMPLETED' AND date(o.paid_at,'+8 hours') BETWEEN '2026-08-14' AND '2026-09-12' WHERE s.status='ACTIVE' GROUP BY s.id ORDER BY 3 DESC,s.id LIMIT 10").fetchall()
        self.assertEqual(result['station_ranking']['items'], [dict(zip(['station_id','station_name','revenue_cents','order_count'], row)) for row in expected])
        self.assertEqual(result['total_revenue_cents'], self.db.execute("SELECT sum(amount_cents) FROM charging_orders WHERE status='COMPLETED'").fetchone()[0])

    def test_empty_input_zero_fills_30_days(self):
        result = self.result()
        self.assertEqual(len(result['revenue_trend']['items']), 30)
        self.assertTrue(all(item['revenue_cents'] == item['order_count'] == item['energy_wh'] == 0 for item in result['revenue_trend']['items']))
        self.assertEqual([item['station_id'] for item in result['station_ranking']['items']], list(range(1, 11)))

    def test_mapper_combiner_two_reducers_through_stdio(self):
        for i in range(15):
            self.order(station=i % 12 + 1, amount=i * 73)
        source, metadata, today = read_snapshot(self.snapshot())
        records = source.read_text(encoding='utf-8').splitlines()
        partitions = [[], []]
        def run(script, value, *args):
            return subprocess.run([sys.executable, str(HERE / script), *args], input=value,
                text=True, encoding='utf-8', capture_output=True, check=True).stdout.splitlines()
        for index in range(3):
            mapped = run('mapper.py', '\n'.join(records[index::3]) + '\n', '--today', str(today))
            combined = run('reducer.py', '\n'.join(sorted(mapped)) + '\n')
            for line in combined:
                key = line.split('\t')[0]
                bucket = int(hashlib.sha256(key.encode()).hexdigest(),16) % 2
                partitions[bucket].append(line)
        values = {}
        for partition in partitions:
            for line in run('reducer.py', '\n'.join(sorted(partition)) + '\n'):
                key, value = line.split('\t')
                values[key] = int(value)
        self.assertEqual(values, local_job(source, today))
        self.assertEqual(materialize(values, metadata, today, 'local-mapreduce', 'test')['input_orders'], 15)

    def test_checksum_rejects_changed_snapshot(self):
        batch = self.snapshot()
        (batch / 'orders.jsonl').write_text('{}\n', encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'checksum'):
            read_snapshot(batch)

    def test_missing_mapper_rows_rejected(self):
        self.order()
        _, metadata, today = read_snapshot(self.snapshot())
        with self.assertRaisesRegex(ValueError, 'input count'):
            materialize({}, metadata, today, 'local-mapreduce', 'test')

    def test_cli_reads_existing_snapshot_and_publishes(self):
        self.order()
        batch = self.snapshot()
        target = self.root / 'result.json'
        command = [sys.executable, str(HERE / 'pipeline.py'), '--snapshot', str(batch),
                   '--output', str(target), '--mode', 'local']
        subprocess.run(command, check=True, capture_output=True)
        result = json.loads(target.read_text(encoding='utf-8'))
        self.assertEqual(result['engine'], 'local-mapreduce')
        self.assertEqual(result['window_end'], '2026-09-12')
        before = target.read_bytes()
        (batch / 'orders.jsonl').write_text('invalid', encoding='utf-8')
        failed = subprocess.run(command, capture_output=True)
        self.assertNotEqual(failed.returncode, 0)
        self.assertEqual(target.read_bytes(), before)
        self.assertFalse(target.with_name('result.json.lock').exists())

    def test_hadoop_submission_and_all_parts_read(self):
        source, _, today = read_snapshot(self.snapshot())
        jar = self.root / 'streaming.jar'
        jar.touch()
        args = SimpleNamespace(streaming_jar=str(jar), hdfs_root='/user/charger/dashboard')
        calls = []
        def fake_command(command, **kwargs):
            calls.append(command)
            return SimpleNamespace(stdout='input_orders\t2\nrevenue:2026-09-12\t150\n')
        with patch('pipeline.command', side_effect=fake_command):
            values, root = hadoop_job(source, today, 'test-run', args)
        self.assertEqual(values['input_orders'], 2)
        job = next(command for command in calls if command[0] == 'hadoop')
        self.assertIn('mapreduce.framework.name=yarn', job)
        self.assertLess(job.index('-files'), job.index('-input'))
        self.assertEqual(calls[-2], ['hdfs','dfs','-test','-e',root + '/output/_SUCCESS'])
        self.assertEqual(calls[-1][-1], root + '/output/part-*')

    def test_invalid_mapper_record_fails(self):
        self.order()
        source, _, today = read_snapshot(self.snapshot())
        row = json.loads(source.read_text(encoding='utf-8'))
        row['amount_cents'] = -1
        with self.assertRaises(ValueError):
            list(metrics(row, today))


if __name__ == '__main__':
    unittest.main()
