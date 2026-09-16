# 第二阶段 Web 大屏 + ML 一键启动

在 Ubuntu 终端执行，不要使用 sudo bash。脚本只启动 Spark 数据链路、Flask API 和 Vue Web 大屏，不启动 Qt 用户端、管理端或 Qt 后端；无图形会话也可以启动，之后手动打开浏览器访问地址即可。

```bash
cd ~/MapForECarCharger
bash scripts/start_ubuntu.sh
```

首次检查并安装 Web/Spark 依赖；按北京时间当天生成 full 数据集并运行 Spark ODS→DWD→DWS→ADS；预测缺失时生成 ML 演示预测；启动 Flask API 和 Vue Web 大屏。Web 统计接口只读取 Spark ADS，不读取 SQLite。

仅检查依赖：

```bash
cd ~/MapForECarCharger
bash scripts/start_ubuntu.sh --check-only
```

缺少 Java/Python 依赖时自动安装（需要 sudo 和网络；PySpark 下载约 450 MB）：

```bash
bash scripts/start_ubuntu.sh --install-deps
```

脚本会识别项目标准目录及当前虚拟机已有的 Python 环境、用户目录 JDK 和 Node 24；也可用 `SPARK_VENV`、`JAVA_HOME`、`NODE_HOME` 显式覆盖。新 Ubuntu 可使用官方 Node 24 压缩包安装到用户目录，或安装发行渠道提供的 Node 20.19+、22.12+、24+ 和 npm。Hadoop 必须预先配置，脚本不自动下载 Hadoop、不格式化 NameNode。

更新业务历史统计，额外将快照保存到已有 HDFS：

```bash
bash scripts/start_ubuntu.sh --refresh-data --hdfs
```

默认 Spark local[2] 使用本地数仓；--hdfs 增加 HDFS 快照存储，不表示所有 Spark 分层均在 HDFS 计算。普通重复启动复用历史批次，刷新历史时使用 --refresh-data。

预期：终端最后输出 Ready，Flask 健康检查和 Web 大屏均可访问 `http://127.0.0.1:5000/`、`http://127.0.0.1:5174/`，顶部能切换七页。运行中日志位于 runtime/desktop-launch/。首次 Spark 数仓计算可能需要数分钟。

```bash
curl -fsS http://127.0.0.1:5000/health
curl -fsS http://127.0.0.1:5174/api/v1/topics | python3 -m json.tool
ss -ltnp | grep -E ':5000|:5174'
```

预期接口返回 JSON，两个端口监听。数据生成和 Spark ADS 计算完成后，刷新 Web 页面即可看到统计和 ML 预测；预测不是实时重算。自动生成的 demo 预测不代表真实业务预测模型通过训练。

脚本重复运行只关闭自己 PID 文件记录且命令匹配的程序；遇到原手工启动服务占用端口会停止并报错，请在原终端 Ctrl+C 后再运行。任何阶段失败会显示日志路径，已启动服务保留用于排查。

指定其他项目、数据库或已训练预测：

```bash
export PROJECT_ROOT="$HOME/MapForECarCharger"
# export LAUNCH_ML_PATH="$PROJECT_ROOT/ml/outputs/forecast-live-test.json"
bash "$PROJECT_ROOT/scripts/start_ubuntu.sh"
```

脚本默认各服务仅监听本机，供 Ubuntu 本地展示。`LAUNCH_ML_PATH` 可指定已有预测结果。

## 统一统计来源

Flask 和 Vue 启动时使用同一个 `latest-flow.env`，设置 `ADS_ROOT=$WAREHOUSE/ads`、`ADS_BATCH_ID=$BATCH`。所有 Web 主题页、设备分页、订单、站点、用户、能源和 ML 展示均来自 Spark ADS/预测文件；本启动脚本不检查、不启动、不连接 SQLite 或 Qt 客户端。

```bash
cd ~/MapForECarCharger
bash scripts/start_ubuntu.sh --refresh-data
curl -fsS http://127.0.0.1:5000/health
curl -fsS http://127.0.0.1:5174/api/v1/topics | python3 -m json.tool
```

普通重复启动复用当天批次；需要重新计算时使用 `--refresh-data`。`--hdfs` 仅额外保存 Spark 输入快照，不改变 Web 的 ADS 数据源。

## 第一阶段 Qt 数据库手动流程（本脚本不执行）

以下内容仅供第一阶段 Qt 客户端调试参考；第二阶段一键启动不会读取这些 SQLite 数据，也不会启动用户端、管理端或 Qt 后端。

保留原模拟库，生成新的副本。先退出用户端/管理端并停止旧服务，再在 Ubuntu 执行：

```bash
cd ~/MapForECarCharger
RICH_DB="$PWD/server-qt/runtime/showcase-rich-$(date +%Y%m%d-%H%M%S).db"
python3 server-qt/tools/enrich_showcase_simulation.py --source server-qt/runtime/showcase-sim.db --target "$RICH_DB" --seed 20260915
export LAUNCH_DATABASE_PATH="$RICH_DB"
bash scripts/start_ubuntu.sh --refresh-data
```

目标路径必须不存在，生成器不会覆盖原库。原约 5 万历史订单保留，再增加 650 笔多状态订单、100 名冻结用户、200 笔不同金额充值；电桩状态覆盖 IDLE/RESERVED/CHARGING/FAULT/OFFLINE。数量取决于原库可用用户和电桩，现有库实测充电中 194、预约 128、故障 3180、离线 1060。被冻结的用户没有活跃订单；预约/充电电桩绑定对应订单且一个用户最多一笔活跃订单。保留演示手机号 13900000000 为正常用户方便人工操作。

预约生成后 15 分钟自动到期，不应改业务超时规则。`--refresh-data` 会用北京时间当天的 full 模拟数据重新计算 Spark 全链路；生成器日期不再写死。机器学习仍需另行训练，演示预测不会自动成为新数据训练结果。离线明确表示模拟断连，故障表示已接入但不可服务。

```bash
python3 - <<'PY'
import os,sqlite3
with sqlite3.connect(os.environ['LAUNCH_DATABASE_PATH']) as db:
    for table in ('users','charging_piles','charging_orders'):
        print(table,db.execute(f'SELECT status,count(*) FROM {table} GROUP BY status').fetchall())
    print('foreign_keys',db.execute('PRAGMA foreign_key_check').fetchall())
    print('integrity',db.execute('PRAGMA quick_check').fetchall())
PY
```

预期 users 有 NORMAL/FROZEN，电桩有五种状态，订单有六种状态；foreign_keys=[]、integrity=[('ok',)]。大屏状态饼图、矩阵、订单漏斗、用户冻结分布和充值图应更丰富。新库是业务展示数据，不另外注入非法外键或时间字段；原历史库已有质量问题仍保留给 Spark 检测清洗。

## 2026-09-16：六张清洗 CSV 数据集验收

新库使用 15000 用户、2614 站点、32256 电桩、400000 订单、75000 充值记录和 650000 状态日志。原 CSV 的 row_id 是数仓标识，不写入业务表。保留原管理员账号，原业务库和 CSV 均不覆盖。导入器恢复 3807 个充电中电桩的订单绑定，使 Qt 能处理停止充电。

```bash
cd /home/zjs/MapForECarCharger
# 从目录导入时使用新目标文件名（六张 CSV 须已复制到此目录）：
# python3 server-qt/tools/import_clean_csv.py --template server-qt/runtime/showcase-sim.db --csv-dir /home/zjs/clean-csv --target server-qt/runtime/showcase-clean-new.db
export LAUNCH_DATABASE_PATH="$PWD/server-qt/runtime/showcase-clean-20260916-v2.db"
bash scripts/start_ubuntu.sh
```

启动成功后脚本记住这份数据库路径。重新导入或更新历史统计使用 --refresh-data；只需看当前已计算好的结果时不要重复计算。Ubuntu 大屏地址 http://127.0.0.1:5174/，该启动方式默认仅监听本机。

注意数据存在 326 名冻结用户有活跃订单，与 Qt 不允许冻结活跃用户的操作规则不同；为展示冻结分布按原数据保留。原有 3116 笔预约全部已过期，本次展示副本续期到启动前 15 分钟，启动后仍按正常规则到期，不保证重启后继续显示预约。历史数据不会按每天开机自动前移，今日卡片可能为零；通过用户端创建当天业务验证实时变化。预测仍是演示预测，不是新 CSV 训练的结果。

```bash
curl -fsS http://127.0.0.1:5000/api/v1/topics | python3 -m json.tool
curl -fsS 'http://127.0.0.1:5174/api/v1/piles?page=1&page_size=120&status=FAULT' | python3 -m json.tool
curl -fsS http://127.0.0.1:9001/api/analytics | python3 -m json.tool
curl -fsS http://127.0.0.1:5000/api/analytics | python3 -m json.tool
```

预期用户冻结 330、充电中 3807、故障 3226、离线 645；预约初始 3116，15 分钟后可变为 0；分页筛选 total=3226、每页最多 120；两个 analytics 接口批次、每日营收和订单数相同，管理端 7 日趋势是同批次 30 日的末 7 日。大屏地图与榜单只显示站点编号。
