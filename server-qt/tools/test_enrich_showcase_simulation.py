import sqlite3
from contextlib import closing
import tempfile
import unittest
from pathlib import Path
from enrich_showcase_simulation import enrich


class SceneTests(unittest.TestCase):
    def test_clone_states_and_constraints(self):
        with tempfile.TemporaryDirectory() as tmp:
            source,target=Path(tmp)/'source.db',Path(tmp)/'rich.db'
            with closing(sqlite3.connect(source)) as db, db:
                db.executescript((Path(__file__).resolve().parents[1]/'migrations/001_initial.sql').read_text(encoding='utf-8'))
                db.execute("INSERT INTO stations(id,name,address,latitude,longitude,price_cents_per_kwh,created_at,updated_at) VALUES(1,'test','test',39,116,150,'2026-01-01','2026-01-01')")
                for i in range(1,31):
                    db.execute("INSERT INTO users(id,phone,nickname,balance_cents,created_at,updated_at) VALUES(?,?,?,200000,'2026-01-01','2026-01-01')",(i,f'187{i:08d}',str(i)))
                for i in range(1,101):
                    db.execute("INSERT INTO charging_piles(id,station_id,pile_no,charge_type,rated_power_w,created_at,updated_at) VALUES(?,1,?,'SLOW',7000,'2026-01-01','2026-01-01')",(i,str(i)))
            original=source.read_bytes()
            report=enrich(source,target)
            self.assertEqual(source.read_bytes(),original)
            self.assertEqual(set(report['pile_states']),{'IDLE','RESERVED','CHARGING','FAULT','OFFLINE'})
            self.assertEqual(set(report['orders_added']),{'PENDING','RESERVED','CHARGING','UNPAID','COMPLETED','CANCELLED'})
            with closing(sqlite3.connect(target)) as db, db:
                self.assertEqual(db.execute('PRAGMA foreign_key_check').fetchall(),[])
                self.assertEqual(db.execute("SELECT count(*) FROM users u JOIN charging_orders o ON o.user_id=u.id WHERE u.status='FROZEN' AND o.status IN ('PENDING','RESERVED','CHARGING','UNPAID')").fetchone()[0],0)
                self.assertEqual(db.execute("SELECT count(*) FROM charging_orders WHERE energy_wh>7000*duration_seconds/3600.0+1 OR paid_at>updated_at").fetchone()[0],0)
            with self.assertRaises(FileExistsError):
                enrich(source,target)


if __name__=='__main__':
    unittest.main()
