"""Qt-schema regression and optional real Hadoop/Qt HTTP acceptance.

python3 ml/tests/test_qt_pipeline.py --server /path/to/charger-server --work-dir /new/output
Append --streaming-jar /path/to/hadoop-streaming.jar to require a real YARN job.
"""
import argparse
from contextlib import closing
import csv
import json
import os
from pathlib import Path
import socket
import sqlite3
import subprocess
import sys
import tempfile
import time
import unittest
from datetime import datetime, timezone
from urllib.request import urlopen
from urllib.error import HTTPError

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / 'ml/src'))
sys.path.insert(0, str(REPO / 'analytics-hadoop'))
from prepare_history import generate
from qt_pipeline import export, run
from export_orders import export_bundle


def fixture(root):
    end = int(time.time()) // 3600 * 3600
    generate(root / 'raw', end=end)
    catalog = json.loads((root / 'raw/catalog.json').read_text())
    stamp = lambda value: datetime.fromtimestamp(int(value), timezone.utc).isoformat()
    database = root / 'fixture.db'
    with closing(sqlite3.connect(database)) as db, db:
        db.executescript((REPO / 'server-qt/migrations/001_initial.sql').read_text())
        db.executescript((REPO / 'server-qt/migrations/002_catalog_details.sql').read_text())
        db.execute('CREATE TABLE schema_migrations(version INTEGER PRIMARY KEY,applied_at TEXT NOT NULL)')
        db.executemany('INSERT INTO schema_migrations VALUES(?,?)', [(1, stamp(end)), (2, stamp(end))])
        db.execute("INSERT INTO users(id,phone,nickname,created_at,updated_at) VALUES(1,'13800000000','PRIVATE',?,?)",
                   (stamp(end), stamp(end)))
        for row in catalog['stations']:
            # Nonmatching/noncontiguous IDs catch accidental catalog-index mapping.
            db.execute('INSERT INTO stations(id,name,address,latitude,longitude,created_at,updated_at) VALUES(?,?,?,?,?,?,?)',
                       (row['id'] * 101, row['name'], 'synthetic', row['latitude'], row['longitude'], stamp(end), stamp(end)))
        for row in catalog['charging_piles']:
            db.execute("INSERT INTO charging_piles(id,station_id,pile_no,charge_type,rated_power_w,created_at,updated_at) VALUES(?,?,?,'SLOW',?,?,?)",
                       (row['id'], row['station_id'] * 101, str(row['id']), row['rated_power_w'], stamp(end), stamp(end)))
        with (root / 'raw/orders.csv').open() as source:
            for row in csv.DictReader(source):
                db.execute("INSERT INTO charging_orders(id,order_no,user_id,station_id,pile_id,status,price_cents_per_kwh,started_at,stopped_at,energy_wh,amount_cents,paid_at,created_at,updated_at) VALUES(?,?,1,?,?,'COMPLETED',100,?,?,?,?,?,?,?)",
                           (int(row['id']), 'PRIVATE-' + row['id'], int(row['station_id']) * 101, int(row['pile_id']),
                            stamp(row['started_at']), stamp(row['stopped_at']), int(row['energy_wh']),
                            int(row['energy_wh']) // 10, stamp(row['stopped_at']), stamp(row['started_at']), stamp(row['stopped_at'])))
        with (root / 'raw/devices.csv').open() as source:
            db.executemany("INSERT INTO pile_status_logs(pile_id,old_status,new_status,reason,created_at) VALUES(?,'IDLE',?,'synthetic',?)",
                           ((int(row['pile_id']), row['status'], stamp(row['reported_at'])) for row in csv.DictReader(source)))
    return database, end - 45 * 86400, end


class QtPipelineTests(unittest.TestCase):
    def test_real_schema_export_and_forecast(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            database, start, end = fixture(root)
            before = database.read_bytes()
            run(database, root / 'ml', root / 'forecast.json', start, end, simulated=True)
            self.assertEqual(before, database.read_bytes())
            payload = json.loads((root / 'forecast.json').read_text())
            self.assertEqual({point['station_id'] for point in payload['predictions']}, {101,202,303})
            self.assertEqual(len(payload['predictions']), 72)
            self.assertTrue(payload['simulated'])
            self.assertEqual(payload['forecast_origin_epoch'], end - 3600)
            exported = (root / 'ml/data/orders.csv').read_text()
            self.assertNotIn('PRIVATE', exported)
            self.assertNotIn('user_id', exported)
            self.assertNotIn('13800000000', exported)
            published = (root / 'forecast.json').read_bytes()
            with closing(sqlite3.connect(database)) as db, db:
                db.execute('UPDATE charging_orders SET energy_wh=999999999 WHERE id=1')
            with self.assertRaisesRegex(ValueError, 'Rejected'):
                run(database, root / 'invalid', root / 'forecast.json', start, end)
            self.assertEqual(published, (root / 'forecast.json').read_bytes())
            with closing(sqlite3.connect(database)) as db, db:
                db.execute("UPDATE charging_orders SET stopped_at=NULL WHERE id=1")
            with self.assertRaisesRegex(ValueError, 'Incomplete'):
                export(database, root / 'incomplete', start, end)
            with closing(sqlite3.connect(database)) as db, db:
                db.execute('DELETE FROM charging_orders')
            with self.assertRaisesRegex(ValueError, 'No completed charging history'):
                run(database, root / 'empty', root / 'forecast.json', start, end)
            self.assertEqual(published, (root / 'forecast.json').read_bytes())


def acceptance(args):
    root = args.work_dir.resolve()
    root.mkdir(parents=True, exist_ok=False)
    database, start, end = fixture(root)
    run(database, root / 'ml', root / 'forecast.json', start, end, simulated=True)
    snapshot = export_bundle(database, root / 'exports')
    command = [sys.executable, str(REPO / 'analytics-hadoop/pipeline.py'), '--snapshot', str(snapshot),
               '--output', str(root / 'analytics.json'), '--mode', 'hadoop' if args.streaming_jar else 'local']
    if args.streaming_jar:
        command += ['--streaming-jar', args.streaming_jar, '--hdfs-root', '/user/charger/acceptance']
    subprocess.run(command, check=True)
    subprocess.run([sys.executable, str(REPO / 'analytics-hadoop/verify_result.py'),
                    '--snapshot', str(snapshot), '--result', str(root / 'analytics.json')], check=True)
    # Reserve distinct free ports until process dispatch, avoiding known running services.
    with socket.socket() as tcp, socket.socket() as http:
        tcp.bind(('127.0.0.1', 0)); http.bind(('127.0.0.1', 0))
        tcp_port, http_port = tcp.getsockname()[1], http.getsockname()[1]
    env = {**os.environ, 'ML_RESULT_PATH': str(root / 'forecast.json'),
           'ANALYTICS_RESULT_PATH': str(root / 'analytics.json')}
    with (root / 'qt.log').open('w') as log:
        server = subprocess.Popen([str(args.server.resolve()), '--database', str(database), '--host', '127.0.0.1',
                                   '--port', str(tcp_port), '--dashboard-port', str(http_port)],
                                  env=env, stdout=log, stderr=log)
        def get(path):
            try:
                response = urlopen(f'http://127.0.0.1:{http_port}' + path, timeout=5)
            except HTTPError as error:
                response = error
            with response:
                return response.status, json.load(response)
        try:
            for _ in range(100):
                if server.poll() is not None:
                    raise RuntimeError('Qt exited; inspect qt.log')
                try:
                    if get('/api/predictions')[0] == 200:
                        break
                except OSError:
                    pass
                time.sleep(.1)
            else:
                raise RuntimeError('Qt startup timed out')
            for endpoint, name in (('/api/analytics', 'analytics.json'), ('/api/predictions', 'forecast.json')):
                status, body = get(endpoint)
                assert status == 200 and body['available'] and not body['stale'], body
                expected = json.loads((root / name).read_text())
                assert body['data'] == expected
                original = (root / name).read_bytes()
                (root / name).write_text('{}')
                assert get(endpoint)[0] == 503
                (root / name).unlink()
                assert get(endpoint)[1]['reason'] == 'missing_result'
                (root / name).write_bytes(original)
                assert get(endpoint)[0] == 200
            old = json.loads((root / 'forecast.json').read_text())
            old['forecast_origin_epoch'] -= 86400
            for point in old['predictions']:
                point['predicted_for_epoch'] -= 86400
            (root / 'forecast.json').write_text(json.dumps(old))
            assert get('/api/predictions')[1]['stale']
            (root / 'forecast.json').write_bytes(original)
            result = {'passed': True, 'engine': json.loads((root / 'analytics.json').read_text())['engine'],
                      'ml_points': 72, 'station_ids': [101,202,303], 'http_exact_readback': True,
                      'missing_invalid_recovery': True, 'stale_forecast': True, 'simulated': True}
            (root / 'acceptance.json').write_text(json.dumps(result, indent=2) + '\n')
            print(json.dumps(result))
        finally:
            server.terminate()
            try:
                server.wait(timeout=10)
            except subprocess.TimeoutExpired:
                server.kill(); server.wait()


if __name__ == '__main__':
    if '--server' in sys.argv:
        parser = argparse.ArgumentParser(description=__doc__)
        parser.add_argument('--server', required=True, type=Path)
        parser.add_argument('--work-dir', required=True, type=Path)
        parser.add_argument('--streaming-jar')
        acceptance(parser.parse_args())
    else:
        unittest.main()
