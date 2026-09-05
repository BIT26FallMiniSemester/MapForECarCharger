from typing import Annotated

from fastapi import APIRouter, Depends, Query, Request
from sqlalchemy import func, select

from app.dependencies import DbSession, require_internal_key
from app.device_services import update_heartbeat
from app.errors import RESOURCE_NOT_FOUND, error
from app.helpers import as_utc, page_data, to_utc_naive, utcnow
from app.models import ChargingOrder, ChargingPile, LoadPrediction, Station
from app.responses import ApiEnvelope, success
from app.schemas import HeartbeatCreate, PredictionsCreate, TelemetryCreate
from app.services import expire_reservations, update_telemetry

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


@router.get("/stations/catalog-mappings")
def station_catalog_mappings(
    request: Request,
    db: DbSession,
    data_source: Annotated[str | None, Query(min_length=1, max_length=64)] = None,
    external_id: Annotated[str | None, Query(min_length=1, max_length=64)] = None,
    page: Annotated[int, Query(ge=1)] = 1,
    page_size: Annotated[int, Query(ge=1, le=1000)] = 500,
) -> ApiEnvelope:
    filters = [Station.data_source.is_not(None), Station.external_id.is_not(None)]
    if data_source is not None:
        filters.append(Station.data_source == data_source.strip())
    if external_id is not None:
        filters.append(Station.external_id == external_id.strip())
    total = db.scalar(select(func.count()).select_from(Station).where(*filters)) or 0
    rows = db.execute(
        select(Station.id, Station.data_source, Station.external_id, Station.name)
        .where(*filters)
        .order_by(Station.data_source, Station.external_id, Station.id)
        .offset((page - 1) * page_size)
        .limit(page_size)
    ).all()
    items = [
        {
            "station_id": row[0],
            "data_source": row[1],
            "external_id": row[2],
            "station_name": row[3],
        }
        for row in rows
    ]
    return success(request, page_data(items, page, page_size, total))


@router.post("/orders/expire-reservations")
def expire_order_reservations(request: Request, db: DbSession) -> ApiEnvelope:
    processed_at = utcnow()
    expired_order_ids = expire_reservations(db, now=processed_at)
    return success(
        request,
        {
            "expired_count": len(expired_order_ids),
            "expired_order_ids": expired_order_ids,
            "processed_at": as_utc(processed_at),
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
            LoadPrediction.predicted_for == to_utc_naive(point.predicted_for),
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
                predicted_for=to_utc_naive(point.predicted_for),
                predicted_value=point.predicted_value,
                model_version=payload.model_version,
                generated_at=to_utc_naive(payload.generated_at),
            )
            db.add(record)
        else:
            record.predicted_value = point.predicted_value
            record.generated_at = to_utc_naive(payload.generated_at)
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
