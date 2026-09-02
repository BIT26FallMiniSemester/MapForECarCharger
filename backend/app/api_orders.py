from math import asin, cos, radians, sin, sqrt
from typing import Annotated

from fastapi import APIRouter, Depends, Query, Request
from sqlalchemy import case, func, or_, select
from sqlalchemy.orm import selectinload

from app.dependencies import DbSession, current_user
from app.enums import (
    ACTIVE_ORDER_STATUSES,
    ONLINE_PILE_STATUSES,
    OrderStatus,
    PileStatus,
    PileType,
    StationStatus,
)
from app.errors import RESOURCE_NOT_FOUND, error
from app.helpers import order_data, page_data, pile_data, station_data
from app.models import ChargingOrder, ChargingPile, Station, User
from app.responses import ApiEnvelope, success
from app.schemas import CancelOrder, OrderCreate
from app.services import (
    cancel_order,
    create_order,
    expire_reservations,
    get_owned_order,
    reserve_order,
    settle_order,
    start_order,
    stop_order,
)

router = APIRouter(tags=["stations, piles and orders"])


def _station_stats(db, station_id: int) -> dict:
    row = db.execute(
        select(
            func.count(ChargingPile.id),
            func.sum(case((ChargingPile.status == PileStatus.IDLE, 1), else_=0)),
            func.sum(case((ChargingPile.status.in_(ONLINE_PILE_STATUSES), 1), else_=0)),
        ).where(ChargingPile.station_id == station_id)
    ).one()
    total, available, online = int(row[0] or 0), int(row[1] or 0), int(row[2] or 0)
    return {
        "total_piles": total,
        "available_piles": available,
        "online_rate": round(online * 100 / total, 2) if total else 0.0,
    }


def _distance_km(lat1: float, lon1: float, lat2: float, lon2: float) -> float:
    d_lat = radians(lat2 - lat1)
    d_lon = radians(lon2 - lon1)
    value = (
        sin(d_lat / 2) ** 2
        + cos(radians(lat1)) * cos(radians(lat2)) * sin(d_lon / 2) ** 2
    )
    return 6371.0088 * 2 * asin(sqrt(value))


def _pile_totals(db, pile_id: int) -> dict:
    ended = (OrderStatus.UNPAID, OrderStatus.COMPLETED)
    row = db.execute(
        select(
            func.count(ChargingOrder.id),
            func.coalesce(func.sum(ChargingOrder.duration_seconds), 0),
        ).where(ChargingOrder.pile_id == pile_id, ChargingOrder.status.in_(ended))
    ).one()
    return {
        "total_charge_count": int(row[0] or 0),
        "total_charge_duration_seconds": int(row[1] or 0),
    }


@router.get("/stations/nearby")
def nearby_stations(
    request: Request,
    db: DbSession,
    _user: Annotated[User, Depends(current_user)],
    latitude: Annotated[float, Query(ge=-90, le=90)],
    longitude: Annotated[float, Query(ge=-180, le=180)],
    radius_km: Annotated[float, Query(ge=0.1, le=100)] = 10,
    available_only: bool = False,
    limit: Annotated[int, Query(ge=1, le=100)] = 20,
) -> ApiEnvelope:
    stations = db.scalars(
        select(Station).where(
            Station.status == StationStatus.ACTIVE,
            Station.latitude.is_not(None),
            Station.longitude.is_not(None),
        )
    ).all()
    items = []
    for station in stations:
        stats = _station_stats(db, station.id)
        if available_only and stats["available_piles"] == 0:
            continue
        distance = _distance_km(
            latitude,
            longitude,
            float(station.latitude),
            float(station.longitude),
        )
        if distance <= radius_km:
            items.append(station_data(station, stats, round(distance, 2)))
    items.sort(key=lambda item: item["distance_km"])
    return success(request, items[:limit])


@router.get("/stations")
def station_list(
    request: Request,
    db: DbSession,
    _user: Annotated[User, Depends(current_user)],
    keyword: str | None = None,
    operator_name: str | None = None,
    district: str | None = None,
    region_scope: str | None = None,
    location_type: str | None = None,
    has_coordinates: bool | None = None,
    bookable_only: bool = False,
    page: Annotated[int, Query(ge=1)] = 1,
    page_size: Annotated[int, Query(ge=1, le=100)] = 20,
) -> ApiEnvelope:
    filters = [Station.status == StationStatus.ACTIVE]
    if keyword:
        pattern = f"%{keyword.strip()}%"
        filters.append(
            or_(
                Station.name.ilike(pattern),
                Station.address.ilike(pattern),
                Station.operator_name.ilike(pattern),
            )
        )
    if operator_name:
        filters.append(Station.operator_name == operator_name.strip())
    if district:
        filters.append(Station.district == district.strip())
    if region_scope:
        filters.append(Station.region_scope == region_scope.strip())
    if location_type:
        filters.append(Station.location_type == location_type.strip())
    if has_coordinates is True:
        filters.extend((Station.latitude.is_not(None), Station.longitude.is_not(None)))
    elif has_coordinates is False:
        filters.append(or_(Station.latitude.is_(None), Station.longitude.is_(None)))
    if bookable_only:
        filters.extend(
            (
                Station.price_cents_per_kwh.is_not(None),
                Station.piles.any(ChargingPile.status == PileStatus.IDLE),
            )
        )
    total = db.scalar(select(func.count()).select_from(Station).where(*filters)) or 0
    stations = db.scalars(
        select(Station)
        .where(*filters)
        .order_by(Station.id)
        .offset((page - 1) * page_size)
        .limit(page_size)
    ).all()
    items = [station_data(item, _station_stats(db, item.id)) for item in stations]
    return success(request, page_data(items, page, page_size, total))


@router.get("/stations/filter-options")
def station_filter_options(
    request: Request,
    db: DbSession,
    _user: Annotated[User, Depends(current_user)],
) -> ApiEnvelope:
    def distinct_values(column) -> list[str]:
        return list(
            db.scalars(
                select(column)
                .where(column.is_not(None), Station.status == StationStatus.ACTIVE)
                .distinct()
                .order_by(column)
            ).all()
        )

    total = (
        db.scalar(
            select(func.count())
            .select_from(Station)
            .where(Station.status == StationStatus.ACTIVE)
        )
        or 0
    )
    with_coordinates = (
        db.scalar(
            select(func.count())
            .select_from(Station)
            .where(
                Station.status == StationStatus.ACTIVE,
                Station.latitude.is_not(None),
                Station.longitude.is_not(None),
            )
        )
        or 0
    )
    return success(
        request,
        {
            "operator_names": distinct_values(Station.operator_name),
            "districts": distinct_values(Station.district),
            "region_scopes": distinct_values(Station.region_scope),
            "location_types": distinct_values(Station.location_type),
            "station_count": total,
            "with_coordinates_count": with_coordinates,
            "without_coordinates_count": total - with_coordinates,
        },
    )


@router.get("/stations/{station_id}")
def station_detail(
    station_id: int,
    request: Request,
    db: DbSession,
    _user: Annotated[User, Depends(current_user)],
) -> ApiEnvelope:
    station = db.get(Station, station_id)
    if station is None:
        raise error(RESOURCE_NOT_FOUND, {"resource": "station", "id": station_id})
    return success(request, station_data(station, _station_stats(db, station.id)))


@router.get("/stations/{station_id}/piles")
def station_piles(
    station_id: int,
    request: Request,
    db: DbSession,
    _user: Annotated[User, Depends(current_user)],
    status: PileStatus | None = None,
    pile_type: PileType | None = None,
    page: Annotated[int, Query(ge=1)] = 1,
    page_size: Annotated[int, Query(ge=1, le=100)] = 20,
) -> ApiEnvelope:
    if db.get(Station, station_id) is None:
        raise error(RESOURCE_NOT_FOUND, {"resource": "station", "id": station_id})
    filters = [ChargingPile.station_id == station_id]
    if status:
        filters.append(ChargingPile.status == status)
    if pile_type:
        filters.append(ChargingPile.pile_type == pile_type)
    total = (
        db.scalar(select(func.count()).select_from(ChargingPile).where(*filters)) or 0
    )
    piles = db.scalars(
        select(ChargingPile)
        .options(selectinload(ChargingPile.station))
        .where(*filters)
        .order_by(ChargingPile.pile_no)
        .offset((page - 1) * page_size)
        .limit(page_size)
    ).all()
    items = [pile_data(item, _pile_totals(db, item.id)) for item in piles]
    return success(request, page_data(items, page, page_size, total))


@router.get("/piles/{pile_id}")
def pile_detail(
    pile_id: int,
    request: Request,
    db: DbSession,
    _user: Annotated[User, Depends(current_user)],
) -> ApiEnvelope:
    pile = db.scalar(
        select(ChargingPile)
        .options(selectinload(ChargingPile.station))
        .where(ChargingPile.id == pile_id)
    )
    if pile is None:
        raise error(RESOURCE_NOT_FOUND, {"resource": "pile", "id": pile_id})
    return success(request, pile_data(pile, _pile_totals(db, pile.id)))


@router.post("/orders", status_code=201)
def new_order(
    payload: OrderCreate,
    request: Request,
    db: DbSession,
    user: Annotated[User, Depends(current_user)],
) -> ApiEnvelope:
    return success(request, order_data(create_order(db, user, payload.pile_id)))


@router.post("/orders/{order_id}/reserve")
def reserve(
    order_id: int,
    request: Request,
    db: DbSession,
    user: Annotated[User, Depends(current_user)],
) -> ApiEnvelope:
    return success(
        request, order_data(reserve_order(db, get_owned_order(db, order_id, user.id)))
    )


@router.post("/orders/{order_id}/start")
def start(
    order_id: int,
    request: Request,
    db: DbSession,
    user: Annotated[User, Depends(current_user)],
) -> ApiEnvelope:
    return success(
        request, order_data(start_order(db, get_owned_order(db, order_id, user.id)))
    )


@router.post("/orders/{order_id}/stop")
def stop(
    order_id: int,
    request: Request,
    db: DbSession,
    user: Annotated[User, Depends(current_user)],
) -> ApiEnvelope:
    return success(
        request, order_data(stop_order(db, get_owned_order(db, order_id, user.id)))
    )


@router.post("/orders/{order_id}/settle")
def settle(
    order_id: int,
    request: Request,
    db: DbSession,
    user: Annotated[User, Depends(current_user)],
) -> ApiEnvelope:
    order = settle_order(db, get_owned_order(db, order_id, user.id), user)
    return success(
        request, {"order": order_data(order), "balance_cents": user.balance_cents}
    )


@router.post("/orders/{order_id}/cancel")
def cancel(
    payload: CancelOrder,
    order_id: int,
    request: Request,
    db: DbSession,
    user: Annotated[User, Depends(current_user)],
) -> ApiEnvelope:
    return success(
        request,
        order_data(
            cancel_order(db, get_owned_order(db, order_id, user.id), payload.reason)
        ),
    )


@router.get("/orders")
def orders(
    request: Request,
    db: DbSession,
    user: Annotated[User, Depends(current_user)],
    status: OrderStatus | None = None,
    page: Annotated[int, Query(ge=1)] = 1,
    page_size: Annotated[int, Query(ge=1, le=100)] = 20,
) -> ApiEnvelope:
    expire_reservations(db, user_id=user.id)
    filters = [ChargingOrder.user_id == user.id]
    if status:
        filters.append(ChargingOrder.status == status)
    total = (
        db.scalar(select(func.count()).select_from(ChargingOrder).where(*filters)) or 0
    )
    result = db.scalars(
        select(ChargingOrder)
        .options(selectinload(ChargingOrder.station), selectinload(ChargingOrder.pile))
        .where(*filters)
        .order_by(ChargingOrder.created_at.desc())
        .offset((page - 1) * page_size)
        .limit(page_size)
    ).all()
    return success(
        request,
        page_data([order_data(item) for item in result], page, page_size, total),
    )


@router.get("/orders/active")
def active_order(
    request: Request, db: DbSession, user: Annotated[User, Depends(current_user)]
) -> ApiEnvelope:
    expire_reservations(db, user_id=user.id)
    order = db.scalar(
        select(ChargingOrder)
        .options(selectinload(ChargingOrder.station), selectinload(ChargingOrder.pile))
        .where(
            ChargingOrder.user_id == user.id,
            ChargingOrder.status.in_(ACTIVE_ORDER_STATUSES),
        )
    )
    return success(request, order_data(order) if order else None)


@router.get("/orders/{order_id}")
def order_detail(
    order_id: int,
    request: Request,
    db: DbSession,
    user: Annotated[User, Depends(current_user)],
) -> ApiEnvelope:
    expire_reservations(db, user_id=user.id, order_id=order_id)
    return success(request, order_data(get_owned_order(db, order_id, user.id)))
