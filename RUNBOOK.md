# MapForECarCharger 完整运行与闭环验收

本文说明如何启动 Qt 业务系统，完成一笔真实充电订单，将 SQLite 快照送入
PySpark 数仓，并通过 Flask 和 Vue/ECharts 展示分析结果。

## 1. 目录和环境变量

以下命令默认仓库位于 Linux 用户主目录。若目录不同，只需修改
`PROJECT_ROOT`：

```bash
export PROJECT_ROOT="$HOME/MapForECarCharger"
cd "$PROJECT_ROOT"
```

默认端口：

| 服务 | 端口 |
|---|---:|
| Qt Socket 后端 | 9000 |
| Qt 内嵌大屏 | 9001 |
| Flask ADS API | 5000 |
| Vue 开发服务器 | 5174 |

## 2. 首次安装依赖

Ubuntu 22.04+：

```bash
sudo apt update
sudo apt install -y \
  build-essential cmake qmake6 \
  qt6-base-dev qt6-base-dev-tools qt6-charts-dev qt6-webengine-dev \
  libqt6sql6-sqlite python3 python3-pip python3-flask sqlite3 curl
```

安装项目 Python 依赖：

```bash
cd "$PROJECT_ROOT"
python3 -m pip install --user -r spark-warehouse/requirements-dev.txt
python3 -m pip install --user -r spark-warehouse/flask-api/requirements.txt
```

项目需要可用的 Java、Hadoop、HDFS、PySpark 和 `spark-submit`。安装路径由使用者
自行配置，但以下命令都应成功：

```bash
java -version
hadoop version
hdfs version
spark-submit --version
python3 -c "import pyspark, flask, yaml; print(pyspark.__version__)"
```

## 3. 首次构建 Qt 程序

构建后端：

```bash
cd "$PROJECT_ROOT"
cmake -S server-qt -B server-qt/build \
  -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Release
cmake --build server-qt/build -j"$(nproc)"
ctest --test-dir server-qt/build --output-on-failure
```

构建用户端：

```bash
cd "$PROJECT_ROOT/clients/user-qt"
qmake6 charging-user-client.pro
make -j"$(nproc)"
```

构建管理端：

```bash
cd "$PROJECT_ROOT/clients/admin-qt"
qmake6 ChargingAdmin.pro
make -j"$(nproc)"
```

## 4. 初始化演示数据库

首次运行时创建一个新的数据库：

```bash
cd "$PROJECT_ROOT"
mkdir -p server-qt/runtime

./server-qt/build/charger-server \
  --database "$PROJECT_ROOT/server-qt/runtime/showcase.db" \
  --migrate-only

./server-qt/build/charger-server \
  --database "$PROJECT_ROOT/server-qt/runtime/showcase.db" \
  --import-catalog "$PROJECT_ROOT/server-qt/data/processed/beijing_public_charging_stations.json"

./server-qt/build/charger-server \
  --database "$PROJECT_ROOT/server-qt/runtime/showcase.db" \
  --seed-showcase --migrate-only
```

`--seed-showcase` 只适用于尚无业务数据的数据库，不要对已使用的数据库重复执行。

## 5. 终端 1：启动 Qt 后端

腾讯地图 Key 应保存在仓库外的环境文件中：

```bash
cd "$PROJECT_ROOT"

set -a
[ -f /etc/map-for-ecar/server.env ] && source /etc/map-for-ecar/server.env
set +a

./server-qt/build/charger-server \
  --database "$PROJECT_ROOT/server-qt/runtime/showcase.db" \
  --host 127.0.0.1 \
  --port 9000 \
  --dashboard-port 9001
```

保持终端运行。另开终端检查：

```bash
ss -ltn | grep -E ':9000|:9001'
```

## 6. 终端 2：打开用户端

必须在 Linux 桌面终端中执行：

```bash
cd "$PROJECT_ROOT/clients/user-qt"
./charging-user-client
```

默认演示用户为 `13900000000`。

## 7. 终端 3：打开管理端

必须在 Linux 桌面终端中执行：

```bash
cd "$PROJECT_ROOT/clients/admin-qt"
./ChargingAdmin
```

默认演示账号为 `admin / 123456`。

## 8. 完成一笔真实订单

在用户端依次执行：

1. 登录并充值。
2. 选择有空闲电桩的站点。
3. 创建订单并预约。
4. 开始充电。
5. 结束充电。
6. 使用余额支付。

随后在管理端检查订单、今日订单数、营收和电桩状态。管理端实时业务数据直接来自
Qt 后端和 SQLite，因此应立即更新。

检查数据库：

```bash
sqlite3 -header -column "$PROJECT_ROOT/server-qt/runtime/showcase.db" \
  "SELECT id,order_no,status,energy_wh,amount_cents,paid_at FROM charging_orders ORDER BY id DESC LIMIT 5;"

sqlite3 -header -column "$PROJECT_ROOT/server-qt/runtime/showcase.db" \
  "SELECT id,user_id,amount_cents,balance_after_cents,created_at FROM recharge_records ORDER BY id DESC LIMIT 5;"

sqlite3 -header -column "$PROJECT_ROOT/server-qt/runtime/showcase.db" \
  "SELECT id,pile_id,order_id,old_status,new_status,reason,created_at FROM pile_status_logs ORDER BY id DESC LIMIT 10;"
```

## 9. 终端 4：导出 SQLite 只读快照

```bash
cd "$PROJECT_ROOT"

SNAPSHOT=$(python3 spark-warehouse/generator/export_sqlite_snapshot.py \
  --database "$PROJECT_ROOT/server-qt/runtime/showcase.db" \
  --output "$PROJECT_ROOT/spark-warehouse/runtime/business-snapshots")

export SNAPSHOT
export BATCH=$(basename "$SNAPSHOT")

echo "SNAPSHOT=$SNAPSHOT"
echo "BATCH=$BATCH"
cat "$SNAPSHOT/metadata.json"
```

快照包含用户、站点、电桩、订单、充值和电桩状态日志六张 CSV。导出器以 SQLite
只读模式打开数据库，并在同一事务中读取所有表。

## 10. 运行 ODS→质量检测→DWD→DWS→ADS

```bash
cd "$PROJECT_ROOT/spark-warehouse"

export WAREHOUSE="$PROJECT_ROOT/spark-warehouse/runtime/business-warehouse/$BATCH"

python3 jobs/run_pipeline.py \
  --master 'local[2]' \
  --spark-submit "$(command -v spark-submit)" \
  --input "$SNAPSHOT" \
  --warehouse "$WAREHOUSE" \
  --batch-id "$BATCH" \
  2>&1 | tee "runtime/pipeline-$BATCH.log"

echo "PIPELINE_EXIT=${PIPESTATUS[0]}"
```

`PIPELINE_EXIT=0` 表示全部分层成功。检查结果：

```bash
find "$WAREHOUSE" -name _SUCCESS | sort
cat "$WAREHOUSE/ads/ads_overview/batch_id=$BATCH/json"/part-*.json
cat "$WAREHOUSE/ads/ads_revenue_trend_30d/batch_id=$BATCH/json"/part-*.json
head -10 "$WAREHOUSE/ads/ads_station_ranking_30d/batch_id=$BATCH/json"/part-*.json
cat "$WAREHOUSE/ads/ads_quality_overview/batch_id=$BATCH/json"/part-*.json
```

实际数据库至少需要一条带业务日期的订单，否则 ADS 无法确定统计截止日期。

## 11. 终端 5：启动 Flask API

跨终端时重新定位最新成功批次：

```bash
cd "$PROJECT_ROOT/spark-warehouse"

export SNAPSHOT=$(find runtime/business-snapshots -mindepth 1 -maxdepth 1 \
  -type d -name 'sqlite-*' -printf '%T@ %p\n' | sort -nr | head -1 | cut -d' ' -f2-)
export BATCH=$(basename "$SNAPSHOT")
export WAREHOUSE="$PROJECT_ROOT/spark-warehouse/runtime/business-warehouse/$BATCH"
export ADS_ROOT="$WAREHOUSE/ads"
export ADS_BATCH_ID="$BATCH"
export ADS_CACHE_SECONDS=5
export FLASK_HOST=0.0.0.0
export FLASK_PORT=5000

python3 flask-api/app.py
```

保持终端运行。

## 12. 终端 6：验证 Flask

```bash
curl -fsS http://127.0.0.1:5000/health
curl -fsS http://127.0.0.1:5000/api/v1/overview
curl -fsS http://127.0.0.1:5000/api/dashboard
curl -fsS http://127.0.0.1:5000/api/analytics
```

## 13. 启动 Vue/ECharts 大屏

Vue 可以与 Flask 在同一台主机运行，也可以从开发电脑连接 Linux 主机。先进入
`web-bigscreen`：

```bash
cd "$PROJECT_ROOT/web-bigscreen"
npm install
```

同机运行：

```bash
export DASHBOARD_TARGET=http://127.0.0.1:5000
export VITE_USE_MOCK=false
export VITE_USE_ANALYTICS=true
npm run dev -- --host 127.0.0.1 --port 5174 --strictPort
```

跨主机运行时，将 `FLASK_HOST` 替换为 Linux 主机地址：

```bash
export DASHBOARD_TARGET=http://FLASK_HOST:5000
export VITE_USE_MOCK=false
export VITE_USE_ANALYTICS=true
npm run dev -- --host 127.0.0.1 --port 5174 --strictPort
```

访问 `http://127.0.0.1:5174/`。

Windows PowerShell 等价命令：

```powershell
cd PATH_TO_REPOSITORY\web-bigscreen
$env:DASHBOARD_TARGET='http://FLASK_HOST:5000'
$env:VITE_USE_MOCK='false'
$env:VITE_USE_ANALYTICS='true'
npm install
npm run dev -- --host 127.0.0.1 --port 5174 --strictPort
```

## 14. 验证完整数据闭环

1. 记录管理端和 Vue 当前订单数、营收、趋势及站点排行。
2. 在用户端完成并支付一笔新订单。
3. 确认管理端实时订单列表立即出现该订单。
4. 重新执行第 9、10 节，生成新的快照和 ADS 批次。
5. 使用新批次重新启动 Flask。
6. 刷新 Vue，确认订单数、营收、趋势、站点排行及批次时间更新。

每个 SQLite 快照和数仓批次都有唯一目录，旧结果不会被覆盖。

## 15. HDFS 基础验证

```bash
start-dfs.sh
jps
hdfs dfsadmin -safemode get
```

首次启动处于安全模式时：

```bash
hdfs dfsadmin -safemode leave
```

上传当前快照：

```bash
hdfs dfs -mkdir -p "/user/$USER/map-for-ecar/business-snapshots/$BATCH"
hdfs dfs -put -f "$SNAPSHOT/dirty" "/user/$USER/map-for-ecar/business-snapshots/$BATCH/"
hdfs dfs -put -f "$SNAPSHOT/metadata.json" "/user/$USER/map-for-ecar/business-snapshots/$BATCH/"
hdfs dfs -ls -R "/user/$USER/map-for-ecar/business-snapshots/$BATCH"
```

当前推荐使用第 10 节的本地文件系统模式完成现场演示。单节点环境下 Spark 全部分层
直接写入 HDFS 的质量检测阶段仍在优化。

## 16. 测试和构建检查

```bash
cd "$PROJECT_ROOT"
python3 -m unittest discover -s ml/tests -p 'test_*.py' -v
python3 -m unittest discover -s analytics-hadoop -p 'test_*.py' -v
python3 -m unittest discover -s spark-warehouse/tests -p 'test_*.py' -v

cd web-bigscreen
npm run build
```

## 17. 停止服务

关闭用户端和管理端窗口。Qt 后端、Flask 和 Vue 所在终端分别按 `Ctrl+C`。

停止 HDFS：

```bash
stop-dfs.sh
jps
```
