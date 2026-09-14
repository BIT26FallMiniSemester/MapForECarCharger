#!/usr/bin/env bash
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BATCH_ID=${BATCH_ID:-sim-quick-2026-09-14-seed20260914}
INPUT=${INPUT:-$ROOT/runtime/generated/$BATCH_ID}
WAREHOUSE=${WAREHOUSE:-$ROOT/runtime/warehouse}
MASTER=${MASTER:-local[2]}

if [ -f "$HOME/.hadoop_env" ]; then
  # shellcheck disable=SC1091
  . "$HOME/.hadoop_env"
fi

python3 "$ROOT/jobs/run_pipeline.py" \
  --master "$MASTER" \
  --input "$INPUT" \
  --warehouse "$WAREHOUSE" \
  --batch-id "$BATCH_ID"

printf '\nPipeline complete. Start Flask with:\n'
printf 'ADS_ROOT=%s/ads ADS_BATCH_ID=%s python3 %s/flask-api/app.py\n' "$WAREHOUSE" "$BATCH_ID" "$ROOT"
