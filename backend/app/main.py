import logging
from sqlite3 import OperationalError
from uuid import uuid4

from fastapi import FastAPI, Request
from fastapi.exceptions import RequestValidationError
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import JSONResponse
from fastapi.staticfiles import StaticFiles
from sqlalchemy.exc import OperationalError as SqlAlchemyOperationalError

from app.api_admin import router as admin_router
from app.api_avatar import router as avatar_router
from app.api_internal import router as internal_router
from app.api_orders import router as orders_router
from app.api_public import router as public_router
from app.api_realtime import router as realtime_router
from app.config import BACKEND_DIR, get_settings
from app.errors import VALIDATION_ERROR, ApiError
from app.responses import success

logger = logging.getLogger(__name__)
settings = get_settings()
app = FastAPI(title=settings.app_name, version=settings.app_version)
app.add_middleware(
    CORSMiddleware,
    allow_origins=settings.cors_origin_list,
    allow_methods=["*"],
    allow_headers=["*"],
    expose_headers=["X-Request-ID"],
)
avatar_directory = BACKEND_DIR / "runtime" / "avatars"
avatar_directory.mkdir(parents=True, exist_ok=True)
app.mount("/static/avatars", StaticFiles(directory=avatar_directory), name="avatars")


@app.middleware("http")
async def request_context(request: Request, call_next):
    request.state.request_id = (
        request.headers.get("X-Request-ID") or f"req_{uuid4().hex}"
    )
    response = await call_next(request)
    response.headers["X-Request-ID"] = request.state.request_id
    return response


@app.exception_handler(ApiError)
async def api_error_handler(request: Request, exc: ApiError) -> JSONResponse:
    body = {
        "code": exc.code,
        "message": exc.message,
        "data": None,
        "request_id": request.state.request_id,
    }
    if exc.details is not None:
        body["details"] = exc.details
    return JSONResponse(status_code=exc.status_code, content=body)


@app.exception_handler(RequestValidationError)
async def validation_error_handler(
    request: Request, exc: RequestValidationError
) -> JSONResponse:
    details = [
        {"location": list(item["loc"]), "message": item["msg"], "type": item["type"]}
        for item in exc.errors()
    ]
    return JSONResponse(
        status_code=422,
        content={
            "code": VALIDATION_ERROR[0],
            "message": VALIDATION_ERROR[1],
            "data": None,
            "details": {"errors": details},
            "request_id": request.state.request_id,
        },
    )


@app.exception_handler(SqlAlchemyOperationalError)
@app.exception_handler(OperationalError)
async def database_error_handler(request: Request, exc: Exception) -> JSONResponse:
    logger.exception("database unavailable", exc_info=exc)
    return JSONResponse(
        status_code=503,
        content={
            "code": 50001,
            "message": "service unavailable",
            "data": None,
            "request_id": request.state.request_id,
        },
    )


@app.exception_handler(Exception)
async def internal_error_handler(request: Request, exc: Exception) -> JSONResponse:
    logger.exception("unhandled error", exc_info=exc)
    return JSONResponse(
        status_code=500,
        content={
            "code": 50000,
            "message": "internal server error",
            "data": None,
            "request_id": request.state.request_id,
        },
    )


@app.get("/", include_in_schema=False)
def root(request: Request) -> dict:
    return success(request, {"name": settings.app_name, "docs": "/docs"})


app.include_router(public_router, prefix=settings.api_prefix)
app.include_router(avatar_router, prefix=settings.api_prefix)
app.include_router(orders_router, prefix=settings.api_prefix)
app.include_router(realtime_router, prefix=settings.api_prefix)
app.include_router(admin_router, prefix=settings.api_prefix)
app.include_router(internal_router, prefix=settings.api_prefix)
