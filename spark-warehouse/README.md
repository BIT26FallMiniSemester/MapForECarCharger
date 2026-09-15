# PySpark 数据质量与离线数仓

本目录用于第二阶段：模拟数据、PySpark 数据质量检测和清洗、SparkSQL ODS/DWD/DWS/ADS 分层，以及 Flask + Vue + ECharts 展示。

- [环境检查](ENVIRONMENT_CHECK.md)
- [数据与分层设计](DATA_DESIGN.md)

当前已完成环境盘点、数据设计、固定种子的模拟数据生成器、ODS 导入、
DQ001–DQ018 质量检测、DWD 清洗隔离、DWS/ADS SparkSQL 聚合、Flask
只读 API 和 Vue 兼容适配。2026-09-15 已用原 SQLite 模拟库快照在
贡献分支的 Spark 4.2.0 `local[2]` 完成 ODS → DQ → DWD → DWS → ADS 全流程；运行
产物位于被 Git 忽略的 `runtime/`，不把测试批次提交进仓库。

合并后的依赖为 PySpark 4.2.0，使用 Java 17；zjs 的新版环境位于 /home/zjs/apps/map-for-ecar-spark42，Node 使用独立安装的 24 系列。Hadoop 3.2.1 HDFS 保留现有安装与数据，使用其原 Java 8 配置。--accept-cleaned 仅可配合 --simulated 使用，允许在清洗模拟脏数据后训练；默认严格门禁保留。

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

生产或课堂集群仍需用目标 HDFS/YARN 环境复跑；本次验收只证明单机 Spark
链路和 Web 联调，不把 `local[2]` 结果冒充集群部署结果。
