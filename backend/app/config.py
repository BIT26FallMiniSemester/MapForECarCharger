from functools import lru_cache
from pathlib import Path
from typing import Literal

from pydantic import Field, model_validator
from pydantic_settings import BaseSettings, SettingsConfigDict

BACKEND_DIR = Path(__file__).resolve().parents[1]
DEFAULT_JWT_SECRET = "development-only-change-this-secret-32-bytes"
DEFAULT_INTERNAL_KEY = "development-internal-key-change-me"


class Settings(BaseSettings):
    app_name: str = "MapForECarCharger API"
    app_version: str = "0.1.0"
    app_env: Literal["development", "test", "production", "prod"] = "development"
    api_prefix: str = "/api/v1"
    database_url: str = (
        f"sqlite:///{BACKEND_DIR / 'runtime' / 'map_for_ecar_charger.db'}"
    )
    jwt_secret: str = Field(default=DEFAULT_JWT_SECRET, min_length=32)
    jwt_algorithm: str = "HS256"
    internal_key: str = Field(default=DEFAULT_INTERNAL_KEY, min_length=32)
    tencent_map_key: str | None = None
    tencent_map_timeout_seconds: float = Field(default=5.0, gt=0, le=30)
    cors_origins: str = "*"
    user_token_expire_seconds: int = Field(default=86_400, ge=60)
    admin_token_expire_seconds: int = Field(default=28_800, ge=60)
    reservation_timeout_seconds: int = Field(default=900, ge=1, le=86_400)

    @property
    def cors_origin_list(self) -> list[str]:
        return [item.strip() for item in self.cors_origins.split(",") if item.strip()]

    @model_validator(mode="after")
    def reject_insecure_production_defaults(self):
        if self.app_env.lower() not in {"production", "prod"}:
            return self
        if self.jwt_secret == DEFAULT_JWT_SECRET:
            raise ValueError("JWT_SECRET must be changed in production")
        if self.internal_key == DEFAULT_INTERNAL_KEY:
            raise ValueError("INTERNAL_KEY must be changed in production")
        if "*" in self.cors_origin_list:
            raise ValueError("CORS_ORIGINS must be restricted in production")
        return self

    model_config = SettingsConfigDict(
        env_file=BACKEND_DIR / ".env",
        env_file_encoding="utf-8",
        extra="ignore",
    )


@lru_cache
def get_settings() -> Settings:
    return Settings()
