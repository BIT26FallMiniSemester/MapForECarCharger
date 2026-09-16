#!/usr/bin/env bash
# Phase 2 launcher: Spark ADS + Flask API + Vue Web big screen.
set -Eeuo pipefail
ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
export PROJECT_ROOT="${PROJECT_ROOT:-$ROOT}"
cd "$PROJECT_ROOT"
INSTALL=0; CHECK=0; REFRESH=0; HDFS=0
for arg in "$@"; do
  case "$arg" in
    --install-deps) INSTALL=1;; --check-only) CHECK=1;;
    --refresh-data) REFRESH=1;; --hdfs) HDFS=1;;
    *) echo "Usage: bash scripts/start_ubuntu.sh [--install-deps] [--check-only] [--refresh-data] [--hdfs]"; exit 2;;
  esac
done
fail() { echo "ERROR: $*" >&2; exit 1; }
[[ $EUID -ne 0 ]] || fail 'Run as your desktop user; sudo is used only for dependency installation.'
if [[ -z ${SPARK_VENV:-} ]]; then
  for candidate in "$HOME/apps/map-for-ecar-spark42" "$HOME/.local/share/map-for-ecar-venv"; do
    [[ -x "$candidate/bin/python" ]] || continue
    SPARK_VENV=$candidate
    break
  done
fi
export SPARK_VENV="${SPARK_VENV:-$HOME/apps/map-for-ecar-spark42}"
if ((INSTALL)); then
  sudo apt-get update
  sudo apt-get install -y python3-venv openjdk-17-jdk curl xdg-utils
  [[ -x "$SPARK_VENV/bin/python" ]] || python3 -m venv "$SPARK_VENV"
  "$SPARK_VENV/bin/python" -m pip install 'pyspark==4.2.0' PyYAML -r spark-warehouse/flask-api/requirements.txt
fi
[[ -x "$SPARK_VENV/bin/python" ]] || fail "Missing Python environment. Retry with --install-deps."
source spark-warehouse/scripts/env_ubuntu.sh
[[ -x "$JAVA_HOME/bin/java" ]] || fail 'Java missing. Set JAVA_HOME or retry with --install-deps.'
for cmd in node npm curl java; do
  command -v "$cmd" >/dev/null || fail "Missing $cmd. See scripts/START_UBUNTU.md."
done
node -e 'const [a,b]=process.versions.node.split(".").map(Number); if(!((a===20&&b>=19)||(a===22&&b>=12)||a>=24))process.exit(1)' || fail 'Node requires 20.19+, 22.12+ or 24+. See launcher manual.'
python -c 'import pyspark,flask,yaml; print("Python dependencies OK; Spark",pyspark.__version__)' || fail 'Python dependencies missing. Retry with --install-deps.'
"$SPARK_HOME/bin/spark-submit" --version >/dev/null 2>&1 || fail 'Spark cannot start with this Java runtime. Set a compatible JAVA_HOME.'
java -version
node --version
npm --version
if [[ -x "$HADOOP_HOME/bin/hadoop" ]]; then
  "$HADOOP_HOME/bin/hadoop" version | head -n 3
else
  echo 'Hadoop missing: local Spark mode works; --hdfs requires configured Hadoop.'
  ((HDFS==0)) || fail 'Install/configure Hadoop before --hdfs; no automatic NameNode formatting.'
fi
((CHECK==0)) || { echo 'Dependency check passed. No services or windows started.'; exit 0; }
RUN="$PROJECT_ROOT/runtime/desktop-launch"
mkdir -p "$RUN"
exec 9>"$RUN/launcher.lock"
flock -n 9 || fail 'Another launcher is already running.'
# Stop only processes recorded by this launcher, after checking their executable command.
stop_owned() {
  local name=$1 pattern=$2 pid
  [[ -f "$RUN/$name.pid" ]] || return 0
  read -r pid < "$RUN/$name.pid"
  [[ $pid =~ ^[0-9]+$ ]] || return 0
  if [[ -r /proc/$pid/cmdline ]] && tr '\0' ' ' < "/proc/$pid/cmdline" | grep -Fq "$pattern"; then
    kill "$pid" 2>/dev/null || true
    for _ in {1..20}; do kill -0 "$pid" 2>/dev/null || break; sleep .2; done
  fi
}
# Clean up clients/backends from an older full-stack launcher run; phase 2 does not restart them.
stop_owned user "$PROJECT_ROOT/clients/user-qt/charging-user-client"
stop_owned admin "$PROJECT_ROOT/clients/admin-qt/ChargingAdmin"
stop_owned server "$PROJECT_ROOT/server-qt/build/charger-server"
stop_owned vue "$PROJECT_ROOT/web-bigscreen/node_modules/vite/bin/vite.js"
stop_owned flask "$PROJECT_ROOT/spark-warehouse/flask-api/app.py"
python - <<'PY'
import socket
for port in (5000,5174):
    with socket.socket() as s:
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        try: s.bind(('127.0.0.1',port))
        except OSError: raise SystemExit(f'Port {port} occupied by an existing service. Stop it in its original terminal and rerun. No unrelated process was killed.')
PY
(cd web-bigscreen; npm ci; npm run build)
FLOW="$PROJECT_ROOT/spark-warehouse/runtime/latest-flow.env"
[[ ! -f "$FLOW" ]] || source "$FLOW"
TODAY=$(TZ=Asia/Shanghai date +%F)
if ((REFRESH)) || [[ -z ${WAREHOUSE:-} || -z ${BATCH:-} || $BATCH != sim-full-$TODAY-seed* ]] || [[ ! -d "$WAREHOUSE/ads/ads_topics/batch_id=$BATCH/json" ]]; then
  GENERATED="$PROJECT_ROOT/spark-warehouse/runtime/generated"
  mkdir -p "$GENERATED"
  SNAPSHOT=$(find "$GENERATED" -mindepth 1 -maxdepth 1 -type d -name "sim-full-$TODAY-seed*" 2>/dev/null | sort | tail -n 1)
  if [[ -z "$SNAPSHOT" ]]; then
    SNAPSHOT=$(python spark-warehouse/generator/generate_all.py --profile full --output "$GENERATED")
  fi
  BATCH=$(basename "$SNAPSHOT")
  WAREHOUSE="$PROJECT_ROOT/spark-warehouse/runtime/generated-warehouse/$BATCH"
  if ((REFRESH)) && [[ -d "$WAREHOUSE" ]]; then
    case "$WAREHOUSE" in
      "$PROJECT_ROOT"/spark-warehouse/runtime/generated-warehouse/*) rm -rf "$WAREHOUSE";;
      *) fail "Refusing to refresh outside generated warehouse: $WAREHOUSE";;
    esac
  fi
  python spark-warehouse/jobs/run_pipeline.py --master 'local[2]' --spark-submit "$SPARK_HOME/bin/spark-submit" --input "$SNAPSHOT" --warehouse "$WAREHOUSE" --batch-id "$BATCH" > "$RUN/pipeline.log" 2>&1 || fail "Spark failed: see $RUN/pipeline.log"
  printf 'export SNAPSHOT=%q\nexport BATCH=%q\nexport WAREHOUSE=%q\n' "$SNAPSHOT" "$BATCH" "$WAREHOUSE" > "$FLOW"
fi
if ((HDFS)); then
  command -v hdfs >/dev/null || fail 'hdfs command unavailable.'
  hdfs dfs -ls / >/dev/null 2>&1 || "$HADOOP_HOME/sbin/start-dfs.sh"
  hdfs dfsadmin -safemode wait
  [[ -d ${SNAPSHOT:-}/dirty ]] || fail 'Snapshot missing; retry --refresh-data --hdfs.'
  hdfs dfs -mkdir -p "/map-for-ecar/input/$BATCH"
  hdfs dfs -put -f "$SNAPSHOT/dirty" "$SNAPSHOT/metadata.json" "/map-for-ecar/input/$BATCH/"
fi
export ML_PREDICTIONS_PATH="${LAUNCH_ML_PATH:-$PROJECT_ROOT/ml/outputs/dashboard-demo-launch/predictions.json}"
if [[ ! -f "$ML_PREDICTIONS_PATH" ]]; then
  [[ -z ${LAUNCH_ML_PATH:-} ]] || fail 'LAUNCH_ML_PATH does not exist.'
  python ml/src/workflow.py demo ml/outputs/dashboard-demo-launch > "$RUN/prediction.log" 2>&1 || fail "Demo prediction failed: $RUN/prediction.log"
fi
export ADS_ROOT="$WAREHOUSE/ads" ADS_BATCH_ID="$BATCH" ADS_CACHE_SECONDS=5
export FLASK_HOST=127.0.0.1 FLASK_PORT=5000
export DASHBOARD_REFRESH_MS=300000 DASHBOARD_INITIAL_DELAY_MS=300000
export DASHBOARD_TARGET=http://127.0.0.1:5000 VITE_USE_MOCK=false VITE_USE_ANALYTICS=true VITE_USE_SPARK_COMPARISONS=true
start() { local name=$1; shift; nohup "$@" 9>&- > "$RUN/$name.log" 2>&1 < /dev/null & echo $! > "$RUN/$name.pid"; }
start_detached() {
  local name=$1; shift
  rm -f "$RUN/$name.pid"
  setsid -f bash -c 'pid_file=$1; shift; echo $$ > "$pid_file"; exec "$@"' _ "$RUN/$name.pid" "$@" \
    9>&- > "$RUN/$name.log" 2>&1 < /dev/null
  for _ in {1..20}; do [[ -s "$RUN/$name.pid" ]] && return 0; sleep .1; done
  fail "$name did not publish its detached PID."
}
wait_http() {
  local url=$1 name=$2
  for _ in {1..60}; do
    curl -fsS "$url" >/dev/null 2>&1 && return 0
    kill -0 "$(cat "$RUN/$name.pid")" 2>/dev/null || fail "$name exited; see $RUN/$name.log"
    sleep 1
  done
  fail "$name timed out; see $RUN/$name.log"
}
start flask env -u DATABASE_PATH python "$PROJECT_ROOT/spark-warehouse/flask-api/app.py"
wait_http http://127.0.0.1:5000/health flask
start_detached vue node "$PROJECT_ROOT/web-bigscreen/node_modules/vite/bin/vite.js" "$PROJECT_ROOT/web-bigscreen" --host 127.0.0.1 --port 5174 --strictPort
wait_http http://127.0.0.1:5174/api/v1/topics vue
if [[ -n ${DISPLAY:-}${WAYLAND_DISPLAY:-} ]] && command -v firefox >/dev/null; then
  nohup firefox --new-window http://127.0.0.1:5174/ 9>&- > "$RUN/browser.log" 2>&1 < /dev/null &
elif [[ -n ${DISPLAY:-}${WAYLAND_DISPLAY:-} ]] && command -v xdg-open >/dev/null; then
  xdg-open http://127.0.0.1:5174/ 9>&- > "$RUN/browser.log" 2>&1 || fail "Browser failed: $RUN/browser.log"
else
  echo 'No GUI session detected; Web is ready at http://127.0.0.1:5174/'
fi
echo "Ready: Flask Spark API http://127.0.0.1:5000/ + Vue Web http://127.0.0.1:5174/"
echo "Web Spark ADS batch: $BATCH; prediction: $ML_PREDICTIONS_PATH"
echo "Logs: $RUN. Predictions generated by demo are simulation-only."
