# Qt Python预测读取契约 v1

HTTP `GET /api/predictions`，端口使用Qt `--dashboard-port`。复用当前Qt的只读结果文件模式；文件由Python原子发布，不能通过请求指定文件路径。

成功响应：`{"available":true,"stale":false,"age_seconds":1800,"max_age_seconds":7200,"data":{...}}`。
未配置、缺失或格式错误：HTTP503，`{"available":false,"reason":"not_configured|missing_result|invalid_result"}`（reason实际取其中一个值）。

`data`包含：

| 字段 | 类型/语义 |
| --- | --- |
| schema_version | 1 |
| source | python-ml |
| station_id_space | qt-database，来自源Qt数据库主键 |
| simulated | boolean，合成数据必须为true |
| model_version | 与模型文件SHA256绑定 |
| generated_at_epoch | 预测执行时UTC epoch秒 |
| forecast_origin_epoch | 最后一条小时输入UTC epoch秒，整点 |
| assumptions | 可用桩换算及状态数据限制 |
| predictions | 每站24个完整、不重复的逐小时点 |

每点包含：station_id（正整数）、horizon_hours（1至24）、predicted_for_epoch（origin+lead×3600）、predicted_load_kw（kW）、predicted_occupied_piles（整数）、predicted_available_piles（整数）、congestion_ratio（0至1）。

1/6/24小时窗口分别读取每站前1/6/24个点；负荷是功率，不能误作整段能量。需要窗口能量时，对每小时平均kW乘1小时求和得到kWh。HTTP响应不沿用历史FastAPI的`code/data`信封或`LOAD_W`类型点。

过期由forecast_origin计算，重新生成旧输入的结果仍会标记stale。不得把过期/模拟结果显示为实时观测。文件上限32MiB，缺少24小时点、重复lead、非整数桩数、非法时间与负数被拒绝。
