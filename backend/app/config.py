from functools import lru_cache
from pathlib import Path

from pydantic import Field
from pydantic_settings import BaseSettings, SettingsConfigDict

BACKEND_DIR = Path(__file__).resolve().parents[1]


class Settings(BaseSettings):
    app_name: str = "MapForECarCharger API"
    app_version: str = "0.1.0"
    app_env: str = "development"
    api_prefix: str = "/api/v1"
    database_url: str = (
        f"sqlite:///{BACKEND_DIR / 'runtime' / 'map_for_ecar_charger.db'}"
    )
    jwt_secret: str = Field(
        default="development-only-change-this-secret-32-bytes", min_length=32
    )
    jwt_algorithm: str = "HS256"
    internal_key: str = Field(
        default="development-internal-key-change-me", min_length=32
    )
    user_token_expire_seconds: int = 86_400
    admin_token_expire_seconds: int = 28_800
    reservation_timeout_seconds: int = Field(default=900, ge=1, le=86_400)

    model_config = SettingsConfigDict(
        env_file=BACKEND_DIR / ".env",
        env_file_encoding="utf-8",
        extra="ignore",
    )


@lru_cache
def get_settings() -> Settings:
    return Settings()
