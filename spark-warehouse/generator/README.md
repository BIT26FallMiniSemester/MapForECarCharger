# 模拟数据生成器

`generate_all.py` 先生成满足业务约束的干净数据，再复制数据并按 `conf/quality_rules.yaml` 注入问题。生成器不读取质量检测结果，也不写入业务 SQLite。

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
