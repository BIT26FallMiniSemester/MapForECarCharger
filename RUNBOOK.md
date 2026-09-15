# Ubuntu 完整测试操作手册

更新时间：2026-09-15。适用于迁移到 D 盘后的 Ubuntu 虚拟机，代码包含提交 `5eb7ba9` 的大屏修复。

虚拟机在 Windows 上的位置不影响 Ubuntu 内部项目路径。以下命令全部在 **Ubuntu 的 Bash 终端** 执行；每个新终端先执行第 2 节。不要复制 Windows 的 `PS>` 提示符。Bash 多行命令使用反斜杠 `\`，最后一行不加反斜杠。

## 1. 先检查虚拟机和代码

```bash
hostname -I
ip -br address
df -h /
cd "$HOME/MapForECarCharger"
if git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
  git status --short
  git log -3 --oneline
else
  echo '当前为文件复制部署，没有 .git；跳过 Git 检查，继续第 2/3 节。'
  test ! -f .deployed-main || cat .deployed-main
fi
```

迁移后根分区已扩展到约 30GB，上次检查约有 12GB 可用空间。原 IP 为 `192.168.88.128`，以当前 `hostname -I` 为准。输出为空时先恢复 VMware NAT 和 DHCP，暂不进行联网更新。

当前 zjs 虚拟机是文件复制部署，没有 `.git`，跳过 Git 更新即可继续测试。`.deployed-main` 是旧部署标记，不代表后续上传修复的完整版本。不要在该目录直接 git init 或覆盖克隆。以下更新命令仅适用于真正的 Git 克隆目录且没有本地修改时：

```bash
cd "$HOME/MapForECarCharger"
if git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
  git fetch origin feature/web-bigscreen
  git switch feature/web-bigscreen && git pull --ff-only origin feature/web-bigscreen
  git log -3 --oneline
else
  echo '文件部署无需执行 git fetch/switch/pull；保留当前目录，直接继续构建。'
fi
```

若 Git 提示本地修改冲突，先保留修改并检查差异，不要使用 `reset --hard`。之前通过上传方式部署的修复可能显示为未提交修改。

## 2. 每个终端：设置运行环境

迁移后的现有安装路径如下；在新终端复制整个代码块：

```bash
export PROJECT_ROOT="$HOME/MapForECarCharger"
export JAVA_HOME=/usr/lib/jvm/java-17-openjdk-amd64
export HADOOP_HOME="$HOME/apps/hadoop-3.2.1"
export SPARK_HOME=/home/zjs/apps/map-for-ecar-spark42/lib/python3.12/site-packages/pyspark
unset PYTHONPATH
export PATH="/home/zjs/apps/map-for-ecar-spark42/bin:$HOME/.local/bin:$HOME/.local/node24/node_modules/.bin:$JAVA_HOME/bin:$HADOOP_HOME/bin:$HADOOP_HOME/sbin:$SPARK_HOME/bin:$PATH"
export DATABASE_PATH="$PROJECT_ROOT/server-qt/runtime/showcase-sim.db"
cd "$PROJECT_ROOT"
```

依赖检查：

```bash
java -version
hadoop version
spark-submit --version
python3 -c 'import pyspark, flask, yaml; print(pyspark.__version__)'
node --version
npm --version
command -v qmake6
sqlite3 --version
```

当前环境为 Java 17、Hadoop 3.2.1、PySpark 4.2.0、Node 24。Vite 8 需要 Node **20.19+ 或 22.12+**，不要使用原来的系统 Node 18。`SPARK_HOME` 指向新版虚拟环境内的 pyspark 包目录，不再使用旧源码 deps。

现有环境无需重复安装。若 Python 包缺失：

```bash
python3 -m pip install PyYAML==6.0.2 pytest==8.3.5
python3 -m pip install -r spark-warehouse/flask-api/requirements.txt
```

PySpark 已通过上述独立虚拟环境使用，不必再次下载大体积安装包。

## 3. 构建和数据库检查

```bash
cd "$PROJECT_ROOT"
cmake -S server-qt -B server-qt/build -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Release
cmake --build server-qt/build -j2
ctest --test-dir server-qt/build --output-on-failure

cd "$PROJECT_ROOT/clients/user-qt"
qmake6 charging-user-client.pro
make -j2

cd "$PROJECT_ROOT/clients/admin-qt"
qmake6 ChargingAdmin.pro
make -j2

cd "$PROJECT_ROOT/web-bigscreen"
npm ci
npm run build
```

使用 `-j2` 降低虚拟机内存压力。检查模拟库：

```bash
cd "$PROJECT_ROOT"
test -f "$DATABASE_PATH" && ls -lh "$DATABASE_PATH"
sqlite3 "$DATABASE_PATH" 'PRAGMA integrity_check;'
sqlite3 "$DATABASE_PATH" 'SELECT count(*) FROM charging_orders; SELECT count(*) FROM users;'
```

预期完整性为 `ok`；初始模拟库有 50,000 笔订单、1,002 个用户。完成新订单后数量会增加。所有端都使用此数据库，避免混用 `showcase.db` 或 `/var/lib/map-for-ecar/charger.db`。

## 4. 终端 A：启动 Qt 后端

保留仓库外 `/etc/map-for-ecar/server.env` 中的地图 Key。可用 `sudoedit /etc/map-for-ecar/server.env` 修改数据库行为：

```dotenv
DATABASE_PATH=/home/zjs/MapForECarCharger/server-qt/runtime/showcase-sim.db
```

路径后不要紧接注释。启动前确认没有旧服务占用端口：

```bash
ss -ltnp | grep -E ':9000|:9001|:5000|:5174'
```

已有服务时在原终端按 Ctrl+C；使用 systemd 启动的服务应通过对应服务单元停止，不要重复启动。

```bash
cd "$PROJECT_ROOT"
set -a
[ ! -f /etc/map-for-ecar/server.env ] || source /etc/map-for-ecar/server.env
set +a
export DATABASE_PATH="$PROJECT_ROOT/server-qt/runtime/showcase-sim.db"
export DASHBOARD_REFRESH_MS=300000
export DASHBOARD_INITIAL_DELAY_MS=300000
./server-qt/build/charger-server \
  --database "$DATABASE_PATH" \
  --host 127.0.0.1 \
  --port 9000 \
  --dashboard-port 9001
```

保持终端运行。此处将 Qt 内嵌大屏汇总推迟 5 分钟，减少大模拟库启动时的阻塞；Vue 通过 Flask 读取实时数据，不依赖 9001 的汇总刷新。

如果报 `Startup failed, code=50000`，先检查数据库路径、完整性和权限，查看终端日志，再检查迁移，不要删除数据库：

```bash
ls -l "$DATABASE_PATH"
test -r "$DATABASE_PATH" && test -w "$DATABASE_PATH" && echo 'database readable/writable'
./server-qt/build/charger-server --help
```

## 5. 终端 B/C：打开用户端和管理端

必须在 Ubuntu 桌面终端中运行，不能直接在没有图形会话的 SSH 中打开 Qt 窗口。

终端 B：

```bash
cd "$PROJECT_ROOT/clients/user-qt"
./charging-user-client
```

演示用户手机号：`13900000000`。

终端 C：

```bash
cd "$PROJECT_ROOT/clients/admin-qt"
./ChargingAdmin
```

管理账号：`admin / 123456`。连接同机 Qt 后端 `127.0.0.1:9000`。

先确认两个端均能登录、查询站点和订单。在用户端依次充值、预约空闲电桩、开始充电、结束充电、支付。记下订单号，在管理端找到同一笔订单。

```bash
sqlite3 -header -column "$DATABASE_PATH" \
  'SELECT id,order_no,status,energy_wh,amount_cents,paid_at FROM charging_orders ORDER BY id DESC LIMIT 5;'
```

## 6. 终端 D：导出快照并运行 Spark 数仓

```bash
cd "$PROJECT_ROOT"
python3 -m pytest -q spark-warehouse/tests
mkdir -p spark-warehouse/runtime
export SNAPSHOT="$(python3 spark-warehouse/generator/export_sqlite_snapshot.py \
  --database "$DATABASE_PATH" \
  --output "$PROJECT_ROOT/spark-warehouse/runtime/business-snapshots")"
export BATCH="$(basename "$SNAPSHOT")"
export WAREHOUSE="$PROJECT_ROOT/spark-warehouse/runtime/business-warehouse/$BATCH"
cat "$SNAPSHOT/metadata.json"

cd "$PROJECT_ROOT/spark-warehouse"
set -o pipefail
python3 jobs/run_pipeline.py \
  --master 'local[2]' \
  --spark-submit "$SPARK_HOME/bin/spark-submit" \
  --input "$SNAPSHOT" \
  --warehouse "$WAREHOUSE" \
  --batch-id "$BATCH" \
  2>&1 | tee "runtime/pipeline-$BATCH.log"
export PIPELINE_EXIT=$?
echo "PIPELINE_EXIT=$PIPELINE_EXIT"
```

只有 `PIPELINE_EXIT=0` 才继续。失败时查看日志末尾，暂不切换 Flask 批次。成功后保存跨终端配置：

```bash
if [ "$PIPELINE_EXIT" -eq 0 ]; then
  printf 'export SNAPSHOT=%q\nexport BATCH=%q\nexport WAREHOUSE=%q\n' \
    "$SNAPSHOT" "$BATCH" "$WAREHOUSE" > "$PROJECT_ROOT/spark-warehouse/runtime/latest-flow.env"
fi
find "$WAREHOUSE" -name _SUCCESS | sort
cat "$WAREHOUSE/ads/ads_overview/batch_id=$BATCH/json"/part-*.json
cat "$WAREHOUSE/ads/ads_quality_overview/batch_id=$BATCH/json"/part-*.json
```

流程为 SQLite 只读导出六张表 → ODS → 质量检测 → DWD 清洗 → DWS 聚合 → ADS 指标。业务库不会被 Spark 清洗修改。

此前本地文件系统模式已通过，初始批次有 49,945 笔有效订单、735 个质量问题、55 条隔离记录。新业务和代码更新后数字可能变化，数据质量问题的存在不等于流程失败。

## 7. HDFS 存储验证

```bash
jps
```

若 NameNode、DataNode 未运行，执行：

```bash
start-dfs.sh
```

检查 HDFS，等待安全模式自动退出：

```bash
jps
hdfs dfsadmin -safemode get
hdfs dfsadmin -report
```

不要重新执行 NameNode format。已有数据时，若长期处于安全模式，先检查 DataNode 和磁盘，不要直接强制退出。

```bash
source "$PROJECT_ROOT/spark-warehouse/runtime/latest-flow.env"
export HDFS_INPUT="/map-for-ecar/input/$BATCH"
hdfs dfs -mkdir -p "$HDFS_INPUT"
hdfs dfs -put -f "$SNAPSHOT/dirty" "$HDFS_INPUT/"
hdfs dfs -put -f "$SNAPSHOT/metadata.json" "$HDFS_INPUT/"
hdfs dfs -ls -R "$HDFS_INPUT"
hdfs dfs -cat "$HDFS_INPUT/metadata.json"
```

应看到六张 CSV 和 metadata。当前完整测试使用本地 Spark 数仓加 HDFS 快照存储；Spark 全部分层直接在 HDFS 中读写仍需单独验证，不能将本节上传成功当成 HDFS 数仓计算成功。

## 8. 预测：先验证演示，再测试真实业务训练

快速生成项目自带模拟预测：

```bash
cd "$PROJECT_ROOT"
python3 ml/src/workflow.py demo ml/outputs/dashboard-demo-test
export ML_PREDICTIONS_PATH="$PROJECT_ROOT/ml/outputs/dashboard-demo-test/predictions.json"
python3 -m json.tool "$ML_PREDICTIONS_PATH" | head -40
```

这是模拟站点的预测展示测试，演示日期由生成器决定，不能用于证明当前真实站点预测准确。

真实业务训练可单独测试（大库耗时并产生较多中间文件）：

```bash
cd "$PROJECT_ROOT"
python3 ml/src/qt_pipeline.py \
  --database "$DATABASE_PATH" \
  --start 2026-06-17T00:00:00Z \
  --work-dir ml/outputs/qt-showcase-test \
  --output ml/outputs/forecast-live-test.json \
  --simulated
```

此起始日期对应当前历史模拟库。此前这份库训练被质量门禁拦截，曾出现 `Rejected orders/events`，因此真实库预测尚未通过完整验收。若再次出现该错误，查看工作目录的 `history.quality.json`；保留演示预测，不要绕过门禁发布结果。`--simulated` 明确标记输入为模拟库。

## 9. 终端 E：启动 Flask

执行第 2 节，然后：

```bash
cd "$PROJECT_ROOT"
source spark-warehouse/runtime/latest-flow.env
export ADS_ROOT="$WAREHOUSE/ads"
export ADS_BATCH_ID="$BATCH"
export ADS_CACHE_SECONDS=5
export DATABASE_PATH="$PROJECT_ROOT/server-qt/runtime/showcase-sim.db"
export ML_PREDICTIONS_PATH="$PROJECT_ROOT/ml/outputs/dashboard-demo-test/predictions.json"
export FLASK_HOST=0.0.0.0
export FLASK_PORT=5000
python3 spark-warehouse/flask-api/app.py
```

实时订单、今日数据、电桩状态来自只读 SQLite；历史排行、分析和质量报告来自 ADS；预测来自指定 JSON。`/api/v1/overview` 是批次指标，比较管理端今日实时指标应使用 `/api/dashboard`。

## 10. 终端 F：启动 Vue 大屏

```bash
cd "$PROJECT_ROOT/web-bigscreen"
export DASHBOARD_TARGET=http://127.0.0.1:5000
export VITE_USE_MOCK=false
export VITE_USE_ANALYTICS=true
export VITE_USE_SPARK_COMPARISONS=true
npm run dev -- --host 0.0.0.0 --port 5174 --strictPort
```

Ubuntu 浏览器打开 `http://127.0.0.1:5174/`。Windows 主机打开 `http://192.168.88.128:5174/`，IP 改变时替换为当前地址。端口占用时关闭旧 Vue，不要改端口掩盖重复服务。

## 11. 终端 G：接口和数据闭环验收

```bash
curl -fsS http://127.0.0.1:5000/health
curl -fsS http://127.0.0.1:5000/api/dashboard | python3 -m json.tool
curl -fsS http://127.0.0.1:5000/api/analytics | python3 -m json.tool
curl -fsS http://127.0.0.1:5174/api/dashboard | python3 -m json.tool
```

执行自动对账（比较过程中暂停创建或支付新订单）：

```bash
python3 - <<'PY'
import json, os, sqlite3, urllib.request
from datetime import datetime, timedelta, timezone
from pathlib import Path
from zoneinfo import ZoneInfo
with urllib.request.urlopen('http://127.0.0.1:5174/api/dashboard', timeout=30) as r:
    data = json.load(r)
now = datetime.now(ZoneInfo('Asia/Shanghai'))
start = now.replace(hour=0, minute=0, second=0, microsecond=0).astimezone(timezone.utc)
bounds = tuple(t.isoformat(timespec='milliseconds').replace('+00:00', 'Z') for t in (start, start + timedelta(days=1)))
with sqlite3.connect(Path(os.environ['DATABASE_PATH']).resolve().as_uri() + '?mode=ro', uri=True) as db:
    orders = db.execute('SELECT count(*) FROM charging_orders WHERE created_at>=? AND created_at<?', bounds).fetchone()[0]
    revenue = db.execute("SELECT coalesce(sum(amount_cents),0) FROM charging_orders WHERE status='COMPLETED' AND paid_at>=? AND paid_at<?", bounds).fetchone()[0]
    energy = db.execute("SELECT coalesce(sum(energy_wh),0) FROM charging_orders WHERE status IN ('UNPAID','COMPLETED') AND stopped_at>=? AND stopped_at<?", bounds).fetchone()[0]
assert data['today_orders'] == orders, (data['today_orders'], orders)
assert data['today_revenue_cents'] == revenue, (data['today_revenue_cents'], revenue)
assert data['trend'][-1]['energy_wh'] == energy
assert data['realtime_orders'], '实时订单为空'
assert data['load_prediction']['points'], '预测为空，请检查预测路径'
print('PASS 今日订单/营收/电量与SQLite一致；实时订单和预测均非空')
print('今日订单:', orders, '营收(分):', revenue, '电量(Wh):', energy)
print('最新订单:', data['realtime_orders'][0]['order_no'])
PY
```

今日统计按北京时间：订单数按创建时间；营收按已完成订单的支付时间；电量按结束时间统计 UNPAID/COMPLETED。界面会将分换算为元、Wh 换算为 kWh，不要直接比较显示值与数据库整数。

闭环测试按以下顺序：

1. 记录管理端和大屏今日订单、营收和电量。
2. 用户端完成并支付一笔新订单，记录订单号。
3. 刷新管理端和大屏，确认订单号出现，再运行上述对账。
4. 重跑第 6 节生成新 ADS 批次；历史排行和分析需要该批次计算，不是实时刷新。
5. 在终端 E 按 Ctrl+C，重跑第 9 节加载新批次；刷新 Vue，检查排行、趋势、批次信息。
6. 重跑第 7 节上传新快照；预测不会随订单自动重训，需单独执行第 8 节。

## 12. 停止与问题定位

关闭 Qt 客户端窗口，在 Qt 后端、Flask、Vue 各终端按 Ctrl+C。需要停止 HDFS 时：

```bash
stop-dfs.sh
ss -ltnp | grep -E ':9000|:9001|:5000|:5174'
df -h /
```

| 现象 | 首先检查 |
|---|---|
| Ubuntu 无 IP | Windows 的 VMware NAT Service、VMnetDHCP 以及虚拟机网卡连接状态 |
| Node/Vite 报错 | `node --version`、`command -v node`，确认用户安装的 Node 20 优先 |
| 用户端/管理端连接失败 | 9000 监听、Qt 后端日志、客户端连接地址 |
| 实时订单为空或今日不一致 | Flask 的 DATABASE_PATH 是否与 Qt 完全相同、代码是否含 5eb7ba9 |
| 预测为空 | ML_PREDICTIONS_PATH 文件存在且含 predictions，重启 Flask |
| ADS 空或读取失败 | 流程退出码、_SUCCESS、ADS_ROOT、ADS_BATCH_ID |
| 磁盘或内存不足 | `df -h /`、`free -h`，避免重复运行大库训练/数仓任务 |

原模拟数据日期不会随每天开机自动前移；今日指标可能为零。请通过用户端新增当日业务来验证实时回路。重复生成大量批次会占用磁盘，先确认需要保留哪些批次，再清理指定旧目录。

## 七页大屏验收（2026-09-15）

先按本手册启动 Qt 后端、Flask 和 Vue；Qt 与 Flask 必须使用同一个 DATABASE_PATH。专题页需要真实 API 模式（VITE_USE_MOCK=false）。前端每次请求完成后间隔 5 秒刷新，专题汇总缓存 5 秒；Spark 历史数据在重新运行数仓后更新。

Ubuntu 终端直接复制：

```bash
cd /home/zjs/MapForECarCharger
source spark-warehouse/scripts/env_ubuntu.sh
curl -fsS http://127.0.0.1:5174/api/v1/topics | python -m json.tool
curl -fsS 'http://127.0.0.1:5174/api/v1/piles?page=2&page_size=120' | python -m json.tool
curl -fsS 'http://127.0.0.1:5174/api/v1/piles?page=1&page_size=120&status=OFFLINE' | python -m json.tool
```

预期：第一条返回 orders、users、stations、energy、system 等字段；第二条 page=2、最多 120 条 items，电桩 ID 与第一页不同；第三条仅包含 OFFLINE 电桩，当前全部已接入的模拟库应为 total=0、items=[]。请求失败时检查 Flask 日志、DATABASE_PATH 和 5000 端口。

在 Ubuntu 浏览器打开 http://127.0.0.1:5174/；主机浏览器使用 http://192.168.88.128:5174/（IP 变化时替换）。按顶部导航逐页检查：

| 地址后缀 | 页面 | 预期结果 |
|---|---|---|
| /#/overview | 综合态势 | 今日指标、实时订单、历史趋势和预测；站点用编号展示 |
| /#/stations | 站点运营 | 区域资源、站点地图，可切换繁忙率/空闲率/故障率；站点编号排行 |
| /#/piles | 电桩监控 | 每页 120 个状态色块，点击下一页应出现不同编号；筛选状态后回到第一页，离线为空显示空状态 |
| /#/orders | 订单运营 | 当日订单流程漏斗、状态分布、小时分布和最新订单 |
| /#/users | 用户分析 | 用户增长、活跃用户、充值与消费；榜单只显示用户编号 |
| /#/energy | 能源营收 | 今日/本月电量和营收、清洗后历史趋势、站点营收及预测 |
| /#/system | 系统监控 | 数据库规模、表行数、ADS 批次与质量、管理操作日志 |

每页刷新浏览器应保留当前页面。地图不可用时查看地图提示和腾讯 Key 配置。Socket 连接数、请求数、地图健康状态等尚未采集的监测项显示“未采集”；管理操作日志不是 Socket 请求日志。电桩状态来自业务数据库，模拟已接入不代表真实设备心跳。

再从用户端创建、启动、结束并支付一笔订单：下一次刷新后实时订单和业务指标应变化；Spark 历史统计需重新运行数仓流程，机器学习预测需重新训练生成，不能期待 5 秒自动计算。静态模拟数据没有当天订单时今日指标允许为零。
