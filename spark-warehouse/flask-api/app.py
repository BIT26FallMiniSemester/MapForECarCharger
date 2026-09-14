"""Read-only Flask API for Spark-generated ADS JSON snapshots."""
from __future__ import annotations

import json
import os
from datetime import datetime, timezone
from pathlib import Path
from threading import RLock
from time import monotonic

from flask import Flask, jsonify, request


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


def create_app(config=None):
    app = Flask(__name__)
    app.config.update(ADS_ROOT=os.environ.get("ADS_ROOT", "runtime/warehouse/ads"),
                      ADS_BATCH_ID=os.environ.get("ADS_BATCH_ID", ""),
                      ADS_CACHE_SECONDS=int(os.environ.get("ADS_CACHE_SECONDS", "60")),
                      ML_PREDICTIONS_PATH=os.environ.get("ML_PREDICTIONS_PATH", ""))
    if config:
        app.config.update(config)
    if not app.config["ADS_BATCH_ID"]:
        raise RuntimeError("ADS_BATCH_ID must be set explicitly")
    store = AdsStore(app.config["ADS_ROOT"], app.config["ADS_BATCH_ID"], app.config["ADS_CACHE_SECONDS"])
    app.extensions["ads_store"] = store

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

    @app.get("/api/dashboard")
    def compatible_dashboard():
        row = store.load("ads_overview")[0]
        trend = store.load("ads_revenue_trend_30d")
        statuses = store.load("ads_pile_status")
        return jsonify({"generated_at": row["generated_at"], "model": "spark-sql-offline",
            "total_revenue_cents": row["total_revenue_cents"], "today_revenue_cents": row["today_revenue_cents"],
            "today_orders": row["today_order_count"], "station_count": row["station_count"], "pile_count": row["pile_count"],
            "quality_score": row.get("quality_score"),
            "trend": [{**item, "date": item.get("biz_date"), "orders": item.get("order_count", 0)} for item in trend],
            "pile_status": [{"name": item["status"], "value": item["count"]} for item in statuses],
            "station_ranking": store.load("ads_station_ranking_30d")[:10],
            "stations": store.load("ads_station_distribution"), "forecast_points": [],
            "load_prediction": load_ml_prediction(app.config["ML_PREDICTIONS_PATH"]), "realtime_orders": []})

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
