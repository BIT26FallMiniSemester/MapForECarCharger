# 大数据阶段环境检查

检查日期：2026-09-14。

## 当前结果

| 组件 | 状态 | 证据与处理 |
|---|---|---|
| Hadoop | 已安装，待复查本次启动状态 | 2026-09-12 已在 Ubuntu 验证 Hadoop 3.2.1、HDFS、YARN，可成功运行真实 MapReduce 作业。|
| Java | 已安装 | 虚拟机此前验证为 JDK 8，路径 `/home/zjs/apps/java`。|
| Python | 已安装 | Windows 有 Miniconda Python；虚拟机此前验证有 `python3`。|
| Spark / PySpark | 未确认 | 本次 SSH 到原地址 `192.168.88.128` 超时；恢复虚拟机后检查 `spark-submit --version` 和 `python3 -c "import pyspark"`。|
| Flask | 未确认 | 恢复虚拟机后检查 Python 模块；建议放入项目虚拟环境，不污染系统 Python。|
| PyCharm | 未发现 | Windows PATH 和 `C:\Program Files\JetBrains` 未发现 PyCharm；虚拟机状态中断，尚未检查桌面安装。|
| Node.js / Vue | 已可用 | `web-bigscreen` 已完成安装、测试和生产构建。|
| Qt 6 | 虚拟机中已可用 | 2026-09-12 已成功编译 Qt 服务并通过 CTest。|

本次先执行 `vmrun list` 时曾显示虚拟机，随后 SSH 超时，重新查询时 VMware 报告该虚拟机未开机。因此 Spark、PySpark、Flask、PyCharm 的最终状态必须在虚拟机稳定启动后补测，不能把“命令没有输出”当作未安装结论。

## 建议的软件基线

- Ubuntu 24.04（沿用现有虚拟机）
- Java 8（与现有 Hadoop 3.2.1 保持一致）
- Hadoop 3.2.1（沿用现有安装）
- Spark 3.5.x，使用 Hadoop 3 构建包
- Python 3.10 或 3.11、PySpark 与 Spark 主版本一致
- Flask 3.x、flask-cors（仅开发时跨域；生产使用同源反向代理）
- PyCharm Community，作为开发工具，不参与服务器运行
- Vue 3、Vite 5、ECharts 5（沿用现有大屏）

不要单独通过 `pip install pyspark` 引入一个与集群 Spark 不一致的版本。集群以 `spark-submit` 的 Spark 安装为准，Python 环境只补充项目需要的包。

## 虚拟机恢复后的检查命令

```bash
source ~/.hadoop_env
java -version
hadoop version
jps
hdfs dfsadmin -report
yarn node -list
spark-submit --version
pyspark --version
python3 --version
python3 -c "import pyspark, flask; print(pyspark.__version__, flask.__version__)"
```

PyCharm 可通过桌面菜单确认，或执行：

```bash
command -v pycharm || true
find ~/.local/share/applications /usr/share/applications -iname '*pycharm*' 2>/dev/null
```

## 环境验收条件

1. HDFS 至少有 1 个 Live DataNode，YARN 至少有 1 个 RUNNING NodeManager。
2. `spark-submit --master yarn` 能完成一个读取 HDFS、写回 HDFS 的小作业。
3. PySpark Worker 使用的 Python 版本与 Driver 一致。
4. 项目用户 `zjs` 能读写 `/user/zjs/map-for-ecar/`，无需使用 root 运行作业。
5. Flask 能读取 ADS 输出；Vue 能通过同源 `/api` 请求 Flask。
6. PyCharm 能打开项目、识别 Python 解释器；它不是部署依赖。
