# ML 负荷预测：独立运行 / 可选后端接入

依赖：Python 3.10+ 标准库与 C++17 编译器（Ubuntu g++，macOS clang++），不依赖第三方 Python 包、数据库或在线后端。以下命令从仓库根目录执行。

## 一键离线验收

~~~bash
python3 ml/src/workflow.py demo ml/outputs/demo --compiler g++
python3 -m unittest discover -s ml/tests -p 'test_*.py' -v
~~~

demo 输出目录必须不存在或为空，防止覆盖成果。重新运行请选择新目录。命令依次生成模拟订单/设备日志、清洗小时数据、编译、训练、预测、推荐/预警、评估图和 API 请求体，**不会发出网络请求**。

输出包括 data/orders.csv、data/devices.csv、data/history.csv、清洗质量报告、模型及特征元数据、manifest.json、predictions.json、operations.json、evaluation.svg、evaluation.json、predictions_api.json。

## 图片要求逐项对应

| 功能 | 实现入口 | 验收产物/行为 |
| --- | --- | --- |
| 可重复模拟历史数据 | prepare_history.py generate | 固定 seed/截止时间；覆盖站点、订单、电量、时长、设备状态 |
| 数据提取与清洗 | prepare_history.py clean | CSV 字段白名单；去重、缺失/非法值/功率越界/重叠订单处理、小时对齐；质量报告 |
| 时序及运营特征 | main.cpp | 16 维：小时/星期周期、周末、显式节假日、负荷滞后/均值、空闲率、站点 ID、总桩数、容量 |
| 可解释基线训练 | pklot_ml train | 岭回归系数、标准化参数、版本元数据；验证集选择持续值基线；时间隔离测试集 |
| 未来 1 小时 | pklot_ml predict | 每站第 1 小时负荷及 UTC 时间点 |
| 未来 6 小时 | 同上 | 第 1～6 小时连续序列，不再只有第 6 小时一个点 |
| 未来 24 小时/高峰 | predict + workflow.py analyze | 第 1～24 小时序列、最高负荷时间、超过阈值的时间点 |
| 未来可用桩数量 | pklot_ml predict | 平均单桩功率换算，扣除已知不可用桩；限制在 [0, 总桩数] |
| 低拥堵推荐及原因 | workflow.py analyze | 用户距离、当前空闲率、未来拥堵度；输出评分、排序及三项原因 |
| 高峰/设备异常提示 | 同上 | 容量比阈值预警；故障/离线/缺失设备状态提示 |
| 指标、对比图、误差分析 | workflow.py report | 每个 lead 的全量 MAE/RMSE；留出集单站真实/预测曲线、偏差、最大绝对误差 |
| 内部接口输出 | export_predictions_api.py、post_predictions_api.py | 动态站点映射；按契约写入 station、horizon、时间、type、value、model_version |

## 数据清洗入口

~~~bash
python3 ml/src/prepare_history.py generate ml/outputs/raw --days 45 --seed 42
python3 ml/src/prepare_history.py clean \
  ml/outputs/raw/catalog.json ml/outputs/raw/orders.csv ml/outputs/raw/devices.csv \
  ml/outputs/history.csv --start 2026-07-18T00:00:00Z --end 2026-09-01T00:00:00Z \
  --holidays ml/outputs/raw/holidays.json
~~~

输入 CSV 是 ML 文件接口，不是新增后端 API。实际数据库导出需要先将站点/电桩主键转换为所选 catalog 的 ID 空间，禁止混用公共目录序号和运行库主键。

- 目录 JSON：stations 的 id/name/latitude/longitude/data_source/external_id；charging_piles 的 id/station_id/rated_power_w。
- 脱敏订单 CSV：id,station_id,pile_id,started_at,stopped_at,energy_wh；结束时间和电量必填。duration_seconds 可随原数据导出，但清洗器从起止时间重算时长，不信任冲突的时长字段。不读取或保存用户姓名、手机号、支付信息。
- 设备 CSV：pile_id,reported_at,status；状态为 IDLE/CHARGING/FAULT/OFFLINE。这是设备状态日志导出，不等同于只含 ONLINE/OFFLINE 的心跳 API 请求体。
- 时间：UTC epoch 秒或带时区 ISO 8601；拒绝无时区字符串。输出 UTC 小时记录，日历特征按 Asia/Shanghai 计算。
- 节假日 JSON：本地日期字符串数组，例如 ["2026-10-01"]。由团队提供覆盖历史区间的实际日历；未提供时特征为 0 并标记未配置。demo 内是**合成测试日期**，不是官方日历。

清洗保留首次有效订单/事件，相同重复项跳过，冲突项计为拒绝；同桩重叠订单跳过。电量按持续时间均匀分摊至小时，得到小时平均功率及平均占用数量。设备状态只向前沿用最多 2 小时，未知状态视为不可用。所有丢弃/跳过计数写入 .quality.json。

必须提供完整时间范围的订单导出：无有效订单的小时按 0 处理，无法自动区分“没有订单”和“导出漏单”，不能用该假设掩盖缺失数据。

## 使用已有全量站点数据

~~~bash
cmake -S ml -B ml/build
cmake --build ml/build
./ml/build/pklot_ml train ml/data/stations_hourly.csv ml/models/stations_load_forecaster.txt ml/models/stations_metrics.json
./ml/build/pklot_ml predict ml/data/stations_hourly.csv ml/models/stations_load_forecaster.txt ml/models/stations_predictions.json
python3 ml/src/workflow.py report ml/models/stations_metrics.json.evaluation.csv ml/models/stations_metrics.json ml/models
~~~

没有 CMake 时：先创建输出目录，然后使用 c++ -O2 -std=c++17 ml/src/main.cpp -o ml/outputs/pklot_ml 编译。

V3 包含 24 个预测头与 16 维特征，旧 V2 文件必须重训，程序会明确拒绝旧格式。核心 JSON 的 horizon_hours 是逐点 lead（1～24），API 导出才组成窗口（1/6/24）。预测时间基于最后一条输入记录，不基于启动时间；model_version 与模型内容绑定。

训练按时间 70/10/20 切分，训练/验证边界清除未来 24 小时标签可能穿越的样本；边界证据见 *.evaluation.csv.split.json。评估与推理使用同样的容量裁剪规则。

## 推荐、峰值和告警

~~~bash
python3 ml/src/workflow.py analyze \
  ml/outputs/demo/data/catalog.json ml/outputs/demo/data/history.csv \
  ml/outputs/demo/predictions.json ml/outputs/demo/operations.json \
  --latitude 39.9 --longitude 116.4 --radius-km 20 --threshold 0.8
~~~

分数越低越好：0.4 × 距离/半径 + 0.3 × 当前忙碌率 + 0.3 × 下小时拥堵率。距离为球面直线距离，不是导航距离。缺坐标、超过半径、全部设备不可用的站点不参与推荐并注明原因。阈值和半径可调；这是可解释规则基线，不是学习得到的个性化推荐模型。

## 可选后端接入

以 api.md、database.md 为设计规范，当前分支 contracts/openapi.yaml 为机器契约。后端是唯一写库入口，不直接写数据库或自行创建表。

~~~bash
python3 ml/src/export_predictions_api.py ml/models/stations_predictions.json ml/data/stations_database.json ml/models/stations_predictions_api.json
# 先通过私密方式设置当前进程的 INTERNAL_KEY，勿写进命令历史或仓库。
python3 ml/src/post_predictions_api.py ml/models/stations_predictions_api.json http://127.0.0.1:8000/api/v1 --dry-run
# 确认映射和数据无误后才去掉 --dry-run，正式写入后端。
~~~

上传器读取环境变量 INTERNAL_KEY，通过 X-Internal-Key 发送。先分页请求 GET /api/v1/internal/stations/catalog-mappings，全部 (data_source,external_id) 映射成功后才写入 POST /api/v1/internal/predictions/load。

每站 3 个请求：1h 窗口 3 个点、6h 窗口 18 个点、24h 窗口 72 个点。每小时含 LOAD_W（kW×1000）、AVAILABLE_PILES、CONGESTION_SCORE；每个请求带 horizon_hours/model_version/generated_at，每个点带 prediction_type/predicted_for/predicted_value。不把 2～23 的 lead 当作后端窗口值发送。

--dry-run 仍访问后端获取映射，但不写入。单元测试使用模拟 HTTP 响应，不等于真实后端联调成功。demo 使用 ML_SYNTHETIC 目录标识，不应上传到未登记该来源的真实数据库。

## 数据和评估边界

现有目录含 2,614 站，其中 2,605 个有桩站点进入训练；小时负载是模拟数据。既有五列 CSV 无实际占用/设备/节假日记录，因此缺失占用率由负荷估算、不可用数按 0、节假日按 0；真实业务应使用订单/日志清洗入口。

可用桩和拥堵预测仍是平均功率换算基线，不是逐桩占用模型；已知不可用数量假设未来 24 小时不变。设备异常基于已观察的故障/离线/未知状态，不声称预测故障概率。曲线的“真实”指测试集观测标签，现阶段也属于模拟数据。部署时必须输入新数据重训/预测，不能把历史快照当作当前预测。
