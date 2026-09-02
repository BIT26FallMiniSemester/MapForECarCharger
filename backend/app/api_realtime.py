from typing import Annotated

from fastapi import APIRouter, Query, Request
from sqlalchemy import select
from sqlalchemy.orm import selectinload

from app.dependencies import DbSession
from app.enums import OrderStatus
from app.helpers import order_data
from app.models import ChargingOrder
from app.responses import ApiEnvelope, success

router = APIRouter(tags=["dashboard"])


@router.get("/dashboard/realtime-orders")
def realtime_orders(
    request: Request,
    db: DbSession,
    limit: Annotated[int, Query(ge=1, le=100)] = 20,
) -> ApiEnvelope:
    orders = db.scalars(
        select(ChargingOrder)
        .options(selectinload(ChargingOrder.station), selectinload(ChargingOrder.pile))
        .where(ChargingOrder.status == OrderStatus.CHARGING)
        .order_by(ChargingOrder.started_at.desc())
        .limit(limit)
    ).all()
    return success(request, {"items": [order_data(item) for item in orders]})
