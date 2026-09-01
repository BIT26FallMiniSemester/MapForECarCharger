from fastapi import APIRouter, Depends, Request
from sqlalchemy import select

from app.dependencies import DbSession, require_internal_key
from app.device_services import update_heartbeat
from app.errors import RESOURCE_NOT_FOUND, error
from app.helpers import as_utc
from app.models import ChargingOrder, ChargingPile, LoadPrediction, Station
from app.responses import ApiEnvelope, success
from app.schemas import HeartbeatCreate, PredictionsCreate, TelemetryCreate
from app.services import update_telemetry

router = APIRouter(
    prefix="/internal",
    tags=["internal"],
    dependencies=[Depends(require_internal_key)],
)


@router.post("/piles/{pile_id}/heartbeat")
def heartbeat(
    pile_id: int, payload: HeartbeatCreate, request: Request, db: DbSession
) -> ApiEnvelope:
    pile = db.get(ChargingPile, pile_id)
    if pile is None:
        raise error(RESOURCE_NOT_FOUND, {"resource": "pile", "id": pile_id})
    pile = update_heartbeat(db, pile, payload.reported_at, payload.device_status)
    return success(
        request,
        {
            "pile_id": pile.id,
            "status": pile.status.value,
            "last_heartbeat_at": as_utc(pile.last_heartbeat_at),
        },
    )


@router.post("/orders/{order_id}/telemetry")
def telemetry(
    order_id: int, payload: TelemetryCreate, request: Request, db: DbSession
) -> ApiEnvelope:
    order = db.get(ChargingOrder, order_id)
    if order is None:
        raise error(RESOURCE_NOT_FOUND, {"resource": "order", "id": order_id})
    order = update_telemetry(db, order, payload.reported_at, payload.energy_wh)
    return success(
        request,
        {
            "order_id": order.id,
            "energy_wh": order.energy_wh,
            "reported_at": as_utc(order.updated_at),
        },
    )


@router.post("/predictions/load")
def write_predictions(
    payload: PredictionsCreate, request: Request, db: DbSession
) -> ApiEnvelope:
    if payload.station_id is not None and db.get(Station, payload.station_id) is None:
        raise error(
            RESOURCE_NOT_FOUND, {"resource": "station", "id": payload.station_id}
        )
    written = 0
    for point in payload.points:
        filters = [
            LoadPrediction.prediction_type == point.prediction_type,
            LoadPrediction.horizon_hours == payload.horizon_hours,
            LoadPrediction.predicted_for == point.predicted_for.replace(tzinfo=None),
            LoadPrediction.model_version == payload.model_version,
        ]
        filters.append(
            LoadPrediction.station_id == payload.station_id
            if payload.station_id is not None
            else LoadPrediction.station_id.is_(None)
        )
        record = db.scalar(select(LoadPrediction).where(*filters))
        if record is None:
            record = LoadPrediction(
                station_id=payload.station_id,
                prediction_type=point.prediction_type,
                horizon_hours=payload.horizon_hours,
                predicted_for=point.predicted_for.replace(tzinfo=None),
                predicted_value=point.predicted_value,
                model_version=payload.model_version,
                generated_at=payload.generated_at.replace(tzinfo=None),
            )
            db.add(record)
        else:
            record.predicted_value = point.predicted_value
            record.generated_at = payload.generated_at.replace(tzinfo=None)
        written += 1
    db.commit()
    return success(
        request,
        {
            "station_id": payload.station_id,
            "horizon_hours": payload.horizon_hours,
            "model_version": payload.model_version,
            "points_written": written,
        },
    )
