#!/usr/bin/env python3
"""Export ML predictions with stable catalog references for backend posting."""
import json
import math
import sys
from collections import defaultdict
from datetime import datetime, timezone
from pathlib import Path

PREDICTION_ENDPOINT = "/internal/predictions/load"
HORIZONS = {1, 6, 24}


def utc_time(epoch):
    return datetime.fromtimestamp(epoch, timezone.utc).isoformat().replace("+00:00", "Z")


def finite_number(value, field):
    value = float(value)
    if not math.isfinite(value):
        raise ValueError(f"{field} must be finite")
    return value


def catalog_references(catalog):
    references = {}
    for station in catalog["stations"]:
        local_id = int(station["id"])
        data_source = station.get("data_source")
        external_id = station.get("external_id")
        if local_id <= 0 or not data_source or external_id is None:
            raise ValueError("catalog stations require id, data_source and external_id")
        if local_id in references:
            raise ValueError("duplicate catalog station ID")
        references[local_id] = {
            "data_source": str(data_source),
            "external_id": str(external_id),
        }
    return references


def build_entries(predictions, generated_at_epoch, model_version, references):
    generated_at_epoch = int(generated_at_epoch)
    if not 1 <= len(model_version) <= 64:
        raise ValueError("model_version must contain 1 to 64 characters")
    entries = []
    grouped = defaultdict(dict)
    for item in predictions:
        local_station_id = int(item["station_id"])
        horizon_hours = int(item["horizon_hours"])
        if local_station_id not in references or not 1 <= horizon_hours <= 24:
            raise ValueError("unknown station_id or invalid horizon_hours")
        if horizon_hours in grouped[local_station_id]:
            raise ValueError("duplicate station/lead prediction")
        grouped[local_station_id][horizon_hours] = item
    if not grouped:
        raise ValueError("predictions must not be empty")
    for local_station_id, sequence in grouped.items():
        if set(sequence) not in (set(range(1, 25)), HORIZONS):
            raise ValueError("expected complete 24-hour sequence or legacy 1/6/24 points")
        origins = {int(item["predicted_for_epoch"]) - lead * 3600 for lead, item in sequence.items()}
        if len(origins) != 1:
            raise ValueError("prediction times must be contiguous from one origin")
        for horizon_hours in sorted(HORIZONS):
            window = [sequence[lead] for lead in range(1, horizon_hours + 1)] if len(sequence) == 24 else [sequence[horizon_hours]]
            points = []
            for item in window:
                load = finite_number(item["predicted_load_kw"], "load") * 1000
                available = finite_number(item["predicted_available_piles"], "available")
                congestion = finite_number(item["congestion_ratio"], "congestion")
                if not math.isfinite(load) or load < 0 or available < 0 or not available.is_integer() or not 0 <= congestion <= 1:
                    raise ValueError("invalid prediction bounds")
                for kind, value in (("LOAD_W", load), ("AVAILABLE_PILES", available), ("CONGESTION_SCORE", congestion)):
                    points.append({"prediction_type": kind, "predicted_for": utc_time(int(item["predicted_for_epoch"])), "predicted_value": value})
            entries.append({"station_ref": references[local_station_id], "payload": {
                "horizon_hours": horizon_hours, "model_version": model_version,
                "generated_at": utc_time(generated_at_epoch), "points": points}})
    return sorted(entries, key=lambda item: (
        item["station_ref"]["data_source"], item["station_ref"]["external_id"],
        item["payload"]["horizon_hours"],
    ))


def main(predictions_path, catalog_path, output, model_version=None):
    predictions = json.loads(predictions_path.read_text(encoding="utf-8"))
    catalog = json.loads(catalog_path.read_text(encoding="utf-8-sig"))
    entries = build_entries(predictions["predictions"], predictions["generated_at_epoch"],
                            model_version or predictions.get("model_version", "pklot-ridge-v3"), catalog_references(catalog))
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps({
        "prediction_endpoint": PREDICTION_ENDPOINT,
        "entries": entries,
    }, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    if len(sys.argv) not in (4, 5):
        raise SystemExit("usage: export_predictions_api.py PREDICTIONS_JSON STATIONS_JSON OUTPUT_JSON [MODEL_VERSION]")
    main(Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3]),
         *(sys.argv[4:] or [None]))
