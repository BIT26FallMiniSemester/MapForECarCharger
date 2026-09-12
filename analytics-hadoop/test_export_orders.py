import hashlib
import json
from pathlib import Path
import sqlite3
import tempfile
import unittest
from unittest.mock import patch

import export_orders as exporter


class ExportTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.database = self.root / '含空格 数据 #1.db'
        self.db = sqlite3.connect(self.database)
        self.addCleanup(self.db.close)
        migrations = Path(__file__).resolve().parents[1] / 'server-qt' / 'migrations'
        for migration in sorted(migrations.glob('*.sql')):
            self.db.executescript(migration.read_text(encoding='utf-8'))
        self.db.execute("INSERT INTO stations(id,name,address,latitude,longitude,created_at,updated_at) VALUES(1,'测试站','地址',39,116,'2026-09-01T00:00:00Z','2026-09-01T00:00:00Z')")
        self.db.commit()

    def order(self, number=1, status='COMPLETED', created='2026-09-10T15:59:59Z',
              started='2026-09-10T16:00:00Z', paid='2026-09-11T16:00:00Z',
              energy=1500, amount=225):
        self.db.execute(
            'INSERT INTO charging_orders(id,order_no,user_id,station_id,pile_id,status,'
            'price_cents_per_kwh,created_at,updated_at,started_at,paid_at,energy_wh,amount_cents) '
            'VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?)',
            (number, 'private-order-number-' + str(number), number, 1, number, status,
             150, created, created, started, paid, energy, amount))
        self.db.commit()

    def read_bundle(self):
        directory = exporter.export_bundle(self.database, self.root / 'exports')
        raw = (directory / 'orders.jsonl').read_bytes()
        rows = [json.loads(line) for line in raw.splitlines()]
        meta = json.loads((directory / 'metadata.json').read_text(encoding='utf-8'))
        self.assertEqual(meta['input_orders'], len(rows))
        self.assertEqual(meta['orders_sha256'], hashlib.sha256(raw).hexdigest())
        return rows, meta

    def test_empty_database(self):
        rows, meta = self.read_bundle()
        self.assertEqual(rows, [])
        self.assertEqual(meta['stations'][0]['station_name'], '测试站')

    def test_dates_units_privacy_and_database_unchanged(self):
        self.order()
        before = self.database.read_bytes()
        rows, meta = self.read_bundle()
        self.assertEqual(self.database.read_bytes(), before)
        self.assertEqual(rows[0]['created_day'], '2026-09-10')
        self.assertEqual(rows[0]['started_day'], '2026-09-11')
        self.assertEqual(rows[0]['paid_day'], '2026-09-12')
        self.assertEqual(rows[0]['amount_cents'], 225)
        self.assertEqual(rows[0]['energy_wh'], 1500)
        self.assertEqual(set(rows[0]), {'station_id','status','created_at','started_at',
            'paid_at','created_day','started_day','paid_day','energy_wh','amount_cents'})
        self.assertEqual(meta['timezone'], 'Asia/Shanghai')

    def test_unpaid_and_null_numbers_preserved_as_zero(self):
        self.order(status='UNPAID', paid=None, energy=None, amount=None)
        rows, _ = self.read_bundle()
        self.assertEqual(rows[0]['status'], 'UNPAID')
        self.assertIsNone(rows[0]['paid_day'])
        self.assertEqual(rows[0]['amount_cents'], 0)

    def test_all_statuses_and_old_orders_exported(self):
        for number, status in enumerate(sorted(exporter.STATUSES), 1):
            self.order(number=number, status=status, created='2020-01-01T00:00:00Z')
        rows, _ = self.read_bundle()
        self.assertEqual(len(rows), 6)
        self.assertEqual({row['status'] for row in rows}, exporter.STATUSES)

    def test_invalid_timestamp_does_not_publish_partial_batch(self):
        self.order(created='2026-09-10T15:00:00')
        with self.assertRaises(ValueError):
            self.read_bundle()
        self.assertEqual(list((self.root / 'exports').iterdir()), [])

    def test_missing_database_not_created(self):
        missing = self.root / 'missing.db'
        with self.assertRaises(sqlite3.OperationalError):
            exporter.export_bundle(missing, self.root / 'exports')
        self.assertFalse(missing.exists())

    def test_completed_without_paid_time_rejected(self):
        self.order(paid=None)
        with self.assertRaises(ValueError):
            self.read_bundle()

    def test_existing_output_is_not_overwritten(self):
        target = self.root / 'existing.jsonl'
        target.write_text('keep', encoding='utf-8')
        with self.assertRaises(FileExistsError):
            exporter.export_snapshot(self.database, target)
        self.assertEqual(target.read_text(), 'keep')

    def test_wal_committed_data_visible_without_changing_db_or_wal(self):
        self.db.execute('PRAGMA journal_mode=WAL')
        self.order()
        wal = Path(str(self.database) + '-wal')
        before = self.database.read_bytes(), wal.read_bytes()
        rows, _ = self.read_bundle()
        self.assertEqual(len(rows), 1)
        self.assertEqual((self.database.read_bytes(), wal.read_bytes()), before)

    def test_concurrent_payment_uses_consistent_snapshot(self):
        self.db.execute('PRAGMA journal_mode=WAL')
        self.order(number=1)
        self.order(number=2, status='UNPAID', paid=None)
        original = exporter.record_for
        changed = False
        def concurrent_write(row):
            nonlocal changed
            if not changed:
                self.db.execute("UPDATE charging_orders SET status='COMPLETED',paid_at='2026-09-12T00:00:00Z' WHERE id=2")
                self.db.execute("UPDATE stations SET name='付款后的站名' WHERE id=1")
                self.db.commit()
                changed = True
            return original(row)
        with patch.object(exporter, 'record_for', side_effect=concurrent_write):
            rows, meta = self.read_bundle()
        self.assertEqual(rows[1]['status'], 'UNPAID')
        self.assertEqual(meta['stations'][0]['station_name'], '测试站')
        later, later_meta = self.read_bundle()
        self.assertEqual(later[1]['status'], 'COMPLETED')
        self.assertEqual(later_meta['stations'][0]['station_name'], '付款后的站名')


if __name__ == '__main__':
    unittest.main()
