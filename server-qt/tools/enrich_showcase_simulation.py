"""Clone a demo database and add consistent, diverse current business scenarios."""
from __future__ import annotations
import argparse
from collections import Counter
from contextlib import closing
from datetime import datetime, timedelta, timezone
from pathlib import Path
import random
import sqlite3
import json


def enrich(source, target, seed=20260915):
    source, target = Path(source).resolve(), Path(target).resolve()
    if not source.is_file():
        raise FileNotFoundError(source)
    if target.exists() or target == source:
        raise FileExistsError('Use a new target; the original database is never overwritten.')
    target.parent.mkdir(parents=True, exist_ok=True)
    staging = target.with_name(target.name + '.staging')
    if staging.exists():
        raise FileExistsError(staging)
    rng = random.Random(seed)
    now = datetime.now(timezone.utc)
    stamp = lambda t: t.isoformat(timespec='milliseconds').replace('+00:00', 'Z')
    prefix = f'SCENE-{seed}-{now.strftime("%Y%m%d%H%M%S%f")}'
    try:
        with closing(sqlite3.connect(source.as_uri()+'?mode=ro', uri=True)) as src, closing(sqlite3.connect(staging)) as db:
            src.backup(db)
            db.execute('PRAGMA foreign_keys=ON')
            free_users = [r[0] for r in db.execute("SELECT id FROM users WHERE status='NORMAL' AND phone!='13900000000' AND NOT EXISTS(SELECT 1 FROM charging_orders o WHERE o.user_id=users.id AND o.status IN ('PENDING','RESERVED','CHARGING','UNPAID')) ORDER BY id")]
            piles = list(db.execute("SELECT p.id,p.station_id,p.rated_power_w,s.price_cents_per_kwh FROM charging_piles p JOIN stations s ON s.id=p.station_id WHERE p.status='IDLE' AND p.reserved_order_id IS NULL AND s.status='ACTIVE' AND s.price_cents_per_kwh>0 AND NOT EXISTS(SELECT 1 FROM charging_orders o WHERE o.pile_id=p.id AND o.status IN ('RESERVED','CHARGING')) ORDER BY p.id"))
            rng.shuffle(free_users)
            rng.shuffle(piles)
            if len(free_users)<10 or len(piles)<10:
                raise ValueError('Need at least 10 unused normal users and 10 idle piles.')
            frozen = free_users[:max(1,len(free_users)//10)]
            db.executemany("UPDATE users SET status='FROZEN',updated_at=? WHERE id=?", [(stamp(now),u) for u in frozen])
            available = free_users[len(frozen):]
            for i,user in enumerate(available[:200]):
                amount=rng.choice((5000,10000,20000,50000))
                db.execute('UPDATE users SET balance_cents=balance_cents+?,updated_at=? WHERE id=?',(amount,stamp(now),user))
                balance=db.execute('SELECT balance_cents FROM users WHERE id=?',(user,)).fetchone()[0]
                db.execute('INSERT INTO recharge_records(user_id,client_request_id,amount_cents,balance_after_cents,created_at) VALUES(?,?,?,?,?)',(user,f'{prefix}-R{i}',amount,balance,stamp(now-timedelta(minutes=rng.randint(1,120)))))
            # Leave normal users and idle piles available for manual end-to-end tests.
            active_count = min(len(available)//2, len(piles)//4, 600)
            counts = Counter()
            for i in range(active_count):
                status = ('CHARGING','CHARGING','CHARGING','RESERVED','RESERVED','UNPAID','PENDING')[i%7]
                pile, station, power, price = piles[i]
                user = available[i]
                created = now-timedelta(minutes=rng.randint(3,12))
                reserved = created+timedelta(seconds=30) if status!='PENDING' else None
                started = reserved+timedelta(seconds=30) if status in ('CHARGING','UNPAID') else None
                stopped = now-timedelta(seconds=30) if status=='UNPAID' else None
                duration = int((stopped-started).total_seconds()) if stopped else None
                energy = round(power*rng.uniform(.35,.8)*duration/3600) if stopped else None
                amount = (energy*price+500)//1000 if stopped else None
                order = db.execute('INSERT INTO charging_orders(order_no,user_id,station_id,pile_id,status,price_cents_per_kwh,reserved_at,expires_at,started_at,stopped_at,duration_seconds,energy_wh,amount_cents,created_at,updated_at) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)',
                    (f'{prefix}-{i}',user,station,pile,status,price,stamp(reserved) if reserved else None,stamp(now+timedelta(minutes=15)) if status=='RESERVED' else None,stamp(started) if started else None,stamp(stopped) if stopped else None,duration,energy,amount,stamp(created),stamp(now))).lastrowid
                if reserved:
                    db.execute('INSERT INTO pile_status_logs(pile_id,order_id,old_status,new_status,reason,created_at) VALUES(?,?,?,?,?,?)',(pile,order,'IDLE','RESERVED','SIMULATED_RESERVE',stamp(reserved)))
                if started:
                    db.execute('INSERT INTO pile_status_logs(pile_id,order_id,old_status,new_status,reason,created_at) VALUES(?,?,?,?,?,?)',(pile,order,'RESERVED','CHARGING','SIMULATED_START',stamp(started)))
                if stopped:
                    db.execute('INSERT INTO pile_status_logs(pile_id,order_id,old_status,new_status,reason,created_at) VALUES(?,?,?,?,?,?)',(pile,order,'CHARGING','IDLE','SIMULATED_STOP',stamp(stopped)))
                if status in ('CHARGING','RESERVED'):
                    db.execute('UPDATE charging_piles SET status=?,reserved_order_id=?,updated_at=? WHERE id=?',(status,order,stamp(now),pile))
                counts[status]+=1
            remaining = piles[active_count:]
            # Faults spread across stations; offline is explicit simulated disconnection.
            faults = max(1,len(remaining)//10)
            offline = max(1,len(remaining)//30)
            for i,(pile,_,_,_) in enumerate(remaining[:faults+offline]):
                status='FAULT' if i<faults else 'OFFLINE'
                db.execute('UPDATE charging_piles SET status=?,updated_at=? WHERE id=?',(status,stamp(now),pile))
                db.execute('INSERT INTO pile_status_logs(pile_id,old_status,new_status,reason,created_at) VALUES(?,?,?,?,?)',(pile,'IDLE',status,'SIMULATED_'+status,stamp(now-timedelta(minutes=rng.randint(1,120)))))
            # Recent completed and cancelled orders enrich the daily funnel and revenue.
            for i,(pile,station,power,price) in enumerate(remaining[faults+offline:faults+offline+200]):
                user=available[active_count+i%max(1,len(available)-active_count)]
                created=now-timedelta(minutes=rng.randint(15,240))
                reserved=created+timedelta(seconds=30)
                if i%5==0:
                    db.execute('INSERT INTO charging_orders(order_no,user_id,station_id,pile_id,status,price_cents_per_kwh,reserved_at,cancelled_at,created_at,updated_at) VALUES(?,?,?,?,?,?,?,?,?,?)',(f'{prefix}-H{i}',user,station,pile,'CANCELLED',price,stamp(reserved),stamp(reserved+timedelta(seconds=30)),stamp(created),stamp(now)))
                    counts['CANCELLED']+=1
                    continue
                started=reserved+timedelta(seconds=30)
                stopped=min(started+timedelta(minutes=rng.randint(5,30)),now-timedelta(seconds=60))
                duration=int((stopped-started).total_seconds())
                energy=round(power*rng.uniform(.35,.8)*duration/3600)
                amount=(energy*price+500)//1000
                paid=stopped+timedelta(seconds=30)
                db.execute('INSERT INTO charging_orders(order_no,user_id,station_id,pile_id,status,price_cents_per_kwh,reserved_at,started_at,stopped_at,duration_seconds,energy_wh,amount_cents,paid_at,created_at,updated_at) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)',(f'{prefix}-H{i}',user,station,pile,'COMPLETED',price,stamp(reserved),stamp(started),stamp(stopped),duration,energy,amount,stamp(paid),stamp(created),stamp(paid)))
                counts['COMPLETED']+=1
            if db.execute('PRAGMA foreign_key_check').fetchall():
                raise ValueError('Foreign key validation failed')
            if db.execute("SELECT count(*) FROM charging_piles p LEFT JOIN charging_orders o ON o.id=p.reserved_order_id WHERE p.status IN ('RESERVED','CHARGING') AND (o.id IS NULL OR o.pile_id!=p.id OR o.station_id!=p.station_id OR o.status!=p.status)").fetchone()[0]:
                raise ValueError('Pile/order state validation failed')
            db.commit()
            report={'database':str(target),'frozen_users_added':len(frozen),'orders_added':dict(counts),'pile_states':dict(db.execute('SELECT status,count(*) FROM charging_piles GROUP BY status')),'user_states':dict(db.execute('SELECT status,count(*) FROM users GROUP BY status')),'reservation_note':'New reservations expire 15 minutes after generation; regenerate a new copy before demonstrations.'}
        staging.replace(target)
        return report
    except Exception:
        staging.unlink(missing_ok=True)
        raise


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source',type=Path,required=True)
    parser.add_argument('--target',type=Path,required=True)
    parser.add_argument('--seed',type=int,default=20260915)
    args=parser.parse_args()
    print(json.dumps(enrich(args.source,args.target,args.seed),ensure_ascii=False,indent=2))
