"""Standalone ML acceptance workflow; never connects to a backend."""
import argparse
import csv
import hashlib
import json
import math
from collections import defaultdict
from pathlib import Path

from prepare_history import generate, clean, save_json, catalog_index
from export_predictions_api import main as export_api
from pklot_ml import train, predict

def distance_km(lat1, lon1, lat2, lon2):
    if not all(math.isfinite(x) for x in (lat1, lon1, lat2, lon2)) or not (
        -90 <= lat1 <= 90 and -90 <= lat2 <= 90 and -180 <= lon1 <= 180 and -180 <= lon2 <= 180
    ):
        raise ValueError("invalid coordinates")
    a, b = math.radians(lat1), math.radians(lat2)
    dlat, dlon = b - a, math.radians(lon2 - lon1)
    hav = math.sin(dlat / 2) ** 2 + math.cos(a) * math.cos(b) * math.sin(dlon / 2) ** 2
    return 6371 * 2 * math.asin(min(1, math.sqrt(hav)))


def analyze(catalog, history, predictions, latitude, longitude, threshold=.8, radius_km=20):
    stations, _ = catalog_index(catalog)
    distance_km(latitude, longitude, latitude, longitude)
    if not 0 < threshold <= 1 or not math.isfinite(radius_km) or radius_km <= 0:
        raise ValueError("invalid alert threshold or radius")
    latest = {}
    for row in history:
        sid, stamp = int(row["station_id"]), int(row["timestamp_epoch"])
        if sid not in latest or stamp > int(latest[sid]["timestamp_epoch"]):
            latest[sid] = row
    grouped = defaultdict(list)
    for item in predictions["predictions"]:
        grouped[int(item["station_id"])].append(item)
    recommendations, excluded, peaks, alerts = [], [], [], []
    for sid, items in sorted(grouped.items()):
        items.sort(key=lambda item: item["horizon_hours"])
        if [item["horizon_hours"] for item in items] != list(range(1, 25)):
            raise ValueError("analysis requires complete 24-hour predictions")
        current = latest[sid]
        origin = int(current["timestamp_epoch"])
        if any(item["predicted_for_epoch"] != origin + item["horizon_hours"] * 3600 for item in items):
            raise ValueError("history cutoff does not match forecast origin")
        capacity, total = float(current["capacity_kw"]), int(current["total_piles"])
        if not math.isfinite(capacity) or capacity <= 0 or total <= 0:
            raise ValueError("invalid station capacity or pile count")
        unknown = int(current.get("unknown_piles", 0))
        unavailable = int(current.get("unavailable_piles", 0))
        fault = int(current.get("fault_piles", 0))
        occupied = float(current.get("occupied_piles", math.ceil(float(current["load_kw"]) / capacity * total)))
        idle = max(0, 1 - (occupied + unavailable) / total)
        maximum = max(item["predicted_load_kw"] for item in items)
        peak_points = [item["predicted_for_epoch"] for item in items if item["predicted_load_kw"] == maximum]
        high_points = [item["predicted_for_epoch"] for item in items if item["predicted_load_kw"] >= threshold * capacity]
        peaks.append({"station_id": sid, "peak_load_kw": maximum, "peak_times_epoch": peak_points,
                      "high_load_times_epoch": high_points})
        if high_points:
            alerts.append({"station_id": sid, "type": "HIGH_LOAD", "times_epoch": high_points,
                           "reason": f"Predicted load >= {threshold:.0%} of capacity"})
        if unavailable or fault or unknown:
            alerts.append({"station_id": sid, "type": "DEVICE_ABNORMAL", "fault_piles": fault,
                           "unavailable_piles": unavailable, "unknown_piles": unknown,
                           "reason": "Latest observed fault/offline/unknown state; not a fault prediction model"})
        station = stations[sid]
        if station.get("latitude") is None or station.get("longitude") is None:
            excluded.append({"station_id": sid, "reason": "missing coordinates"})
            continue
        distance = distance_km(latitude, longitude, float(station["latitude"]), float(station["longitude"]))
        if distance > radius_km or unavailable >= total:
            excluded.append({"station_id": sid, "reason": "outside radius or all devices unavailable"})
            continue
        congestion = float(items[0]["congestion_ratio"])
        score = .4 * distance / radius_km + .3 * (1 - idle) + .3 * congestion
        recommendations.append({"station_id": sid, "score": round(score, 6),
                                "observed_at_epoch": origin,
                                "distance_km": round(distance, 3), "current_idle_ratio": idle,
                                "predicted_congestion": congestion,
                                "reasons": [f"distance {distance:.2f} km", f"current idle {idle:.0%}",
                                            f"next-hour congestion {congestion:.0%}"]})
    recommendations.sort(key=lambda item: (item["score"], item["station_id"]))
    return {"model_version": predictions.get("model_version", "unknown"),
            "query_location": {"latitude": latitude, "longitude": longitude},
            "recommendations": recommendations, "excluded": excluded, "peaks": peaks, "alerts": alerts,
            "policy": {"distance_weight": .4, "current_busy_weight": .3, "future_congestion_weight": .3,
                       "radius_km": radius_km, "high_load_capacity_ratio": threshold,
                       "device_state_assumption": "latest unavailable count persists over forecast window"}}


def evaluation_report(evaluation_path, metrics_path, output):
    with evaluation_path.open(newline="") as source:
        rows = list(csv.DictReader(source))
    if not rows:
        raise ValueError("evaluation CSV is empty")
    groups = defaultdict(list)
    for row in rows:
        groups[int(row["lead_hours"])].append(row)
    metrics = json.loads(metrics_path.read_text())
    errors, panels = {}, []
    for index, (lead, values) in enumerate(sorted(groups.items())):
        values.sort(key=lambda item: int(item["predicted_for_epoch"]))
        actual = [float(row["actual_kw"]) for row in values]
        predicted = [float(row["predicted_kw"]) for row in values]
        delta = [p - a for a, p in zip(actual, predicted)]
        errors[lead] = {"sample_count": len(delta), "bias_kw": sum(delta) / len(delta),
                        "mae_kw": sum(abs(e) for e in delta) / len(delta),
                        "rmse_kw": math.sqrt(sum(e * e for e in delta) / len(delta)),
                        "max_absolute_error_kw": max(abs(e) for e in delta)}
        ybase, scale = index * 250, max(1, *actual, *predicted)
        def line(series, color):
            points = " ".join(f"{50 + i * 800 / max(1, len(series)-1):.2f},{ybase + 170 - v / scale * 120:.2f}"
                              for i, v in enumerate(series))
            return f'<polyline fill="none" stroke="{color}" stroke-width="1.5" points="{points}"/>'
        panels.extend([f'<text x="50" y="{ybase+25}">Station {values[0]["station_id"]}, +{lead}h; '
                       f'load 0..{scale:.1f} kW; MAE {errors[lead]["mae_kw"]:.3f} kW</text>',
                       f'<path d="M50 {ybase+50} V{ybase+170} H850" fill="none" stroke="#999"/>',
                       line(actual, "#2463eb"), line(predicted, "#ea580c"),
                       f'<text x="50" y="{ybase+194}">UTC epoch {values[0]["predicted_for_epoch"]} .. '
                       f'{values[-1]["predicted_for_epoch"]}; ordered hourly samples</text>',
                       f'<text x="50" y="{ybase+217}">Bias {errors[lead]["bias_kw"]:.3f} kW; '
                       f'RMSE {errors[lead]["rmse_kw"]:.3f}; max absolute error {errors[lead]["max_absolute_error_kw"]:.3f}</text>'])
    output.mkdir(parents=True, exist_ok=True)
    height = max(920, 250 * len(groups) + 50)
    (output / "evaluation.svg").write_text(
        f'<svg xmlns="http://www.w3.org/2000/svg" width="920" height="{height}" '
        f'viewBox="0 0 920 {height}"><rect width="100%" height="100%" fill="white"/>'
        '<g font-family="sans-serif" font-size="13">' + "".join(panels) +
        f'<text x="50" y="{250*len(groups)+20}">Blue: actual; orange: predicted. Held-out first-station sample; NOT training fit.</text></g></svg>',
        encoding="utf-8")
    save_json(output / "evaluation.json", {"all_station_metrics": metrics, "sample_error_analysis": errors,
              "chart_scope": "first station in held-out set, leads 1/6/24; aggregate metrics cover all stations"})
    table = ["| Lead (h) | Strategy | MAE (kW) | RMSE (kW) | Persistence MAE |",
             "| --- | --- | --- | --- | --- |"]
    for lead, score in sorted(metrics.items(), key=lambda pair: int(pair[0])):
        table.append(f'| {lead} | {score["strategy"]} | {score["model_mae"]:.4f} | '
                     f'{score["model_rmse"]:.4f} | {score["persistence_mae"]:.4f} |')
    (output / "stations_evaluation_report.md").write_text(
        "# Python V4 负荷预测评估\n\n"
        "由 workflow.py report 从测试输出重建。24 个逐小时预测头，16 维特征；模型与基线使用时间隔离的测试集。\n\n"
        "## 全站聚合指标\n\n" + "\n".join(table) + "\n\n"
        "## 对比图与误差分析\n\n"
        "见 [evaluation.svg](evaluation.svg) 与 [evaluation.json](evaluation.json)。"
        "图只展示留出集第一站的 1/6/24h 样本；上表汇总所有测试站点，不可用单站曲线代表全量效果。\n\n"
        "## 边界\n\n"
        "随仓库存档的小时负载与 demo 均为模拟数据，不是实测运营数据。"
        "图中 actual 是测试集标签。训练和验证边界剔除跨界的 24h 未来标签；"
        "预测裁剪到站点容量范围。数据跨度、模型质量和缺失信息必须在真实数据接入后重新验证。\n",
        encoding="utf-8")


def run(output):
    if output.exists() and any(output.iterdir()):
        raise ValueError("demo output must be empty; choose a new directory to avoid overwriting results")
    output.mkdir(parents=True, exist_ok=True)
    generate(output / "data")
    catalog_path = output / "data/catalog.json"
    catalog = json.loads(catalog_path.read_text())
    meta = catalog["metadata"]
    history_path = output / "data/history.csv"
    history, quality = clean(catalog, output / "data/orders.csv", output / "data/devices.csv", history_path,
                             meta["start_epoch"], meta["end_epoch"],
                             json.loads((output / "data/holidays.json").read_text()))
    model, metrics, predictions_path = (output / name for name in ("model.json", "metrics.json", "predictions.json"))
    train(history_path, model, metrics)
    predict(history_path, model, predictions_path)
    model_hash = hashlib.sha256(model.read_bytes()).hexdigest()
    predictions = json.loads(predictions_path.read_text())
    metadata = json.loads(Path(str(model) + ".metadata.json").read_text())
    save_json(output / "operations.json", analyze(catalog, history, predictions, 39.9, 116.4))
    evaluation_report(Path(str(metrics) + ".evaluation.csv"), metrics, output)
    export_api(predictions_path, catalog_path, output / "predictions_api.json")
    save_json(output / "manifest.json", {"simulated": True, "seed": 42, "features": metadata["features"],
              "model_version": predictions["model_version"], "model_sha256": model_hash,
              "history_sha256": hashlib.sha256(history_path.read_bytes()).hexdigest(),
              "forecast_hours": list(range(1, 25)), "api_windows": [1, 6, 24],
              "split": "chronological 70/10/20; 24h target purge at boundaries",
              "quality": quality["counts"]})
    print(f"workflow OK: 3 simulated stations x 24 hours; artifacts: {output}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    demo = commands.add_parser("demo")
    demo.add_argument("output", type=Path)
    analysis = commands.add_parser("analyze")
    for name in ("catalog", "history", "predictions", "output"):
        analysis.add_argument(name, type=Path)
    analysis.add_argument("--latitude", type=float, required=True)
    analysis.add_argument("--longitude", type=float, required=True)
    analysis.add_argument("--threshold", type=float, default=.8)
    analysis.add_argument("--radius-km", type=float, default=20)
    report = commands.add_parser("report")
    for name in ("evaluation", "metrics", "output"):
        report.add_argument(name, type=Path)
    args = parser.parse_args()
    if args.command == "demo":
        run(args.output)
    elif args.command == "analyze":
        with args.history.open(newline="") as source:
            result = analyze(json.loads(args.catalog.read_text(encoding="utf-8-sig")), csv.DictReader(source),
                             json.loads(args.predictions.read_text()), args.latitude, args.longitude,
                             args.threshold, args.radius_km)
        save_json(args.output, result)
    else:
        evaluation_report(args.evaluation, args.metrics, args.output)
