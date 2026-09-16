"""Read-only Flask API for Spark-generated ADS JSON snapshots."""
from __future__ import annotations

import json
import os
import sqlite3
from contextlib import closing
from datetime import datetime, timedelta, timezone
from pathlib import Path
from threading import RLock
from time import monotonic
from zoneinfo import ZoneInfo

from flask import Flask, jsonify, request


_DISTRICT_GEOMETRY = None


def _point_in_ring(longitude, latitude, ring):
    """Return whether a longitude/latitude point lies in one GeoJSON linear ring."""
    inside = False
    for index, point in enumerate(ring):
        previous = ring[index - 1]
        x1, y1 = point
        x2, y2 = previous
        crosses = (y1 > latitude) != (y2 > latitude)
        if crosses and longitude < (x2 - x1) * (latitude - y1) / (y2 - y1) + x1:
            inside = not inside
    return inside


def _load_beijing_districts():
    global _DISTRICT_GEOMETRY
    if _DISTRICT_GEOMETRY is not None:
        return _DISTRICT_GEOMETRY
    path = Path(__file__).resolve().parents[2] / "web-bigscreen/public/maps/beijing.json"
    document = json.loads(path.read_text(encoding="utf-8"))
    districts = []
    for feature in document["features"]:
        geometry = feature["geometry"]
        polygons = geometry["coordinates"] if geometry["type"] == "MultiPolygon" else [geometry["coordinates"]]
        points = [point for polygon in polygons for ring in polygon for point in ring]
        longitudes = [point[0] for point in points]
        latitudes = [point[1] for point in points]
        districts.append(
            (
                feature["properties"]["name"],
                feature["properties"]["centroid"],
                geometry,
                (min(longitudes), min(latitudes), max(longitudes), max(latitudes)),
            )
        )
    _DISTRICT_GEOMETRY = districts
    return _DISTRICT_GEOMETRY


def _coordinate_district(longitude, latitude):
    """Resolve a station to Beijing's district polygons, with a nearest-centroid fallback."""
    districts = _load_beijing_districts()
    for name, _, geometry, bounds in districts:
        minimum_longitude, minimum_latitude, maximum_longitude, maximum_latitude = bounds
        if not (minimum_longitude <= longitude <= maximum_longitude and minimum_latitude <= latitude <= maximum_latitude):
            continue
        polygons = geometry["coordinates"] if geometry["type"] == "MultiPolygon" else [geometry["coordinates"]]
        for polygon in polygons:
            if _point_in_ring(longitude, latitude, polygon[0]) and not any(
                _point_in_ring(longitude, latitude, hole) for hole in polygon[1:]
            ):
                return name
    return min(districts, key=lambda item: (item[1][0] - longitude) ** 2 + (item[1][1] - latitude) ** 2)[0]


def _resolve_station_districts(stations):
    for station in stations:
        if station["district"] not in (None, "", "未知", "未知区域"):
            continue
        station["district"] = _coordinate_district(float(station["longitude"]), float(station["latitude"]))
        station["district_source"] = "coordinate"
    return stations


def read_topics(database_path):
    """Bounded, read-only business aggregates for the six topic screens."""
    if not database_path:
        raise FileNotFoundError("DATABASE_PATH is required for topic screens")
    path = Path(database_path).resolve()
    now = datetime.now(ZoneInfo("Asia/Shanghai"))
    start = now.replace(hour=0, minute=0, second=0, microsecond=0)
    iso = lambda t: t.astimezone(timezone.utc).isoformat(timespec="milliseconds").replace("+00:00", "Z")
    lower, upper, window, month = map(iso, (start, start + timedelta(days=1), start - timedelta(days=29), start.replace(day=1)))
    with closing(sqlite3.connect(path.as_uri() + "?mode=ro", uri=True, timeout=5)) as db:
        db.row_factory = sqlite3.Row
        db.execute("PRAGMA query_only=ON")
        db.execute("BEGIN")
        rows = lambda sql, args=(): [dict(r) for r in db.execute(sql, args)]
        scalar = lambda sql, args=(): db.execute(sql, args).fetchone()[0]
        order_stats = rows("SELECT count(*) total_orders,coalesce(avg(CASE WHEN status='COMPLETED' THEN duration_seconds END),0) avg_duration_seconds,coalesce(avg(CASE WHEN status='COMPLETED' THEN energy_wh END),0) avg_energy_wh,coalesce(avg(CASE WHEN status='COMPLETED' THEN amount_cents END),0) avg_amount_cents FROM charging_orders")[0]
        order_stats["statuses"] = rows("SELECT status,count(*) count FROM charging_orders GROUP BY status")
        order_stats["funnel"] = rows("SELECT count(*) created,coalesce(sum(reserved_at IS NOT NULL),0) reserved,coalesce(sum(started_at IS NOT NULL),0) started,coalesce(sum(stopped_at IS NOT NULL),0) stopped,coalesce(sum(paid_at IS NOT NULL AND status='COMPLETED'),0) paid FROM charging_orders WHERE created_at>=? AND created_at<?", (lower, upper))[0]
        order_stats["hourly"] = rows("SELECT cast(strftime('%H',created_at,'+8 hours') as integer) hour,count(*) count FROM charging_orders WHERE created_at>=? AND created_at<? GROUP BY hour", (lower, upper))
        users = rows("SELECT count(*) total_users,coalesce(sum(balance_cents),0) balance_cents,coalesce(sum(created_at>=? AND created_at<?),0) today_new FROM users", (lower, upper))[0]
        users["active_30d"] = scalar("SELECT count(distinct user_id) FROM charging_orders WHERE created_at>=? AND created_at<?", (window, upper))
        users["recharge_cents"] = scalar("SELECT coalesce(sum(amount_cents),0) FROM recharge_records")
        users["statuses"] = rows("SELECT status,count(*) count FROM users GROUP BY status")
        users["growth"] = rows("SELECT date(created_at,'+8 hours') date,count(*) count FROM users WHERE created_at>=? AND created_at<? GROUP BY date", (window, upper))
        users["recharges"] = rows("SELECT date(created_at,'+8 hours') date,sum(amount_cents) amount_cents FROM recharge_records WHERE created_at>=? AND created_at<? GROUP BY date", (window, upper))
        users["top"] = rows("SELECT user_id,count(*) order_count,sum(amount_cents) amount_cents FROM charging_orders WHERE status='COMPLETED' GROUP BY user_id ORDER BY amount_cents DESC,user_id LIMIT 10")
        users["spending"] = rows("WITH spending AS (SELECT u.id,coalesce(sum(o.amount_cents),0) cents FROM users u LEFT JOIN charging_orders o ON o.user_id=u.id AND o.status='COMPLETED' GROUP BY u.id) SELECT CASE WHEN cents=0 THEN '未消费' WHEN cents<5000 THEN '0–50元' WHEN cents<20000 THEN '50–200元' WHEN cents<100000 THEN '200–1000元' ELSE '1000元以上' END band,count(*) count FROM spending GROUP BY band")
        spatial = rows("SELECT s.id station_id,s.latitude,s.longitude,s.district,s.status station_status,coalesce(p.total,0) pile_count,coalesce(p.idle,0) available_pile_count,coalesce(p.busy,0) busy_pile_count,coalesce(p.fault,0) fault_count,coalesce(p.offline,0) offline_count,coalesce(o.orders,0) order_count,coalesce(o.revenue,0) revenue_cents FROM stations s LEFT JOIN (SELECT station_id,count(*) total,sum(status='IDLE') idle,sum(status IN ('RESERVED','CHARGING')) busy,sum(status='FAULT') fault,sum(status='OFFLINE') offline FROM charging_piles GROUP BY station_id) p ON p.station_id=s.id LEFT JOIN (SELECT station_id,count(*) orders,sum(amount_cents) revenue FROM charging_orders WHERE status='COMPLETED' AND paid_at>=? AND paid_at<? GROUP BY station_id) o ON o.station_id=s.id ORDER BY s.id", (window, upper))
        _resolve_station_districts(spatial)
        for station in spatial:
            station["utilization_rate"] = round(100 * station["busy_pile_count"] / station["pile_count"], 2) if station["pile_count"] else 0
        logs = rows("SELECT l.id,p.pile_no,l.old_status,l.new_status,l.reason,l.created_at FROM pile_status_logs l JOIN charging_piles p ON p.id=l.pile_id ORDER BY l.id DESC LIMIT 20")
        energy = rows("SELECT coalesce(sum(CASE WHEN status='COMPLETED' AND paid_at>=? AND paid_at<? THEN amount_cents ELSE 0 END),0) month_revenue_cents,coalesce(sum(CASE WHEN status IN ('UNPAID','COMPLETED') AND stopped_at>=? AND stopped_at<? THEN energy_wh ELSE 0 END),0) month_energy_wh FROM charging_orders", (month, upper, month, upper))[0]
        tables = [{"name": name, "count": scalar(f"SELECT count(*) FROM {name}")} for name in ("users", "stations", "charging_piles", "charging_orders", "recharge_records", "pile_status_logs", "operation_logs")]
        system = {"database_bytes": path.stat().st_size + sum(p.stat().st_size for p in (Path(str(path) + "-wal"), Path(str(path) + "-shm")) if p.exists()), "journal_mode": scalar("PRAGMA journal_mode"), "schema_version": scalar("SELECT max(version) FROM schema_migrations"), "tables": tables, "admin_events": rows("SELECT action,target_type,target_id,created_at FROM operation_logs ORDER BY id DESC LIMIT 10"), "socket_connections": None, "socket_requests": None, "map_status": "未采集"}
        return {"generated_at": datetime.now(timezone.utc).isoformat(), "window_start": start.date().isoformat(), "source": "业务数据库（含模拟数据）", "orders": order_stats, "users": users, "stations": spatial, "pile_logs": logs, "energy": energy, "system": system}


def read_pile_page(database_path, page, size, status):
    if not database_path:
        raise FileNotFoundError("DATABASE_PATH is required")
    where, args = (" WHERE status=?", (status,)) if status else ("", ())
    with closing(sqlite3.connect(Path(database_path).resolve().as_uri() + "?mode=ro", uri=True, timeout=5)) as db:
        db.row_factory = sqlite3.Row
        db.execute("PRAGMA query_only=ON")
        db.execute("BEGIN")
        total = db.execute("SELECT count(*) FROM charging_piles" + where, args).fetchone()[0]
        items = [dict(r) for r in db.execute("SELECT id,station_id,pile_no,status,rated_power_w FROM charging_piles" + where + " ORDER BY id LIMIT ? OFFSET ?", (*args, size, (page-1)*size))]
        return {"items": items, "total": total, "page": page, "page_size": size}


class AdsStore:
    def __init__(self, root, batch_id, cache_seconds=60):
        self.root = Path(root).resolve()
        self.batch_id = batch_id
        self.cache_seconds = cache_seconds
        self._cache = {}
        self._lock = RLock()

    def dataset_path(self, name):
        if not name.startswith("ads_") or "/" in name or "\\" in name:
            raise ValueError("invalid ADS dataset name")
        return self.root / name / f"batch_id={self.batch_id}" / "json"

    def load(self, name):
        now = monotonic()
        with self._lock:
            cached = self._cache.get(name)
            if cached and now - cached[0] < self.cache_seconds:
                return cached[1]
            directory = self.dataset_path(name)
            files = sorted(directory.glob("part-*.json"))
            if not files:
                raise FileNotFoundError(f"ADS dataset is not ready: {directory}")
            rows = []
            for path in files:
                with path.open(encoding="utf-8") as stream:
                    rows.extend(json.loads(line) for line in stream if line.strip())
            self._cache[name] = (now, rows)
            return rows


def envelope(rows, **extra):
    first = rows[0] if rows else {}
    return {"batch_id": first.get("batch_id"), "generated_at": first.get("generated_at"),
            "data_as_of": first.get("data_as_of"), "items": rows, **extra}


def load_ml_prediction(path):
    if not path:
        return {"model_version": None, "horizon_hours": 0, "points": []}
    document = json.loads(Path(path).read_text(encoding="utf-8"))
    grouped = {}
    for row in document.get("predictions", []):
        lead = int(row["horizon_hours"])
        if not 1 <= lead <= 6:
            continue
        point = grouped.setdefault(lead, {"predicted_for_epoch": int(row["predicted_for_epoch"]),
            "load_w": 0.0, "available_piles": 0, "occupied_piles": 0})
        if point["predicted_for_epoch"] != int(row["predicted_for_epoch"]):
            raise ValueError(f"ML horizon {lead} contains inconsistent timestamps")
        point["load_w"] += float(row["predicted_load_kw"]) * 1000
        point["available_piles"] += int(row["predicted_available_piles"])
        point["occupied_piles"] += int(row.get("predicted_occupied_piles", 0))
    points = []
    for lead, point in sorted(grouped.items()):
        total = point["available_piles"] + point["occupied_piles"]
        points.append({"horizon_hours": lead,
            "predicted_for": datetime.fromtimestamp(point["predicted_for_epoch"], timezone.utc).isoformat().replace("+00:00", "Z"),
            "load_w": round(point["load_w"], 2), "available_piles": point["available_piles"],
            "congestion_score": round(point["occupied_piles"] / total, 4) if total else 0.0})
    return {"model_version": document.get("model_version"), "horizon_hours": len(points), "points": points}


def load_live_dashboard(database_path):
    if not database_path:
        return None
    path = Path(database_path).resolve()
    uri = f"file:{path.as_posix()}?mode=ro"
    zone = ZoneInfo("Asia/Shanghai")
    local_now = datetime.now(timezone.utc).astimezone(zone)
    start = datetime(local_now.year, local_now.month, local_now.day, tzinfo=zone).astimezone(timezone.utc)
    end = start + timedelta(days=1)
    lower = start.isoformat(timespec="milliseconds").replace("+00:00", "Z")
    upper = end.isoformat(timespec="milliseconds").replace("+00:00", "Z")
    with sqlite3.connect(uri, uri=True) as connection:
        connection.row_factory = sqlite3.Row
        scalar = lambda sql, args=(): connection.execute(sql, args).fetchone()[0]
        statuses = [dict(row) for row in connection.execute(
            "SELECT status name,count(*) value FROM charging_piles GROUP BY status ORDER BY status")]
        orders = [dict(row) for row in connection.execute(
            "SELECT o.id,o.order_no,o.status,o.station_id,s.name station_name,p.pile_no,"
            "CASE WHEN o.status='CHARGING' THEN p.rated_power_w ELSE 0 END power_w,"
            "coalesce(o.amount_cents,0) amount_cents FROM charging_orders o "
            "JOIN stations s ON s.id=o.station_id JOIN charging_piles p ON p.id=o.pile_id "
            "ORDER BY o.created_at DESC,o.id DESC LIMIT 8")]
        return {
            "generated_at": datetime.now(timezone.utc).isoformat().replace("+00:00", "Z"),
            "total_revenue_cents": scalar("SELECT coalesce(sum(amount_cents),0) FROM charging_orders WHERE status='COMPLETED'"),
            "today_revenue_cents": scalar("SELECT coalesce(sum(amount_cents),0) FROM charging_orders WHERE status='COMPLETED' AND paid_at>=? AND paid_at<?", (lower, upper)),
            "today_orders": scalar("SELECT count(*) FROM charging_orders WHERE created_at>=? AND created_at<?", (lower, upper)),
            "today_energy_wh": scalar("SELECT coalesce(sum(energy_wh),0) FROM charging_orders WHERE status IN ('UNPAID','COMPLETED') AND stopped_at>=? AND stopped_at<?", (lower, upper)),
            "station_count": scalar("SELECT count(*) FROM stations WHERE status='ACTIVE'"),
            "pile_count": scalar("SELECT count(*) FROM charging_piles"),
            "pile_status": statuses,
            "realtime_orders": orders,
        }


def create_app(config=None):
    app = Flask(__name__)
    app.config.update(ADS_ROOT=os.environ.get("ADS_ROOT", "runtime/warehouse/ads"),
                      ADS_BATCH_ID=os.environ.get("ADS_BATCH_ID", ""),
                      ADS_CACHE_SECONDS=int(os.environ.get("ADS_CACHE_SECONDS", "60")),
                      ML_PREDICTIONS_PATH=os.environ.get("ML_PREDICTIONS_PATH", ""),
                      DATABASE_PATH=os.environ.get("DATABASE_PATH", ""))
    if config:
        app.config.update(config)
    if not app.config["ADS_BATCH_ID"]:
        raise RuntimeError("ADS_BATCH_ID must be set explicitly")
    store = AdsStore(app.config["ADS_ROOT"], app.config["ADS_BATCH_ID"], app.config["ADS_CACHE_SECONDS"])
    app.extensions["ads_store"] = store
    topic_cache = {}
    topic_lock = RLock()

    @app.get("/api/v1/topics")
    def topics():
        with topic_lock:
            if not topic_cache or monotonic() - topic_cache["at"] >= 5:
                topic_cache.update(data=read_topics(app.config["DATABASE_PATH"]), at=monotonic())
            return jsonify(topic_cache["data"])

    @app.get("/api/v1/piles")
    def piles():
        try:
            page = int(request.args.get("page", 1))
            size = int(request.args.get("page_size", 120))
            status = request.args.get("status", "")
            if page < 1 or not 1 <= size <= 240 or status not in ("", "IDLE", "RESERVED", "CHARGING", "FAULT", "OFFLINE"):
                raise ValueError()
        except ValueError:
            return jsonify({"error": "invalid_pagination"}), 400
        return jsonify(read_pile_page(app.config["DATABASE_PATH"], page, size, status))

    @app.errorhandler(sqlite3.Error)
    def database_unavailable(error):
        app.logger.warning("Read-only database query failed: %s", type(error).__name__)
        return jsonify({"error": "database_unavailable"}), 503

    @app.after_request
    def add_headers(response):
        response.headers["Cache-Control"] = "no-store"
        response.headers["Access-Control-Allow-Origin"] = "*"
        return response

    @app.errorhandler(FileNotFoundError)
    def unavailable(error):
        return jsonify({"error": "ads_not_ready", "message": str(error), "batch_id": app.config["ADS_BATCH_ID"]}), 503

    @app.get("/health")
    def health():
        return {"status": "ok", "batch_id": app.config["ADS_BATCH_ID"]}

    @app.get("/api/v1/overview")
    def overview():
        rows = store.load("ads_overview")
        return jsonify(rows[0] if rows else {})

    @app.get("/api/v1/trends/revenue")
    def revenue_trend():
        days = max(1, min(int(request.args.get("days", 30)), 90))
        rows = store.load("ads_revenue_trend_30d")[-days:]
        return jsonify(envelope(rows, days=days))

    @app.get("/api/v1/rankings/stations")
    def station_ranking():
        limit = max(1, min(int(request.args.get("limit", 10)), 100))
        rows = store.load("ads_station_ranking_30d")[:limit]
        return jsonify(envelope(rows, days=30, limit=limit))

    @app.get("/api/v1/distribution/districts")
    def district_distribution():
        return jsonify(envelope(store.load("ads_district_distribution")))

    @app.get("/api/v1/distribution/stations")
    def station_distribution():
        return jsonify(envelope(store.load("ads_station_distribution")))

    @app.get("/api/v1/piles/status")
    def pile_status():
        return jsonify(envelope(store.load("ads_pile_status")))

    @app.get("/api/v1/piles/utilization")
    def pile_utilization():
        limit = max(1, min(int(request.args.get("limit", 50)), 100))
        return jsonify(envelope(store.load("ads_pile_utilization")[:limit], limit=limit))

    @app.get("/api/v1/quality/overview")
    def quality_overview():
        rows = store.load("ads_quality_overview")
        return jsonify(rows[0] if rows else {})

    @app.get("/api/v1/quality/rules")
    def quality_rules():
        return jsonify(envelope(store.load("ads_quality_rules")))

    @app.get("/api/v1/comparisons")
    def comparisons():
        district = store.load("ads_district_charge_type_30d")
        hours = store.load("ads_day_type_hour_30d")
        first = (district or hours)[0] if district or hours else {}
        return jsonify({"batch_id": first.get("batch_id"), "generated_at": first.get("generated_at"),
                        "data_as_of": first.get("data_as_of"),
                        "district_charge_type": district, "day_type_hour": hours})

    @app.get("/api/dashboard")
    def compatible_dashboard():
        row = store.load("ads_overview")[0]
        trend = store.load("ads_revenue_trend_30d")
        statuses = store.load("ads_pile_status")
        live = load_live_dashboard(app.config["DATABASE_PATH"])
        dashboard = {"generated_at": row["generated_at"], "model": "spark-sql-offline",
            "total_revenue_cents": row["total_revenue_cents"], "today_revenue_cents": row["today_revenue_cents"],
            "today_orders": row["today_order_count"], "station_count": row["station_count"], "pile_count": row["pile_count"],
            "quality_score": row.get("quality_score"),
            "trend": [{**item, "date": item.get("biz_date"), "orders": item.get("order_count", 0)} for item in trend],
            "pile_status": [{"name": item["status"], "value": item["count"]} for item in statuses],
            "station_ranking": store.load("ads_station_ranking_30d")[:10],
            "stations": store.load("ads_station_distribution"), "forecast_points": [],
            "load_prediction": load_ml_prediction(app.config["ML_PREDICTIONS_PATH"]), "realtime_orders": []}
        if live:
            dashboard.update(live)
            dashboard["trend"][-1] = {**dashboard["trend"][-1], "orders": live["today_orders"],
                "order_count": live["today_orders"], "revenue_cents": live["today_revenue_cents"],
                "energy_wh": live["today_energy_wh"]}
        return jsonify(dashboard)

    @app.get("/api/analytics")
    def compatible_analytics():
        row = store.load("ads_overview")[0]
        trend = store.load("ads_revenue_trend_30d")
        return jsonify({"available": True, "stale": False, "data": {"schema_version": 1, "engine": "spark-sql",
            "batch_id": app.config["ADS_BATCH_ID"], "snapshot_at": row["generated_at"], "data_as_of": row["data_as_of"],
            "window_start": trend[0].get("biz_date") if trend else None, "window_end": trend[-1].get("biz_date") if trend else None,
            "input_orders": row.get("total_order_count", 0), "quality_score": row.get("quality_score"),
            "total_revenue_cents": row["total_revenue_cents"], "revenue_trend": {"days": len(trend), "items": trend},
            "station_ranking": {"days": 30, "items": store.load("ads_station_ranking_30d")[:10]}}})
    return app


if __name__ == "__main__":
    application = create_app()
    application.run(host=os.environ.get("FLASK_HOST", "127.0.0.1"), port=int(os.environ.get("FLASK_PORT", "5000")))
