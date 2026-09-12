#!/usr/bin/env python3
"""Pure-Python load forecasting core (standard library only)."""
import argparse
import csv
import hashlib
import json
import math
import sys
import time
from collections import defaultdict
from pathlib import Path

HORIZONS = tuple(range(1, 25))
FEATURES = ("intercept", "hour_sin", "hour_cos", "weekday_sin", "weekday_cos",
            "weekend", "current_load_kw", "lag_1h", "lag_24h", "lag_168h",
            "rolling_24h", "is_holiday", "idle_ratio", "station_id",
            "total_piles", "capacity_kw")


def _integer(value):
    try:
        number = int(value)
    except (TypeError, ValueError) as error:
        raise ValueError("invalid integer field") from error
    if str(number) != str(value).strip():
        raise ValueError("invalid integer field")
    return number


def _number(value):
    try:
        number = float(value)
    except (TypeError, ValueError) as error:
        raise ValueError("invalid finite numeric field") from error
    if not math.isfinite(number):
        raise ValueError("invalid finite numeric field")
    return number


def read_csv(path):
    with Path(path).open(newline="") as source:
        reader = csv.DictReader(source)
        required = {"timestamp_epoch", "station_id", "total_piles", "capacity_kw", "load_kw"}
        if not reader.fieldnames:
            raise ValueError("input CSV is empty")
        missing = required - set(reader.fieldnames)
        if missing:
            raise ValueError(f"missing required CSV column: {sorted(missing)[0]}")
        rows = []
        for raw in reader:
            station_id, total_piles = _integer(raw["station_id"]), _integer(raw["total_piles"])
            if not 0 < station_id <= 2_147_483_647 or not 0 < total_piles <= 1_000_000:
                raise ValueError("invalid station ID or pile count")
            capacity, load = _number(raw["capacity_kw"]), _number(raw["load_kw"])
            occupied = _number(raw.get("occupied_piles") or math.ceil(load / capacity * total_piles) if capacity > 0 else 0)
            unavailable = _number(raw.get("unavailable_piles") or 0)
            if unavailable < 0 or unavailable > total_piles or not unavailable.is_integer():
                raise ValueError("invalid unavailable pile count")
            rows.append({"timestamp_epoch": _integer(raw["timestamp_epoch"]), "station_id": station_id,
                         "total_piles": total_piles, "capacity_kw": capacity, "load_kw": load,
                         "occupied_piles": occupied, "is_holiday": _number(raw.get("is_holiday") or 0),
                         "unavailable_piles": int(unavailable)})
    if not rows:
        raise ValueError("input CSV contains no data rows")
    return sorted(rows, key=lambda row: (row["station_id"], row["timestamp_epoch"]))


def group_by_station(records):
    stations = defaultdict(list)
    for row in records:
        stations[row["station_id"]].append(row)
    for station_id, rows in stations.items():
        for index, row in enumerate(rows):
            if (row["capacity_kw"] <= 0 or row["load_kw"] < 0 or row["load_kw"] > row["capacity_kw"]
                    or row["occupied_piles"] < 0 or row["occupied_piles"] > row["total_piles"]
                    or row["is_holiday"] not in (0, 1) or row["timestamp_epoch"] % 3600):
                raise ValueError(f"station {station_id} has invalid hourly data")
            if index and row["timestamp_epoch"] - rows[index - 1]["timestamp_epoch"] != 3600:
                raise ValueError(f"station {station_id} has missing or duplicate hourly records")
    return dict(sorted(stations.items()))


def make_features(rows, index):
    if index < 168:
        raise ValueError("at least 168 prior hours are required")
    current, stamp = rows[index], rows[index]["timestamp_epoch"]
    hour = (stamp // 3600 + 8) % 24
    weekday = ((stamp + 8 * 3600) // 86400 + 4) % 7
    return [1.0, math.sin(2 * math.pi * hour / 24), math.cos(2 * math.pi * hour / 24),
            math.sin(2 * math.pi * weekday / 7), math.cos(2 * math.pi * weekday / 7),
            float(weekday in (0, 6)), current["load_kw"], rows[index - 1]["load_kw"],
            rows[index - 24]["load_kw"], rows[index - 168]["load_kw"],
            sum(row["load_kw"] for row in rows[index - 24:index]) / 24, current["is_holiday"],
            max(0.0, 1 - (current["occupied_piles"] + current["unavailable_piles"]) / current["total_piles"]),
            float(current["station_id"]), float(current["total_piles"]), current["capacity_kw"]]


def make_samples(records):
    samples = []
    for station_id, rows in group_by_station(records).items():
        if len(rows) <= 192:
            raise ValueError("each station needs more than 192 hourly rows")
        for index in range(168, len(rows) - 24):
            samples.append({"timestamp_epoch": rows[index]["timestamp_epoch"], "station_id": station_id,
                            "capacity_kw": rows[index]["capacity_kw"], "features": make_features(rows, index),
                            "targets": [rows[index + lead]["load_kw"] for lead in HORIZONS]})
    return sorted(samples, key=lambda row: (row["timestamp_epoch"], row["station_id"]))


def solve(matrix):
    size = len(FEATURES)
    for column in range(size):
        pivot = max(range(column, size), key=lambda row: abs(matrix[row][column]))
        if abs(matrix[pivot][column]) < 1e-12:
            raise ValueError("regression matrix is singular")
        matrix[column], matrix[pivot] = matrix[pivot], matrix[column]
        divisor = matrix[column][column]
        matrix[column][column:] = [value / divisor for value in matrix[column][column:]]
        for row in range(size):
            if row == column:
                continue
            factor = matrix[row][column]
            matrix[row][column:] = [value - factor * base
                                    for value, base in zip(matrix[row][column:], matrix[column][column:])]
    return [row[size] for row in matrix]


def standardized(model, features):
    return [features[0]] + [(value - model["means"][index]) / model["scales"][index]
                            for index, value in enumerate(features[1:], 1)]


def forecast(model, horizon, features):
    if model["use_persistence"][horizon]:
        return features[6]
    return sum(value * weight for value, weight in zip(standardized(model, features), model["weights"][horizon]))


def _scores(rows, model, horizon, persistence):
    model_errors, persistence_errors = [], []
    for sample in rows:
        predicted = sample["features"][6] if persistence else forecast(model, horizon, sample["features"])
        predicted = min(sample["capacity_kw"], max(0, predicted))
        model_errors.append(predicted - sample["targets"][horizon])
        persistence_errors.append(sample["features"][6] - sample["targets"][horizon])
    return {"model_mae": sum(map(abs, model_errors)) / len(rows),
            "model_rmse": math.sqrt(sum(error * error for error in model_errors) / len(rows)),
            "persistence_mae": sum(map(abs, persistence_errors)) / len(rows),
            "persistence_rmse": math.sqrt(sum(error * error for error in persistence_errors) / len(rows))}


def train_model(input_path, evaluation_path):
    samples = make_samples(read_csv(input_path))
    train_end = samples[len(samples) * 7 // 10]["timestamp_epoch"]
    validation_end = samples[len(samples) * 8 // 10]["timestamp_epoch"]
    training = [row for row in samples if row["timestamp_epoch"] + 86400 < train_end]
    validation = [row for row in samples if train_end <= row["timestamp_epoch"] and row["timestamp_epoch"] + 86400 < validation_end]
    testing = [row for row in samples if row["timestamp_epoch"] >= validation_end]
    if not training or not validation or not testing:
        raise ValueError("not enough data for temporal train/validation/test split")
    evaluation_path.parent.mkdir(parents=True, exist_ok=True)
    Path(str(evaluation_path) + ".split.json").write_text(json.dumps({
        "train_target_max": training[-1]["timestamp_epoch"] + 86400,
        "validation_origin_min": validation[0]["timestamp_epoch"],
        "validation_target_max": validation[-1]["timestamp_epoch"] + 86400,
        "test_origin_min": testing[0]["timestamp_epoch"]}) + "\n")
    means, scales = [0.0] * len(FEATURES), [1.0] * len(FEATURES)
    for feature in range(1, len(FEATURES)):
        means[feature] = sum(row["features"][feature] for row in training) / len(training)
        variance = sum((row["features"][feature] - means[feature]) ** 2 for row in training) / len(training)
        scales[feature] = max(1.0 if variance < 1e-18 else math.sqrt(variance), 1e-9)
    model = {"means": means, "scales": scales, "weights": [], "use_persistence": [False] * 24}
    vectors = [standardized(model, row["features"]) for row in training]
    gram = [[sum(vector[row] * vector[column] for vector in vectors) for column in range(len(FEATURES))]
            for row in range(len(FEATURES))]
    for horizon in range(24):
        rhs = [sum(vector[row] * sample["targets"][horizon] for vector, sample in zip(vectors, training))
               for row in range(len(FEATURES))]
        matrix = [line[:] + [rhs[index]] for index, line in enumerate(gram)]
        for feature in range(1, len(FEATURES)):
            matrix[feature][feature] += .01
        model["weights"].append(solve(matrix))
    metrics = {}
    for horizon in range(24):
        ridge = _scores(validation, model, horizon, False)
        model["use_persistence"][horizon] = ridge["persistence_mae"] <= ridge["model_mae"]
        score = _scores(testing, model, horizon, model["use_persistence"][horizon])
        score["strategy"] = "persistence" if model["use_persistence"][horizon] else "ridge"
        metrics[str(horizon + 1)] = score
    with evaluation_path.open("w", newline="") as output:
        writer = csv.writer(output)
        writer.writerow(("station_id", "origin_epoch", "predicted_for_epoch", "lead_hours", "actual_kw", "predicted_kw", "error_kw"))
        for sample in testing:
            if sample["station_id"] != testing[0]["station_id"]:
                continue
            for lead in (1, 6, 24):
                predicted = min(sample["capacity_kw"], max(0, forecast(model, lead - 1, sample["features"])))
                writer.writerow((sample["station_id"], sample["timestamp_epoch"], sample["timestamp_epoch"] + lead * 3600,
                                 lead, sample["targets"][lead - 1], predicted, predicted - sample["targets"][lead - 1]))
    return model, metrics


def save_model(path, model):
    payload = {"format_version": "PKLOT_ML_V4_PYTHON", "timezone": "Asia/Shanghai",
               "features": FEATURES, "lead_hours": HORIZONS, **model}
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    Path(path).write_text(json.dumps(payload, separators=(",", ":"), ensure_ascii=False) + "\n", encoding="utf-8")


def load_model(path):
    try:
        text = Path(path).read_text(encoding="utf-8")
        if text.startswith("PKLOT_ML_V3\n"):
            lines = text.splitlines()
            if lines[1] != "16" or len(lines) != 29:
                raise ValueError("invalid or unsupported model file; retrain with Python V4")
            model = {"format_version": "PKLOT_ML_V4_PYTHON", "features": FEATURES,
                     "means": list(map(float, lines[2].split())),
                     "scales": list(map(float, lines[3].split())),
                     "weights": [list(map(float, line.split()[1:])) for line in lines[4:28]],
                     "use_persistence": [value == "1" for value in lines[28].split()]}
        else:
            model = json.loads(text)
    except (OSError, json.JSONDecodeError, IndexError) as error:
        raise ValueError("invalid or unsupported model file; retrain with Python V4") from error
    if model.get("format_version") != "PKLOT_ML_V4_PYTHON" or len(model.get("features", ())) != len(FEATURES):
        raise ValueError("invalid or unsupported model file; retrain with Python V4")
    if len(model.get("means", ())) != 16 or any(not math.isfinite(value) for value in model["means"]):
        raise ValueError("invalid model mean")
    if len(model.get("scales", ())) != 16 or any(not math.isfinite(value) or value <= 0 for value in model["scales"]):
        raise ValueError("invalid model scale")
    if len(model.get("weights", ())) != 24 or any(len(row) != 16 or any(not math.isfinite(value) for value in row)
                                                   for row in model["weights"]):
        raise ValueError("invalid model weight")
    if len(model.get("use_persistence", ())) != 24 or any(type(value) is not bool for value in model["use_persistence"]):
        raise ValueError("invalid forecast strategy")
    return model


def make_predictions(records, model):
    predictions = []
    for station_id, rows in group_by_station(records).items():
        if len(rows) <= 168:
            raise ValueError("each station needs at least 169 hourly rows for prediction")
        features, current = make_features(rows, len(rows) - 1), rows[-1]
        for horizon in HORIZONS:
            load = min(current["capacity_kw"], max(0, forecast(model, horizon - 1, features)))
            occupied = min(current["total_piles"], math.ceil(load / (current["capacity_kw"] / current["total_piles"])))
            predictions.append({"station_id": station_id, "horizon_hours": horizon,
                                "predicted_for_epoch": current["timestamp_epoch"] + horizon * 3600,
                                "predicted_load_kw": round(load, 4), "predicted_occupied_piles": occupied,
                                "predicted_available_piles": max(0, current["total_piles"] - occupied - current["unavailable_piles"]),
                                "congestion_ratio": round(occupied / current["total_piles"], 4)})
    return predictions


def train(input_path, model_path, metrics_path):
    model, metrics = train_model(Path(input_path), Path(str(metrics_path) + ".evaluation.csv"))
    save_model(model_path, model)
    Path(metrics_path).write_text(json.dumps(metrics, indent=2) + "\n")
    Path(str(model_path) + ".metadata.json").write_text(json.dumps({
        "format_version": "PKLOT_ML_V4_PYTHON", "timezone": "Asia/Shanghai",
        "features": FEATURES, "lead_hours": HORIZONS}) + "\n")


def predict(input_path, model_path, output_path):
    model_bytes = Path(model_path).read_bytes()
    result = {"model_version": "pklot-v4-" + hashlib.sha256(model_bytes).hexdigest()[:16],
              "generated_at_epoch": int(time.time()),
              "predictions": make_predictions(read_csv(input_path), load_model(model_path))}
    Path(output_path).parent.mkdir(parents=True, exist_ok=True)
    Path(output_path).write_text(json.dumps(result, indent=2) + "\n")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    train_parser = commands.add_parser("train")
    train_parser.add_argument("input", type=Path); train_parser.add_argument("model", type=Path); train_parser.add_argument("metrics", type=Path)
    predict_parser = commands.add_parser("predict")
    predict_parser.add_argument("input", type=Path); predict_parser.add_argument("model", type=Path); predict_parser.add_argument("output", type=Path)
    args = parser.parse_args(argv)
    if args.command == "train":
        train(args.input, args.model, args.metrics)
    else:
        predict(args.input, args.model, args.output)


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)
