# Ubuntu 一键初始化与启动

在 Ubuntu 桌面终端执行，不要使用 sudo bash，也不要从无图形会话的 SSH 启动界面。

```bash
cd ~/MapForECarCharger
bash scripts/start_ubuntu.sh
```

首次检查、构建并安装 Vue npm 依赖；检查现有模拟库；已有 ADS 批次复用，缺失则只读导出 SQLite 并运行 Spark ODS→DWD→DWS→ADS；预测缺失时生成模拟演示预测；启动 Qt 后端、Flask、一个用户窗口、一个管理窗口和浏览器。业务数据库不会清空或重新生成。

仅检查依赖：

```bash
cd ~/MapForECarCharger
bash scripts/start_ubuntu.sh --check-only
```

缺少 Qt/Java/Python 依赖时自动安装（需要 sudo 和网络；PySpark 下载约 450 MB）：

```bash
bash scripts/start_ubuntu.sh --install-deps
```

脚本会识别项目标准目录及当前虚拟机已有的 Python 环境、用户目录 JDK 和 Node 24；也可用 `SPARK_VENV`、`JAVA_HOME`、`NODE_HOME` 显式覆盖。新 Ubuntu 可使用官方 Node 24 压缩包安装到用户目录，或安装发行渠道提供的 Node 20.19+、22.12+、24+ 和 npm。Hadoop 必须预先配置，脚本不自动下载 Hadoop、不格式化 NameNode。

更新业务历史统计，额外将快照保存到已有 HDFS：

```bash
bash scripts/start_ubuntu.sh --refresh-data --hdfs
```

默认 Spark local[2] 使用本地数仓；--hdfs 增加 HDFS 快照存储，不表示所有 Spark 分层均在 HDFS 计算。普通重复启动复用历史批次，刷新历史时使用 --refresh-data。

预期：终端最后输出 Ready，用户端和管理端各一个窗口，浏览器打开 http://127.0.0.1:5174/，顶部能切换七页。管理账号 admin / 123456，演示用户手机号 13900000000。运行中日志位于 runtime/desktop-launch/。首次数仓在本机此前约 2 分钟，加上构建和下载会更久。

```bash
curl -fsS http://127.0.0.1:5000/health
curl -fsS http://127.0.0.1:5174/api/v1/topics | python3 -m json.tool
ss -ltnp | grep -E ':9000|:9001|:5000|:5174'
```

预期接口返回 JSON，四个端口监听。用户端新建订单并完成支付，下一轮大屏刷新应出现业务变化；Spark 历史结果和预测不是实时重算。自动生成的 demo 预测不代表真实业务预测模型通过训练。

脚本重复运行只关闭自己 PID 文件记录且命令匹配的程序；遇到原手工启动服务占用端口会停止并报错，请在原终端 Ctrl+C 后再运行。任何阶段失败会显示日志路径，已启动服务保留用于排查。

指定其他项目、数据库或已训练预测：

```bash
export PROJECT_ROOT="$HOME/MapForECarCharger"
export LAUNCH_DATABASE_PATH="$PROJECT_ROOT/server-qt/runtime/showcase-sim.db"
# 只有文件存在时才启用下行：
# export LAUNCH_ML_PATH="$PROJECT_ROOT/ml/outputs/forecast-live-test.json"
bash "$PROJECT_ROOT/scripts/start_ubuntu.sh"
```

地图配置仍从 /etc/map-for-ecar/server.env 读取，不输出 Key。脚本默认各服务仅监听本机，供 Ubuntu 完整测试。

## 丰富业务场景模拟库

保留原模拟库，生成新的副本。先退出用户端/管理端并停止旧服务，再在 Ubuntu 执行：

```bash
cd ~/MapForECarCharger
RICH_DB="$PWD/server-qt/runtime/showcase-rich-$(date +%Y%m%d-%H%M%S).db"
python3 server-qt/tools/enrich_showcase_simulation.py --source server-qt/runtime/showcase-sim.db --target "$RICH_DB" --seed 20260915
export LAUNCH_DATABASE_PATH="$RICH_DB"
bash scripts/start_ubuntu.sh --refresh-data
```

目标路径必须不存在，生成器不会覆盖原库。原约 5 万历史订单保留，再增加 650 笔多状态订单、100 名冻结用户、200 笔不同金额充值；电桩状态覆盖 IDLE/RESERVED/CHARGING/FAULT/OFFLINE。数量取决于原库可用用户和电桩，现有库实测充电中 194、预约 128、故障 3180、离线 1060。被冻结的用户没有活跃订单；预约/充电电桩绑定对应订单且一个用户最多一笔活跃订单。保留演示手机号 13900000000 为正常用户方便人工操作。

预约生成后 15 分钟自动到期，不应改业务超时规则。建议提前准备依赖与构建，再临近展示时生成新库；重新生成的新库路径需重新设置 LAUNCH_DATABASE_PATH。--refresh-data 重新计算这份库的 Spark 历史结果；机器学习仍需另行训练，演示预测不会自动成为新库训练结果。离线明确表示模拟断连，故障表示已接入但不可服务。

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
