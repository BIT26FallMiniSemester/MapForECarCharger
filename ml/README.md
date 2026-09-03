# ML 负荷预测

本模块支持两种运行方式：

- **独立运行**：C++17 程序在本地完成数据生成、训练和预测，不需要后端、网络或密钥。
- **接入后端**：将独立预测结果转换为接口请求，动态解析站点 ID 后上传。后端接入层不反向依赖到核心训练和预测程序。

- `data/`：站点库 JSON 和由其生成的模拟小时级 CSV。
- `models/`：基于站点库重训的岭回归模型、指标、评估报告、预测结果和后端 API 请求体。
- `src/`：C++ 训练/预测程序和站点库转换脚本。
- `notebooks/`：保留给后续可视化或对比实验。

`stations_database.json` 来自北京公共充电站目录，包含真实站点目录字段和按项目决策生成的桩记录；小时负载无真实记录，因此 `stations_hourly.csv` 是基于桩数、额定功率、工作日/周末和早晚高峰生成的可复现模拟数据，不能当作真实运营负载。

## 独立运行

在仓库根目录执行：

```bash
cmake -S ml -B ml/build
cmake --build ml/build
./ml/build/pklot_ml demo
```

完整离线流程：

```bash
./ml/build/pklot_ml generate ml/data/stations_hourly.csv 90 5
./ml/build/pklot_ml train ml/data/stations_hourly.csv ml/models/stations_model.txt ml/models/stations_metrics.json
./ml/build/pklot_ml predict ml/data/stations_hourly.csv ml/models/stations_model.txt ml/models/stations_predictions.json
```

`stations_predictions.json` 是独立模式的最终产物，可直接供本地分析使用。

## 后端对接

接口与数据库设计以团队提供的 `api.md`、`database.md` 为规范，机器校验以主分支 `contracts/openapi.yaml` 为准。预测结果只能通过后端内部接口写入，不直接操作数据库：

```bash
python3 ml/src/export_predictions_api.py ml/models/stations_predictions.json ml/data/stations_database.json ml/models/stations_predictions_api.json
INTERNAL_KEY=... python3 ml/src/post_predictions_api.py ml/models/stations_predictions_api.json http://192.168.137.128:8000/api/v1 --dry-run
INTERNAL_KEY=... python3 ml/src/post_predictions_api.py ml/models/stations_predictions_api.json http://192.168.137.128:8000/api/v1
```

导出器生成 `POST /api/v1/internal/predictions/load` 的请求体：每个站点和预测步长包含 `LOAD_W`、`AVAILABLE_PILES`、`CONGESTION_SCORE` 三个点。模型的 `predicted_load_kw` 会转换为后端规定的 `LOAD_W`；`generated_at` 使用生成时间，`predicted_for` 使用最后一条输入数据时间加预测步长，二者均输出为 ISO 8601 UTC。

上传器先通过 `GET /api/v1/internal/stations/catalog-mappings` 按 `data_source` 分页获取动态映射，再按 `(data_source, external_id)` 把目录标识转换为运行中数据库的 `stations.id`。全部映射成功后才会写入预测；`--dry-run` 会完成映射校验但不写入。

因此，后端暂时不可达或未配置 `INTERNAL_KEY` 时，仍可正常使用上面的独立模式；只有执行上传器时才需要后端服务。
