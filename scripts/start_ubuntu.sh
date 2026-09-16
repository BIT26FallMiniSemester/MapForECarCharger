#!/usr/bin/env bash
# Run from an Ubuntu desktop terminal, not sudo bash.
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
  sudo apt-get install -y build-essential cmake qt6-base-dev qt6-charts-dev qt6-webengine-dev libqt6sql6-sqlite python3-venv openjdk-17-jdk curl xdg-utils
  [[ -x "$SPARK_VENV/bin/python" ]] || python3 -m venv "$SPARK_VENV"
  "$SPARK_VENV/bin/python" -m pip install 'pyspark==4.2.0' PyYAML -r spark-warehouse/flask-api/requirements.txt
fi
[[ -x "$SPARK_VENV/bin/python" ]] || fail "Missing Python environment. Retry with --install-deps."
source spark-warehouse/scripts/env_ubuntu.sh
[[ -x "$JAVA_HOME/bin/java" ]] || fail 'Java missing. Set JAVA_HOME or retry with --install-deps.'
for cmd in cmake make g++ qmake6 node npm curl java; do
  command -v "$cmd" >/dev/null || fail "Missing $cmd. See scripts/START_UBUNTU.md."
done
command -v firefox >/dev/null || command -v xdg-open >/dev/null || fail 'Missing Firefox or xdg-open.'
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
[[ -n ${DISPLAY:-}${WAYLAND_DISPLAY:-} ]] || fail 'Use an Ubuntu desktop terminal to open Qt and the browser.'
# Load private map configuration without printing credentials.
if [[ -f /etc/map-for-ecar/server.env ]]; then
  [[ -r /etc/map-for-ecar/server.env ]] || fail '/etc/map-for-ecar/server.env is not readable by your user.'
  set -a; source /etc/map-for-ecar/server.env; set +a
fi
export DATABASE_PATH="${LAUNCH_DATABASE_PATH:-$PROJECT_ROOT/server-qt/runtime/showcase-sim.db}"
[[ -f "$DATABASE_PATH" && -r "$DATABASE_PATH" && -w "$DATABASE_PATH" ]] || fail "Database missing or not writable: $DATABASE_PATH. Copy your showcase-sim.db first."
python - <<'PY'
import os,sqlite3,pathlib
p=pathlib.Path(os.environ['DATABASE_PATH']).resolve()
with sqlite3.connect(p.as_uri()+'?mode=ro',uri=True) as db:
    result=db.execute('PRAGMA quick_check').fetchall()
    if result != [('ok',)]: raise SystemExit(f'Database check failed: {result}')
    print('Database OK; orders:',db.execute('SELECT count(*) FROM charging_orders').fetchone()[0])
PY
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
stop_owned user "$PROJECT_ROOT/clients/user-qt/charging-user-client"
stop_owned admin "$PROJECT_ROOT/clients/admin-qt/ChargingAdmin"
stop_owned vue "$PROJECT_ROOT/web-bigscreen/node_modules/vite/bin/vite.js"
stop_owned flask "$PROJECT_ROOT/spark-warehouse/flask-api/app.py"
stop_owned server "$PROJECT_ROOT/server-qt/build/charger-server"
python - <<'PY'
import socket
for port in (9000,9001,5000,5174):
    with socket.socket() as s:
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        try: s.bind(('127.0.0.1',port))
        except OSError: raise SystemExit(f'Port {port} occupied by an existing service. Stop it in its original terminal and rerun. No unrelated process was killed.')
PY
cmake -S server-qt -B server-qt/build -DCMAKE_BUILD_TYPE=Release
cmake --build server-qt/build -j2
(cd clients/user-qt; qmake6 charging-user-client.pro; make -j2)
(cd clients/admin-qt; qmake6 ChargingAdmin.pro; make -j2)
(cd web-bigscreen; npm ci; npm run build)
FLOW="$PROJECT_ROOT/spark-warehouse/runtime/latest-flow.env"
[[ ! -f "$FLOW" ]] || source "$FLOW"
if ((REFRESH)) || [[ -z ${WAREHOUSE:-} || -z ${BATCH:-} ]] || [[ ! -d "$WAREHOUSE/ads/ads_overview/batch_id=$BATCH/json" ]]; then
  SNAPSHOT=$(python spark-warehouse/generator/export_sqlite_snapshot.py --database "$DATABASE_PATH" --output "$PROJECT_ROOT/spark-warehouse/runtime/business-snapshots")
  BATCH=$(basename "$SNAPSHOT")
  WAREHOUSE="$PROJECT_ROOT/spark-warehouse/runtime/business-warehouse/$BATCH"
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
wait_http() {
  local url=$1 name=$2
  for _ in {1..60}; do
    curl -fsS "$url" >/dev/null 2>&1 && return 0
    kill -0 "$(cat "$RUN/$name.pid")" 2>/dev/null || fail "$name exited; see $RUN/$name.log"
    sleep 1
  done
  fail "$name timed out; see $RUN/$name.log"
}
start server "$PROJECT_ROOT/server-qt/build/charger-server" --database "$DATABASE_PATH" --host 127.0.0.1 --port 9000 --dashboard-port 9001
for _ in {1..60}; do
  if python -c 'import socket; socket.create_connection(("127.0.0.1",9000),1).close()' 2>/dev/null; then break; fi
  kill -0 "$(cat "$RUN/server.pid")" 2>/dev/null || fail "Qt server exited: $RUN/server.log"
  sleep 1
done
python -c 'import socket; socket.create_connection(("127.0.0.1",9000),1).close()' || fail 'Qt server startup timeout.'
start flask python "$PROJECT_ROOT/spark-warehouse/flask-api/app.py"
wait_http http://127.0.0.1:5000/health flask
(cd web-bigscreen; start vue node "$PROJECT_ROOT/web-bigscreen/node_modules/vite/bin/vite.js" --host 127.0.0.1 --port 5174 --strictPort)
wait_http http://127.0.0.1:5174/api/v1/topics vue
(cd clients/user-qt; start user "$PROJECT_ROOT/clients/user-qt/charging-user-client")
(cd clients/admin-qt; start admin "$PROJECT_ROOT/clients/admin-qt/ChargingAdmin")
sleep 2
for name in user admin; do kill -0 "$(cat "$RUN/$name.pid")" 2>/dev/null || fail "$name window failed: $RUN/$name.log"; done
if command -v firefox >/dev/null; then
  nohup firefox --new-window http://127.0.0.1:5174/ > "$RUN/browser.log" 2>&1 < /dev/null &
else
  xdg-open http://127.0.0.1:5174/ > "$RUN/browser.log" 2>&1 || fail "Browser failed: $RUN/browser.log"
fi
echo "Ready: one user client, one admin client, Vue http://127.0.0.1:5174/"
echo "DB: $DATABASE_PATH; ADS batch: $BATCH; prediction: $ML_PREDICTIONS_PATH"
echo "Logs: $RUN. Predictions generated by demo are simulation-only."
