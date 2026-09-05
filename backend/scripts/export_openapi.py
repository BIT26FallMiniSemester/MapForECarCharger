from pathlib import Path

import yaml

from app.main import app

repository_root = Path(__file__).resolve().parents[2]
target = repository_root / "contracts" / "openapi.yaml"
target.parent.mkdir(parents=True, exist_ok=True)
target.write_text(
    yaml.safe_dump(app.openapi(), allow_unicode=True, sort_keys=False),
    encoding="utf-8",
)
print(f"OpenAPI contract written to {target}")
