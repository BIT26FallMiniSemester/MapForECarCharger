# ADS 只读 API

Flask 只读取 `build_ads.py` 导出的 JSON，不直接查询 ODS/DWD，也不在请求中启动 Spark。

```bash
python3 -m pip install -r requirements.txt
export ADS_ROOT=/home/zjs/map-for-ecar/runtime/warehouse/ads
export ADS_BATCH_ID=sim-quick-2026-09-14-seed20260914
export ML_PREDICTIONS_PATH=/home/zjs/map-for-ecar/ml/models/stations_predictions.json
python3 app.py
```

健康检查为 `/health`，新接口位于 `/api/v1/*`。`/api/dashboard` 和
`/api/analytics` 用于兼容现有 Vue 大屏。

`ML_PREDICTIONS_PATH` 可选。配置后 Flask 把组员 ML 模块的逐站预测汇总为
未来 1～6 小时的平台负荷、空闲桩和拥堵度；未配置时返回空预测，不伪造数据。
