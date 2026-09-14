# PySpark 数据质量与离线数仓

本目录用于第二阶段：模拟数据、PySpark 数据质量检测和清洗、SparkSQL ODS/DWD/DWS/ADS 分层，以及 Flask + Vue + ECharts 展示。

- [环境检查](ENVIRONMENT_CHECK.md)
- [数据与分层设计](DATA_DESIGN.md)

当前已完成环境盘点、数据设计、固定种子的模拟数据生成器、ODS 导入、
DQ001–DQ017 质量检测、DWD 清洗隔离、DWS/ADS SparkSQL 聚合、Flask
只读 API 和 Vue 兼容适配。quick 模式数据已实际生成；生成文件位于被
Git 忽略的 `runtime/generated/`。Spark 作业仍需在 Java/PySpark 安装后
完成首次引擎级联调。

## Windows / VS Code 验证

先在 Windows 主机打开仓库根目录，然后按 `Ctrl+Shift+P`，选择
`Tasks: Run Task` → `验证：主机完整检查`。该任务依次运行 Python 测试、
PySpark 作业语法检查和 Vue 生产构建。选择 `启动：Web 大屏` 可在 5174
端口运行前端。

Windows 未安装 Java/PySpark 时不执行 Spark 引擎集成测试；HDFS、YARN
和 Spark 的最终验证在 Ubuntu 虚拟机完成。

主机验证通过后同步到 Ubuntu：

```powershell
powershell -ExecutionPolicy Bypass -File spark-warehouse/scripts/sync_to_ubuntu.ps1
```

下一步实现 PySpark ODS 导入和 DQ001–DQ017 数据质量检测。Spark/PySpark/Flask/PyCharm 的实际安装状态仍需在虚拟机稳定启动后复查。
