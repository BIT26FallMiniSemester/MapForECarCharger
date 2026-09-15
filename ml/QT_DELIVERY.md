# 大数据统计与机器学习运行说明

基于主分支 `12f8f74` 的 Qt6/SQLite 后端。大数据沿用 `analytics-hadoop/`，Python ML 新增 Qt 数据导出和结果读取链路。

## 一、大数据统计分析

```bash
# 在仓库根目录，Linux 主机已配置 Hadoop/HDFS/YARN 和 Python3。
python3 analytics-hadoop/pipeline.py \
  --database server-qt/runtime/charger.db \
  --mode hadoop \
  --streaming-jar "$HADOOP_HOME/share/hadoop/tools/lib/hadoop-streaming-3.5.0.jar" \
  --output analytics-hadoop/runtime/dashboard.json
```

将 JAR 文件名替换为安装版本。快照保存到 `analytics-hadoop/runtime/exports/`。使用输出批次目录复核：

```bash
python3 analytics-hadoop/verify_result.py \
  --snapshot analytics-hadoop/runtime/exports/实际批次 \
  --result analytics-hadoop/runtime/dashboard.json
```

只读导出字段白名单，不含用户、手机号、订单号和凭据；非法时间、金额及完成但未付款记录拒绝发布。营收按完成订单付款日（北京时间）计算，分为金额单位；趋势包含连续30天，排行为有效站点Top10。窗口、去重、原子发布和HDFS留存口径见 `analytics-hadoop/README.md`。

## 二、Python机器学习

```bash
# 日期必须覆盖真实完整的历史区间，推荐45天。每次使用新的work-dir。
python3 ml/src/qt_pipeline.py \
  --database server-qt/runtime/charger.db \
  --start 2026-07-31T00:00:00Z --end 2026-09-14T00:00:00Z \
  --work-dir ml/outputs/qt-run-001 \
  --output ml/outputs/forecast.json
```

结束时间不填时使用当前UTC整点。输入为带时区时间，使用半开区间 `[start,end)`；每个站点至少需足够历史满足168小时滞后、24小时标签和70/10/20时间切分。空历史、尚未停止且与窗口重叠的充电订单、清洗拒绝记录会阻止新结果发布，保留上次结果供排障。

导出保留 Qt 的真实站点/电桩主键。读取已停止的充电量，包括尚未付款的充电；不读取个人信息。各表在同一SQLite只读事务中读取。不写入业务库。

`pile_status_logs` 是状态变更日志，不能当作连续设备心跳：沿用既有清洗规则最多保持2小时，随后按未知不可用处理；RESERVED按不可用处理。长时间无事件可能低估可用桩数。接入真实周期状态观测后可改善此限制，不回填未来状态。

16维特征、24个岭回归预测头，验证集选择岭回归/持续值基线；训练和验证边界各剔除穿越未来24小时的标签。负荷裁剪到容量，可用桩数由平均单桩功率换算并扣除已知不可用数；这是基线估计，不是独立训练的逐桩分类器。

输出位于work-dir：

- `data/`：脱敏CSV、目录、小时数据与清洗质量报告。
- `model.json`及元数据：Python V4模型、16维特征、24个预测头。
- `metrics.json`：每个lead的负荷MAE/RMSE及持续值基线；`*.evaluation.csv.split.json`证明时间边界隔离。
- `evaluation.svg`、`evaluation.json`、`stations_evaluation_report.md`：留出集1/6/24h曲线、指标及误差分析。
- `predictions.json`：每站连续24小时，分别取前1/6/24点作为各预测窗口。
- `--output`：原子发布、供Qt读取的结果JSON。

无订单小时视为零依赖“完整历史导出”的前提；不得用新建空库伪装45天实测历史。合成数据库必须附加 `--simulated`。真实数据不足时使用离线demo验证程序，不声称已得到真实预测效果。

## 三、接入Qt

```bash
export ANALYTICS_RESULT_PATH="$PWD/analytics-hadoop/runtime/dashboard.json"
export ML_RESULT_PATH="$PWD/ml/outputs/forecast.json"
server-qt/build/charger-server --database server-qt/runtime/charger.db \
  --host 127.0.0.1 --port 9000 --dashboard-port 19001
```

19001是此示例HTTP端口，避免与同机HDFS 9001冲突。业务TCP端口仍为9000。

- `GET /api/analytics`：大屏已有消费链路，读取Hadoop统计。
- `GET /api/predictions`：读取Python预测。返回 `available/stale/age_seconds/data`；缺失、损坏、不完整预测返回503及reason；历史预测返回200但`stale=true`，调用方须显示过期提示。
- `ML_MAX_AGE_SECONDS`默认7200，过期依据数据截止时刻，重新写文件不会让历史预测变新。

机器学习字段契约见 [contracts/ml-predictions.md](../contracts/ml-predictions.md)。业务数据库和结果必须来自同一个实例；迁移到别的数据库必须重新导出，不复用数值ID。旧 `post_predictions_api.py` 针对历史FastAPI分支，不能用于当前Qt主分支。

## 四、可重复验收

```bash
python3 -m unittest discover -s analytics-hadoop -p 'test_*.py' -v
python3 -m unittest discover -s ml/tests -p 'test_*.py' -v
cmake -S server-qt -B server-qt/build -DBUILD_TESTING=ON
cmake --build server-qt/build -j3
ctest --test-dir server-qt/build --output-on-failure
# 生成真实Qt结构的合成数据库，训练/预测，运行Hadoop并核对SQL，再启动真实Qt测试HTTP。
python3 ml/tests/test_qt_pipeline.py \
  --server server-qt/build/charger-server \
  --work-dir ml/outputs/acceptance-new \
  --streaming-jar "$HADOOP_HOME/share/hadoop/tools/lib/hadoop-streaming-3.5.0.jar"
```

工作目录必须不存在。省略streaming-jar仅验证本地MapReduce，结果明确标记local；完整Hadoop验收必须提供JAR。测试数据库及HTTP服务独立创建，不操作生产实例；结束自动停止测试Qt进程。结果见 `acceptance.json`。
