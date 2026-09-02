from typing import Any

from fastapi import Request
from pydantic import BaseModel, ConfigDict


class ApiEnvelope(BaseModel):
    model_config = ConfigDict(extra="allow")

    code: int = 0
    message: str = "success"
    data: Any = None
    request_id: str


def success(
    request: Request, data: Any = None, message: str = "success"
) -> ApiEnvelope:
    return ApiEnvelope(
        code=0,
        message=message,
        data=data,
        request_id=request.state.request_id,
    )
