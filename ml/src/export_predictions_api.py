#!/usr/bin/env python3
"""Export ML predictions with stable catalog references for backend posting."""
import json
import math
import sys
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
    for item in predictions:
        local_station_id = int(item["station_id"])
        horizon_hours = int(item["horizon_hours"])
        if local_station_id not in references or horizon_hours not in HORIZONS:
            raise ValueError("unknown station_id or invalid horizon_hours")
        predicted_for = utc_time(int(item["predicted_for_epoch"]))
        entries.append(
            {
                "station_ref": references[local_station_id],
                "payload": {
                    "horizon_hours": horizon_hours,
                    "model_version": model_version,
                    "generated_at": utc_time(generated_at_epoch),
                    "points": [
                        {"prediction_type": "LOAD_W", "predicted_for": predicted_for,
                         "predicted_value": finite_number(item["predicted_load_kw"], "predicted_load_kw") * 1000},
                        {"prediction_type": "AVAILABLE_PILES", "predicted_for": predicted_for,
                         "predicted_value": finite_number(item["predicted_available_piles"], "predicted_available_piles")},
                        {"prediction_type": "CONGESTION_SCORE", "predicted_for": predicted_for,
                         "predicted_value": finite_number(item["congestion_ratio"], "congestion_ratio")},
                    ],
                },
            }
        )
    return sorted(entries, key=lambda item: (
        item["station_ref"]["data_source"], item["station_ref"]["external_id"],
        item["payload"]["horizon_hours"],
    ))


def main(predictions_path, catalog_path, output, model_version="pklot-ridge-v1"):
    predictions = json.loads(predictions_path.read_text(encoding="utf-8"))
    catalog = json.loads(catalog_path.read_text(encoding="utf-8-sig"))
    entries = build_entries(predictions["predictions"], predictions["generated_at_epoch"],
                            model_version, catalog_references(catalog))
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps({
        "prediction_endpoint": PREDICTION_ENDPOINT,
        "entries": entries,
    }, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    if len(sys.argv) not in (4, 5):
        raise SystemExit("usage: export_predictions_api.py PREDICTIONS_JSON STATIONS_JSON OUTPUT_JSON [MODEL_VERSION]")
    main(Path(sys.argv[1]), Path(sys.argv[2]), Path(sys.argv[3]),
         *(sys.argv[4:] or ["pklot-ridge-v1"]))
