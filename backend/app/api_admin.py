from datetime import timedelta
from typing import Annotated

from fastapi import APIRouter, Depends, Query, Request
from sqlalchemy import and_, case, extract, func, or_, select
from sqlalchemy.orm import selectinload

from app.admin_services import (
    add_pile,
    create_station,
    restart_pile,
    set_pile_status,
    update_station,
)
from app.dependencies import DbSession, current_admin, current_user
from app.enums import (
    ONLINE_PILE_STATUSES,
    OrderStatus,
    PileStatus,
    PileType,
    StationStatus,
    UserStatus,
)
from app.errors import RESOURCE_NOT_FOUND, error
from app.helpers import (
    as_utc,
    order_data,
    page_data,
    pile_data,
    recharge_data,
    station_data,
    user_data,
    utcnow,
)
from app.models import (
    Admin,
    ChargingOrder,
    ChargingPile,
    LoadPrediction,
    PileStatusLog,
    RechargeRecord,
    Station,
    User,
)
from app.query_stats import pile_order_totals, station_pile_stats
from app.responses import ApiEnvelope, success
from app.schemas import (
    PileStatusUpdate,
    StationCreate,
    StationPileCreate,
    StationUpdate,
)
from app.user_admin_services import set_user_status

router = APIRouter(tags=["admin and dashboard"])


def _pile_stats(db, station_id: int | None = None) -> dict:
    if station_id is not None:
        return station_pile_stats(db, (station_id,))[station_id]
    query = select(
        func.count(ChargingPile.id),
        func.sum(case((ChargingPile.status == PileStatus.IDLE, 1), else_=0)),
        func.sum(case((ChargingPile.status.in_(ONLINE_PILE_STATUSES), 1), else_=0)),
    )
    if station_id is not None:
        query = query.where(ChargingPile.station_id == station_id)
    row = db.execute(query).one()
    total, available, online = int(row[0] or 0), int(row[1] or 0), int(row[2] or 0)
    return {
        "total_piles": total,
        "available_piles": available,
        "online_rate": round(online * 100 / total, 2) if total else 0.0,
    }


def _revenue_data(db, days: int) -> dict:
    now = utcnow()
    today = now.replace(hour=0, minute=0, second=0, microsecond=0)
    month = today.replace(day=1)
    completed = ChargingOrder.status == OrderStatus.COMPLETED
    total_revenue = (
        db.scalar(
            select(func.coalesce(func.sum(ChargingOrder.amount_cents), 0)).where(
                completed
            )
        )
        or 0
    )
    today_row = db.execute(
        select(
            func.coalesce(func.sum(ChargingOrder.amount_cents), 0),
            func.count(ChargingOrder.id),
        ).where(completed, ChargingOrder.settled_at >= today)
    ).one()
    month_revenue = (
        db.scalar(
            select(func.coalesce(func.sum(ChargingOrder.amount_cents), 0)).where(
                completed, ChargingOrder.settled_at >= month
            )
        )
        or 0
    )
    total_energy = (
        db.scalar(
            select(func.coalesce(func.sum(ChargingOrder.energy_wh), 0)).where(
                ChargingOrder.status.in_((OrderStatus.UNPAID, OrderStatus.COMPLETED))
            )
        )
        or 0
    )
    start = today - timedelta(days=days - 1)
    rows = db.execute(
        select(
            func.date(ChargingOrder.settled_at),
            func.sum(ChargingOrder.amount_cents),
            func.count(ChargingOrder.id),
        )
        .where(completed, ChargingOrder.settled_at >= start)
        .group_by(func.date(ChargingOrder.settled_at))
    ).all()
    by_date = {str(row[0]): (int(row[1]), int(row[2])) for row in rows}
    trend = []
    for offset in range(days):
        date = (start + timedelta(days=offset)).date().isoformat()
        revenue, count = by_date.get(date, (0, 0))
        trend.append({"date": date, "revenue_cents": revenue, "order_count": count})
    return {
        "today_revenue_cents": int(today_row[0]),
        "month_revenue_cents": int(month_revenue),
        "total_revenue_cents": int(total_revenue),
        "today_order_count": int(today_row[1]),
        "total_energy_wh": int(total_energy),
        "trend": trend,
    }


@router.get("/admin/revenue")
def admin_revenue(
    request: Request,
    db: DbSession,
    _admin: Annotated[Admin, Depends(current_admin)],
    days: Annotated[int, Query()] = 7,
) -> ApiEnvelope:
    if days not in (7, 30):
        from app.errors import INVALID_REQUEST

        raise error(INVALID_REQUEST, {"days": days})
    return success(request, _revenue_data(db, days))


@router.get("/admin/piles")
def admin_piles(
    request: Request,
    db: DbSession,
    _admin: Annotated[Admin, Depends(current_admin)],
    station_id: int | None = None,
    status: PileStatus | None = None,
    pile_type: PileType | None = None,
    keyword: str | None = None,
    page: Annotated[int, Query(ge=1)] = 1,
    page_size: Annotated[int, Query(ge=1, le=100)] = 20,
) -> ApiEnvelope:
    filters = []
    if station_id:
        filters.append(ChargingPile.station_id == station_id)
    if status:
        filters.append(ChargingPile.status == status)
    if pile_type:
        filters.append(ChargingPile.pile_type == pile_type)
    if keyword:
        filters.append(ChargingPile.pile_no.ilike(f"%{keyword.strip()}%"))
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
    totals_by_pile = pile_order_totals(db, (item.id for item in piles))
    return success(
        request,
        page_data(
            [pile_data(item, totals_by_pile[item.id]) for item in piles],
            page,
            page_size,
            total,
        ),
    )


@router.get("/admin/piles/{pile_id}")
def admin_pile_detail(
    pile_id: int,
    request: Request,
    db: DbSession,
    _admin: Annotated[Admin, Depends(current_admin)],
) -> ApiEnvelope:
    pile = db.scalar(
        select(ChargingPile)
        .options(selectinload(ChargingPile.station))
        .where(ChargingPile.id == pile_id)
    )
    if pile is None:
        raise error(RESOURCE_NOT_FOUND, {"resource": "pile", "id": pile_id})
    logs = db.scalars(
        select(PileStatusLog)
        .where(PileStatusLog.pile_id == pile_id)
        .order_by(PileStatusLog.created_at.desc())
        .limit(20)
    ).all()
    data = pile_data(pile, pile_order_totals(db, (pile.id,))[pile.id])
    data["recent_status_logs"] = [
        {
            "old_status": item.old_status.value,
            "new_status": item.new_status.value,
            "source": item.source.value,
            "reason": item.reason,
            "created_at": as_utc(item.created_at),
        }
        for item in logs
    ]
    return success(request, data)


@router.post("/admin/piles/{pile_id}/restart")
def admin_restart_pile(
    pile_id: int,
    request: Request,
    db: DbSession,
    admin: Annotated[Admin, Depends(current_admin)],
) -> ApiEnvelope:
    pile = db.get(ChargingPile, pile_id)
    if pile is None:
        raise error(RESOURCE_NOT_FOUND, {"resource": "pile", "id": pile_id})
    return success(request, pile_data(restart_pile(db, admin, pile)))


@router.patch("/admin/piles/{pile_id}/status")
def admin_set_pile_status(
    payload: PileStatusUpdate,
    pile_id: int,
    request: Request,
    db: DbSession,
    admin: Annotated[Admin, Depends(current_admin)],
) -> ApiEnvelope:
    pile = db.get(ChargingPile, pile_id)
    if pile is None:
        raise error(RESOURCE_NOT_FOUND, {"resource": "pile", "id": pile_id})
    return success(request, pile_data(set_pile_status(db, admin, pile, payload)))


@router.get("/admin/stations")
def admin_stations(
    request: Request,
    db: DbSession,
    _admin: Annotated[Admin, Depends(current_admin)],
    keyword: str | None = None,
    status: StationStatus | None = None,
    operator_name: str | None = None,
    district: str | None = None,
    data_source: str | None = None,
    has_coordinates: bool | None = None,
    page: Annotated[int, Query(ge=1)] = 1,
    page_size: Annotated[int, Query(ge=1, le=100)] = 20,
) -> ApiEnvelope:
    filters = []
    if keyword:
        pattern = f"%{keyword.strip()}%"
        filters.append(
            or_(
                Station.name.ilike(pattern),
                Station.address.ilike(pattern),
                Station.operator_name.ilike(pattern),
            )
        )
    if status:
        filters.append(Station.status == status)
    if operator_name:
        filters.append(Station.operator_name == operator_name.strip())
    if district:
        filters.append(Station.district == district.strip())
    if data_source:
        filters.append(Station.data_source == data_source.strip())
    if has_coordinates is True:
        filters.extend((Station.latitude.is_not(None), Station.longitude.is_not(None)))
    elif has_coordinates is False:
        filters.append(or_(Station.latitude.is_(None), Station.longitude.is_(None)))
    total = db.scalar(select(func.count()).select_from(Station).where(*filters)) or 0
    stations = db.scalars(
        select(Station)
        .where(*filters)
        .order_by(Station.id)
        .offset((page - 1) * page_size)
        .limit(page_size)
    ).all()
    stats_by_station = station_pile_stats(db, (item.id for item in stations))
    return success(
        request,
        page_data(
            [station_data(item, stats_by_station[item.id]) for item in stations],
            page,
            page_size,
            total,
        ),
    )


@router.post("/admin/stations", status_code=201)
def admin_create_station(
    payload: StationCreate,
    request: Request,
    db: DbSession,
    admin: Annotated[Admin, Depends(current_admin)],
) -> ApiEnvelope:
    station = create_station(db, admin, payload)
    return success(request, station_data(station, _pile_stats(db, station.id)))


@router.put("/admin/stations/{station_id}")
def admin_update_station(
    payload: StationUpdate,
    station_id: int,
    request: Request,
    db: DbSession,
    admin: Annotated[Admin, Depends(current_admin)],
) -> ApiEnvelope:
    station = db.get(Station, station_id)
    if station is None:
        raise error(RESOURCE_NOT_FOUND, {"resource": "station", "id": station_id})
    station = update_station(db, admin, station, payload)
    return success(request, station_data(station, _pile_stats(db, station.id)))


@router.post("/admin/stations/{station_id}/piles", status_code=201)
def admin_add_pile(
    payload: StationPileCreate,
    station_id: int,
    request: Request,
    db: DbSession,
    admin: Annotated[Admin, Depends(current_admin)],
) -> ApiEnvelope:
    station = db.get(Station, station_id)
    if station is None:
        raise error(RESOURCE_NOT_FOUND, {"resource": "station", "id": station_id})
    return success(request, pile_data(add_pile(db, admin, station, payload)))


@router.get("/admin/users")
def admin_users(
    request: Request,
    db: DbSession,
    _admin: Annotated[Admin, Depends(current_admin)],
    phone_keyword: str | None = None,
    status: UserStatus | None = None,
    page: Annotated[int, Query(ge=1)] = 1,
    page_size: Annotated[int, Query(ge=1, le=100)] = 20,
) -> ApiEnvelope:
    filters = []
    if phone_keyword:
        filters.append(User.phone.ilike(f"%{phone_keyword.strip()}%"))
    if status:
        filters.append(User.status == status)
    total = db.scalar(select(func.count()).select_from(User).where(*filters)) or 0
    users = db.scalars(
        select(User)
        .where(*filters)
        .order_by(User.created_at.desc())
        .offset((page - 1) * page_size)
        .limit(page_size)
    ).all()
    return success(
        request, page_data([user_data(item) for item in users], page, page_size, total)
    )


@router.get("/admin/users/{user_id}")
def admin_user_detail(
    user_id: int,
    request: Request,
    db: DbSession,
    _admin: Annotated[Admin, Depends(current_admin)],
) -> ApiEnvelope:
    user = db.get(User, user_id)
    if user is None:
        raise error(RESOURCE_NOT_FOUND, {"resource": "user", "id": user_id})
    order_count = (
        db.scalar(
            select(func.count())
            .select_from(ChargingOrder)
            .where(ChargingOrder.user_id == user.id)
        )
        or 0
    )
    spent = (
        db.scalar(
            select(func.coalesce(func.sum(ChargingOrder.amount_cents), 0)).where(
                ChargingOrder.user_id == user.id,
                ChargingOrder.status == OrderStatus.COMPLETED,
            )
        )
        or 0
    )
    orders = db.scalars(
        select(ChargingOrder)
        .options(selectinload(ChargingOrder.station), selectinload(ChargingOrder.pile))
        .where(ChargingOrder.user_id == user.id)
        .order_by(ChargingOrder.created_at.desc())
        .limit(5)
    ).all()
    records = db.scalars(
        select(RechargeRecord)
        .where(RechargeRecord.user_id == user.id)
        .order_by(RechargeRecord.created_at.desc())
        .limit(5)
    ).all()
    data = user_data(user)
    data.update(
        {
            "order_count": order_count,
            "total_spent_cents": int(spent),
            "recent_orders": [order_data(item) for item in orders],
            "recent_recharge_records": [recharge_data(item) for item in records],
        }
    )
    return success(request, data)


@router.post("/admin/users/{user_id}/freeze")
def freeze_user(
    user_id: int,
    request: Request,
    db: DbSession,
    admin: Annotated[Admin, Depends(current_admin)],
) -> ApiEnvelope:
    user = db.get(User, user_id)
    if user is None:
        raise error(RESOURCE_NOT_FOUND, {"resource": "user", "id": user_id})
    return success(
        request, user_data(set_user_status(db, admin, user, UserStatus.FROZEN))
    )


@router.post("/admin/users/{user_id}/unfreeze")
def unfreeze_user(
    user_id: int,
    request: Request,
    db: DbSession,
    admin: Annotated[Admin, Depends(current_admin)],
) -> ApiEnvelope:
    user = db.get(User, user_id)
    if user is None:
        raise error(RESOURCE_NOT_FOUND, {"resource": "user", "id": user_id})
    return success(
        request, user_data(set_user_status(db, admin, user, UserStatus.NORMAL))
    )


@router.get("/dashboard/overview")
def dashboard_overview(request: Request, db: DbSession) -> ApiEnvelope:
    revenue = _revenue_data(db, 7)
    today = utcnow().replace(hour=0, minute=0, second=0, microsecond=0)
    today_energy = (
        db.scalar(
            select(func.coalesce(func.sum(ChargingOrder.energy_wh), 0)).where(
                ChargingOrder.stopped_at >= today
            )
        )
        or 0
    )
    total_orders = (
        db.scalar(
            select(func.count())
            .select_from(ChargingOrder)
            .where(ChargingOrder.status == OrderStatus.COMPLETED)
        )
        or 0
    )
    charging = (
        db.scalar(
            select(func.count())
            .select_from(ChargingOrder)
            .where(ChargingOrder.status == OrderStatus.CHARGING)
        )
        or 0
    )
    users = db.scalar(select(func.count()).select_from(User)) or 0
    station_filter = Station.status == StationStatus.ACTIVE
    stations = (
        db.scalar(select(func.count()).select_from(Station).where(station_filter)) or 0
    )
    operational_stations = (
        db.scalar(
            select(func.count())
            .select_from(Station)
            .where(
                station_filter,
                Station.piles.any(),
            )
        )
        or 0
    )
    public_catalog_stations = (
        db.scalar(
            select(func.count())
            .select_from(Station)
            .where(
                station_filter,
                Station.data_source.is_not(None),
            )
        )
        or 0
    )
    geocoded_stations = (
        db.scalar(
            select(func.count())
            .select_from(Station)
            .where(
                station_filter,
                Station.latitude.is_not(None),
                Station.longitude.is_not(None),
            )
        )
        or 0
    )
    piles = _pile_stats(db)
    return success(
        request,
        {
            "today_revenue_cents": revenue["today_revenue_cents"],
            "total_revenue_cents": revenue["total_revenue_cents"],
            "today_order_count": revenue["today_order_count"],
            "total_order_count": total_orders,
            "today_energy_wh": int(today_energy),
            "total_energy_wh": revenue["total_energy_wh"],
            "charging_order_count": charging,
            "user_count": users,
            "station_count": stations,
            "operational_station_count": operational_stations,
            "public_catalog_station_count": public_catalog_stations,
            "geocoded_station_count": geocoded_stations,
            "pile_count": piles["total_piles"],
            "available_pile_count": piles["available_piles"],
            "online_rate": piles["online_rate"],
            "updated_at": as_utc(utcnow()),
        },
    )


@router.get("/dashboard/stations-map")
def dashboard_stations_map(request: Request, db: DbSession) -> ApiEnvelope:
    rows = db.execute(
        select(
            Station.id,
            Station.name,
            Station.latitude,
            Station.longitude,
            func.count(ChargingPile.id),
            func.sum(case((ChargingPile.status == PileStatus.IDLE, 1), else_=0)),
            func.sum(case((ChargingPile.status == PileStatus.RESERVED, 1), else_=0)),
            func.sum(case((ChargingPile.status == PileStatus.CHARGING, 1), else_=0)),
            func.sum(case((ChargingPile.status == PileStatus.FAULT, 1), else_=0)),
            func.sum(case((ChargingPile.status == PileStatus.OFFLINE, 1), else_=0)),
            func.sum(case((ChargingPile.status.in_(ONLINE_PILE_STATUSES), 1), else_=0)),
        )
        .outerjoin(ChargingPile, ChargingPile.station_id == Station.id)
        .where(
            Station.status == StationStatus.ACTIVE,
            Station.latitude.is_not(None),
            Station.longitude.is_not(None),
        )
        .group_by(Station.id, Station.name, Station.latitude, Station.longitude)
        .order_by(Station.id)
    ).all()
    items = []
    for row in rows:
        total = int(row[4] or 0)
        reserved = int(row[6] or 0)
        charging = int(row[7] or 0)
        online = int(row[10] or 0)
        items.append(
            {
                "station_id": row[0],
                "station_name": row[1],
                "latitude": float(row[2]),
                "longitude": float(row[3]),
                "total_piles": total,
                "available_piles": int(row[5] or 0),
                "reserved_piles": reserved,
                "charging_piles": charging,
                "fault_piles": int(row[8] or 0),
                "offline_piles": int(row[9] or 0),
                "online_rate": round(online * 100 / total, 2) if total else 0.0,
                "utilization_rate": round((reserved + charging) * 100 / total, 2)
                if total
                else 0.0,
            }
        )
    return success(request, {"items": items})


@router.get("/dashboard/hourly-demand")
def dashboard_hourly_demand(
    request: Request,
    db: DbSession,
    days: Annotated[int, Query(ge=1, le=365)] = 30,
) -> ApiEnvelope:
    today = utcnow().replace(hour=0, minute=0, second=0, microsecond=0)
    start = today - timedelta(days=days - 1)
    end = today + timedelta(days=1)
    hour = extract("hour", ChargingOrder.started_at)
    rows = db.execute(
        select(
            hour,
            func.count(ChargingOrder.id),
            func.coalesce(func.sum(ChargingOrder.energy_wh), 0),
        )
        .where(
            ChargingOrder.status.in_(
                (OrderStatus.CHARGING, OrderStatus.UNPAID, OrderStatus.COMPLETED)
            ),
            ChargingOrder.started_at >= start,
            ChargingOrder.started_at < end,
        )
        .group_by(hour)
    ).all()
    by_hour = {
        int(row[0]): {"order_count": int(row[1]), "energy_wh": int(row[2])}
        for row in rows
    }
    return success(
        request,
        {
            "days": days,
            "items": [
                {
                    "hour": value,
                    "order_count": by_hour.get(value, {}).get("order_count", 0),
                    "energy_wh": by_hour.get(value, {}).get("energy_wh", 0),
                }
                for value in range(24)
            ],
        },
    )


@router.get("/dashboard/alerts")
def dashboard_alerts(
    request: Request,
    db: DbSession,
    limit: Annotated[int, Query(ge=1, le=100)] = 20,
) -> ApiEnvelope:
    rows = db.execute(
        select(PileStatusLog, ChargingPile.pile_no, ChargingPile.station_id)
        .join(ChargingPile, ChargingPile.id == PileStatusLog.pile_id)
        .where(
            or_(
                PileStatusLog.new_status.in_((PileStatus.FAULT, PileStatus.OFFLINE)),
                and_(
                    PileStatusLog.old_status.in_(
                        (PileStatus.FAULT, PileStatus.OFFLINE)
                    ),
                    PileStatusLog.new_status == PileStatus.IDLE,
                ),
            )
        )
        .order_by(PileStatusLog.created_at.desc(), PileStatusLog.id.desc())
        .limit(limit)
    ).all()
    items = []
    for log, pile_no, station_id in rows:
        if log.new_status == PileStatus.FAULT:
            level, alert_type, title = "ERROR", "PILE_FAULT", "充电桩故障"
            message = f"{pile_no} 进入故障状态"
        elif log.new_status == PileStatus.OFFLINE:
            level, alert_type, title = "WARNING", "PILE_OFFLINE", "充电桩离线"
            message = f"{pile_no} 进入离线状态"
        else:
            level, alert_type, title = "INFO", "PILE_RECOVERED", "充电桩恢复"
            message = f"{pile_no} 已恢复空闲状态"
        items.append(
            {
                "id": log.id,
                "level": level,
                "type": alert_type,
                "title": title,
                "message": message,
                "station_id": station_id,
                "pile_id": log.pile_id,
                "occurred_at": as_utc(log.created_at),
            }
        )
    return success(request, {"items": items})


@router.get("/dashboard/revenue-trend")
def dashboard_revenue(
    request: Request, db: DbSession, days: Annotated[int, Query()] = 30
) -> ApiEnvelope:
    if days not in (7, 30):
        from app.errors import INVALID_REQUEST

        raise error(INVALID_REQUEST, {"days": days})
    return success(request, {"days": days, "items": _revenue_data(db, days)["trend"]})


@router.get("/dashboard/pile-status")
def dashboard_pile_status(request: Request, db: DbSession) -> ApiEnvelope:
    total = db.scalar(select(func.count()).select_from(ChargingPile)) or 0
    rows = dict(
        db.execute(
            select(ChargingPile.status, func.count()).group_by(ChargingPile.status)
        ).all()
    )
    items = [
        {
            "status": status.value,
            "count": int(rows.get(status, 0)),
            "percentage": round(int(rows.get(status, 0)) * 100 / total, 2)
            if total
            else 0.0,
        }
        for status in PileStatus
    ]
    return success(request, {"total": total, "items": items})


@router.get("/dashboard/station-ranking")
def station_ranking(
    request: Request,
    db: DbSession,
    metric: Annotated[
        str, Query(pattern="^(revenue|order_count|energy|utilization)$")
    ] = "revenue",
    days: Annotated[int, Query(ge=1, le=365)] = 30,
    limit: Annotated[int, Query(ge=1, le=100)] = 10,
) -> ApiEnvelope:
    since = utcnow() - timedelta(days=days)
    rows = db.execute(
        select(
            Station.id,
            Station.name,
            func.coalesce(func.sum(ChargingOrder.amount_cents), 0),
            func.count(ChargingOrder.id),
            func.coalesce(func.sum(ChargingOrder.energy_wh), 0),
            func.coalesce(func.sum(ChargingOrder.duration_seconds), 0),
        )
        .outerjoin(
            ChargingOrder,
            (ChargingOrder.station_id == Station.id)
            & (ChargingOrder.status == OrderStatus.COMPLETED)
            & (ChargingOrder.settled_at >= since),
        )
        .where(Station.piles.any())
        .group_by(Station.id)
    ).all()
    stats_by_station = station_pile_stats(db, (row[0] for row in rows))
    items = []
    for row in rows:
        pile_count = stats_by_station[row[0]]["total_piles"]
        capacity_seconds = pile_count * days * 86400
        items.append(
            {
                "station_id": row[0],
                "station_name": row[1],
                "revenue_cents": int(row[2]),
                "order_count": int(row[3]),
                "energy_wh": int(row[4]),
                "utilization_rate": round(int(row[5]) * 100 / capacity_seconds, 2)
                if capacity_seconds
                else 0.0,
            }
        )
    key = {
        "revenue": "revenue_cents",
        "order_count": "order_count",
        "energy": "energy_wh",
        "utilization": "utilization_rate",
    }[metric]
    items.sort(key=lambda item: item[key], reverse=True)
    return success(request, {"metric": metric, "days": days, "items": items[:limit]})


@router.get("/predictions/load")
def load_predictions(
    request: Request,
    db: DbSession,
    station_id: int | None = None,
    horizon_hours: Annotated[int, Query()] = 6,
) -> ApiEnvelope:
    if horizon_hours not in (1, 6, 24):
        from app.errors import INVALID_REQUEST

        raise error(INVALID_REQUEST, {"horizon_hours": horizon_hours})
    filters = [LoadPrediction.horizon_hours == horizon_hours]
    filters.append(
        LoadPrediction.station_id == station_id
        if station_id is not None
        else LoadPrediction.station_id.is_(None)
    )
    latest = db.scalar(
        select(LoadPrediction)
        .where(*filters)
        .order_by(LoadPrediction.generated_at.desc(), LoadPrediction.id.desc())
        .limit(1)
    )
    records = (
        db.scalars(
            select(LoadPrediction)
            .where(
                *filters,
                LoadPrediction.model_version == latest.model_version,
                LoadPrediction.generated_at == latest.generated_at,
            )
            .order_by(LoadPrediction.predicted_for)
        ).all()
        if latest is not None
        else []
    )
    grouped = {}
    for item in records:
        key = as_utc(item.predicted_for)
        grouped.setdefault(key, {"predicted_for": key})
        field = {
            "LOAD_W": "load_w",
            "AVAILABLE_PILES": "available_piles",
            "CONGESTION_SCORE": "congestion_score",
        }[item.prediction_type.value]
        grouped[key][field] = float(item.predicted_value)
    return success(
        request,
        {
            "station_id": station_id,
            "horizon_hours": horizon_hours,
            "model_version": latest.model_version if latest else None,
            "generated_at": as_utc(latest.generated_at) if latest else None,
            "points": list(grouped.values()),
        },
    )


@router.get("/recommendations/stations")
def recommendations(
    request: Request,
    db: DbSession,
    _user: Annotated[User, Depends(current_user)],
    latitude: Annotated[float, Query(ge=-90, le=90)],
    longitude: Annotated[float, Query(ge=-180, le=180)],
    radius_km: Annotated[float, Query(ge=0.1, le=100)] = 20,
    limit: Annotated[int, Query(ge=1, le=100)] = 5,
) -> ApiEnvelope:
    from app.api_orders import _distance_km

    stations = db.scalars(
        select(Station).where(
            Station.status == StationStatus.ACTIVE,
            Station.latitude.is_not(None),
            Station.longitude.is_not(None),
        )
    ).all()
    candidates = []
    for station in stations:
        distance = _distance_km(
            latitude, longitude, float(station.latitude), float(station.longitude)
        )
        if distance > radius_km:
            continue
        candidates.append((station, distance))
    stats_by_station = station_pile_stats(
        db, (station.id for station, _distance in candidates)
    )
    items = []
    for station, distance in candidates:
        stats = stats_by_station[station.id]
        free_rate = (
            stats["available_piles"] / stats["total_piles"]
            if stats["total_piles"]
            else 0
        )
        score = round(1 - free_rate, 4)
        data = station_data(station, stats, round(distance, 2))
        data.update(
            {
                "congestion_score": score,
                "recommendation_reason": f"距您 {distance:.1f} km，当前空闲 {stats['available_piles']} 个",
            }
        )
        items.append(data)
    items.sort(key=lambda item: (item["congestion_score"], item["distance_km"]))
    return success(request, {"items": items[:limit]})
