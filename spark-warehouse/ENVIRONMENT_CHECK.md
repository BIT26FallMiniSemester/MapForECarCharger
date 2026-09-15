# 大数据阶段环境检查

检查日期：2026-09-15。

## zjs 迁移后虚拟机基线

本次按用户要求升级 Java 17、PySpark 4.2.0 和 Node 24，独立 Python 环境为 /home/zjs/apps/map-for-ecar-spark42。Hadoop 3.2.1 保留原 Java 8 配置与数据，Node 支持 20.19+ 或 22.12+。下文为贡献分支提供的独立环境记录，不是 zjs 虚拟机的安装状态；不要按下文直接覆盖当前环境。

## 贡献分支的环境记录

| 组件 | 状态 | 证据与处理 |
|---|---|---|
| Hadoop / YARN | 本轮未复测 | 本轮明确以 Spark `local[2]` 验证，不声称已部署到集群。|
| Java | 已验证 | Ubuntu 使用 OpenJDK 17.0.20.1。|
| Python | 已验证 | macOS 3.14；Ubuntu 3.14.4。|
| Spark / PySpark | 已验证 | Ubuntu 隔离目录使用 Spark/PySpark 4.2.0，完整跑通 ODS → ADS。|
| Flask | 已验证 | macOS 项目运行目录使用 Flask 3.1.0，13 个实际路由均返回 200。|
| Node.js / Vue | 已验证 | Node v24.19.0/v24.20.0，满足 `>=23`；Vue 3.5.25，6 项测试及生产构建通过。|
| Qt 6 / PyCharm | 本轮未复测 | 与本轮 Spark/Flask/Vue 验收无关。|

Spark 4.2.0 本轮只解压到 `/tmp` 隔离目录，Flask 依赖只安装到 Git 忽略的
`spark-warehouse/runtime/host-python`，未改系统 Python 或全局 Java/Spark。

## 建议的软件基线

- Java 17
- Spark/PySpark 4.2.0
- Python 3.10 以上，Driver 与 Worker 版本一致
- Flask 3.1
- Node.js 23 以上、Vue 3（当前为 Node 24、Vue 3.5）

集群以 `spark-submit` 的 Spark 安装为准；若目标集群不是 4.2.0，应先统一
PySpark 与 Spark 版本再部署。

## 集群部署前检查命令

```bash
java -version
hadoop version
jps
hdfs dfsadmin -report
yarn node -list
spark-submit --version
pyspark --version
python3 --version
python3 -c "import pyspark; print(pyspark.__version__)"
```

## 环境验收条件

1. 本地验收：Spark `local[2]` 全流程、Flask API、Vue 代理和生产构建均通过。
2. 集群验收：HDFS 至少有 1 个 Live DataNode，YARN 至少有 1 个 RUNNING NodeManager。
3. `spark-submit --master yarn` 能完成一个读取 HDFS、写回 HDFS 的小作业。
4. PySpark Worker 使用的 Python 版本与 Driver 一致，项目账号能读写目标 HDFS 目录。
