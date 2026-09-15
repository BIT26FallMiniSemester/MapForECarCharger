"""Export a Qt SQLite snapshot, train Python forecasts, publish for Qt HTTP."""
import argparse
from contextlib import closing
import json
import os
from pathlib import Path
import sqlite3
import tempfile
import time

from prepare_history import clean, epoch, save_json, write_csv
from pklot_ml import train, predict
from workflow import evaluation_report


def export(database, output, start, end):
    """Preserve actual database IDs; never export user identifiers or credentials."""
    with closing(sqlite3.connect(Path(database).resolve().as_uri() + '?mode=ro', uri=True)) as db:
        db.row_factory = sqlite3.Row
        db.execute('PRAGMA query_only=ON')
        db.execute('BEGIN')
        stations = [dict(row) for row in db.execute(
            'SELECT id,name,latitude,longitude,data_source,external_id FROM stations ORDER BY id')]
        piles = [dict(row) for row in db.execute(
            'SELECT id,station_id,rated_power_w FROM charging_piles ORDER BY id')]
        catalog = {'metadata': {'source': 'qt-sqlite', 'simulated': False,
                               'start_epoch': start, 'end_epoch': end},
                   'stations': stations, 'charging_piles': piles}
        # Stopped-but-unpaid charging still consumed energy. In-progress totals are incomplete.
        orders = [dict(row) for row in db.execute(
            'SELECT id,station_id,pile_id,started_at,stopped_at,energy_wh '
            'FROM charging_orders WHERE started_at IS NOT NULL ORDER BY id')]
        incomplete = [row for row in orders if row['stopped_at'] is None
                      and epoch(row['started_at']) < end]
        if incomplete:
            raise ValueError('Incomplete charging orders overlap history; choose an earlier cutoff')
        orders = [row for row in orders if row['stopped_at'] is not None
                  and epoch(row['started_at']) < end and epoch(row['stopped_at']) > start]
        # Logs record transitions, not periodic heartbeats. Do not fabricate past states.
        events = [dict(row) for row in db.execute(
            "SELECT pile_id,created_at reported_at,CASE WHEN new_status='RESERVED' "
            "THEN 'OFFLINE' ELSE new_status END status FROM pile_status_logs ORDER BY id")]
    output.mkdir(parents=True, exist_ok=True)
    save_json(output / 'catalog.json', catalog)
    write_csv(output / 'orders.csv', ['id','station_id','pile_id','started_at','stopped_at','energy_wh'], orders)
    write_csv(output / 'devices.csv', ['pile_id','reported_at','status'], events)
    return catalog


def publish(predictions_path, catalog, target, simulated=False):
    document = json.loads(predictions_path.read_text())
    station_ids = {station['id'] for station in catalog['stations']}
    points = document['predictions']
    if not points or any(point['station_id'] not in station_ids for point in points):
        raise ValueError('Prediction station IDs do not match the Qt catalog')
    origins = {point['predicted_for_epoch'] - point['horizon_hours'] * 3600 for point in points}
    if len(origins) != 1:
        raise ValueError('All stations must share one forecast origin')
    document.update(schema_version=1, source='python-ml', simulated=simulated,
                    forecast_origin_epoch=origins.pop(),
                    station_id_space='qt-database',
                    assumptions=['Available piles estimated from load and latest unavailable count',
                                 'Qt transition logs expire after 2h; unknown states are unavailable'])
    target.parent.mkdir(parents=True, exist_ok=True)
    # Publish atomically so the HTTP reader cannot observe a partial JSON file.
    name = None
    try:
        with tempfile.NamedTemporaryFile(mode='w', dir=target.parent, delete=False, encoding='utf-8') as stream:
            name = stream.name
            json.dump(document, stream, ensure_ascii=False, allow_nan=False)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(name, target)
    finally:
        if name and Path(name).exists():
            Path(name).unlink()


def run(database, output, result, start, end, holidays=(), simulated=False, accept_cleaned=False):
    if accept_cleaned and not simulated:
        raise ValueError('Accepting cleaned records requires an explicitly simulated database')
    if end > int(time.time()) // 3600 * 3600:
        raise ValueError('History end must not be in the future')
    if output.exists() and any(output.iterdir()):
        raise ValueError('Work directory must be empty')
    if result.resolve() == database.resolve():
        raise ValueError('Result must not replace the database')
    catalog = export(database, output / 'data', start, end)
    catalog['metadata']['simulated'] = simulated
    save_json(output / 'data/catalog.json', catalog)
    history = output / 'data/history.csv'
    _, quality = clean(catalog, output / 'data/orders.csv', output / 'data/devices.csv',
                       history, start, end, holidays)
    dirty_keys = ('rejected_orders', 'overlapping_orders', 'rejected_events')
    if any(quality['counts'].get(key) for key in dirty_keys) and not accept_cleaned:
        raise ValueError('Rejected orders/events; inspect history.quality.json before publishing')
    if not quality['counts'].get('accepted_orders'):
        raise ValueError('No completed charging history in this window; cannot train an operational model')
    model, metrics, predictions = output / 'model.json', output / 'metrics.json', output / 'predictions.json'
    train(history, model, metrics)
    predict(history, model, predictions)
    evaluation_report(Path(str(metrics) + '.evaluation.csv'), metrics, output)
    publish(predictions, catalog, result, simulated)
    removed = sum(quality['counts'].get(key, 0) for key in dirty_keys)
    print(f"Data cleaning: accepted_orders={quality['counts']['accepted_orders']}; removed_records={removed}")
    print(f'Qt ML ready: {result}; evaluation: {output}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--database', required=True, type=Path)
    parser.add_argument('--work-dir', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--start', required=True, type=epoch)
    parser.add_argument('--end', type=epoch, default=int(time.time()) // 3600 * 3600)
    parser.add_argument('--holidays', type=Path)
    parser.add_argument('--simulated', action='store_true', help='Explicitly label a synthetic input database')
    parser.add_argument('--accept-cleaned', action='store_true',
                        help='Continue after cleaning rejected/overlapping simulated records')
    args = parser.parse_args()
    run(args.database, args.work_dir, args.output, args.start, args.end,
        json.loads(args.holidays.read_text()) if args.holidays else [], args.simulated, args.accept_cleaned)
