# 模拟数据生成器

`generate_all.py` 先生成满足业务约束的干净数据，再复制数据并按 `conf/quality_rules.yaml` 注入问题。生成器不读取质量检测结果，也不写入业务 SQLite。

当前 `quick` 为 45 日、100 站、1 万订单、1.5 万状态事件；`full` 为 120 日、2,614 站、40 万订单、65 万状态事件。快/慢充各有三个模拟额定功率档位；工作日早晚高峰与周末午间高峰按北京时间生成。配置中的当前故障桩比例为 10%、离线桩为 2%，仅从非预约/非充电桩选取，并在最后一条状态事件记录 `IDLE→FAULT/OFFLINE`。这是刻意提高的演示比例，不是实际设备故障率。其他历史状态事件只是允许的模拟转移样本，不可据此推断逐桩真实停机时长。

生成器默认自动使用运行时的北京时间当天作为结束日期，YAML 不再写死日期；调用代码仍可显式传入 `end_date` 以便做可复现测试。历史订单按日期均匀抽样，当天历史订单不会跨到次日。`full` 另外增加 100 单当天 `CHARGING`，所以新批次共 400,100 单；每单使用不同的空闲桩和用户，记录匹配的 `RESERVED→CHARGING` 事件。问题注入只作用于历史订单，避免快照的 100 单被随机隔离。这些是**模拟快照**，不是线上设备订单。`full` 使用种子 `20260917`，已有批次不会被覆盖。

快速验证：

```bash
python spark-warehouse/generator/generate_all.py --profile quick
```

全量生成：

```bash
python spark-warehouse/generator/generate_all.py --profile full
```

输出到 `spark-warehouse/runtime/generated/<batch_id>/`，包括：

- `clean/`：满足业务约束的基准 CSV。
- `dirty/`：供 ODS 导入的含质量问题 CSV。
- `_injected_issues.jsonl`：只用于测试核对，不允许质量检测作业读取。
- `metadata.json`：规模、种子、日期范围、文件行数与 SHA-256。

相同配置和种子会产生内容相同的数据文件。批次目录已存在时拒绝覆盖。
