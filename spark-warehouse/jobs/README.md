# PySpark 作业

作业按 `ingest_ods → detect_quality → build_dwd → build_dws → build_ads` 执行。所有作业接收 `batch_id`，不得通过扫描“最新目录”隐式选择输入。

当前已实现：

```bash
spark-submit jobs/ingest_ods.py \
  --master 'local[*]' \
  --input runtime/generated/sim-quick-2026-09-14-seed20260914 \
  --output runtime/warehouse/ods

spark-submit jobs/detect_quality.py \
  --master 'local[*]' \
  --ods runtime/warehouse/ods \
  --output runtime/warehouse/quality-reports \
  --batch-id sim-quick-2026-09-14-seed20260914
```

`detect_quality.py` 只分析 ODS 数据，不读取 `_injected_issues.jsonl`。明细按“批次、表、row_id、规则”去重，汇总同时给出命中数、受影响行数、问题率和最多 10 个样例 `row_id`。

完整 quick 流水线：

```bash
./scripts/run_quick_pipeline.sh
```

也可以显式执行：

```bash
python3 jobs/run_pipeline.py \
  --master 'local[2]' \
  --input runtime/generated/sim-quick-2026-09-14-seed20260914 \
  --warehouse runtime/warehouse \
  --batch-id sim-quick-2026-09-14-seed20260914
```

Ubuntu 单机内存较小时先用 `local[2]` 完成验收；切换 YARN 前需要先调整容器与 Java 堆内存。
