# SQLite 导出与 MapReduce 统计

当前已完成独立导出模块 `export_orders.py`。需要 Python 3.9 或以上，仅使用标准库；此步骤不需要 Hadoop，也不改动 Qt、Vue 或数据库结构。

## 运行

在项目根目录执行，数据库路径替换为实际运行中的 SQLite 文件：

```bash
python analytics-hadoop/export_orders.py --database server-qt/runtime/showcase.db --output-dir analytics-hadoop/runtime/exports
```

命令输出新批次目录的绝对路径，每批包含：

- `orders.jsonl`：UTF-8，每行一笔订单，按数据库订单 id 排序。
- `metadata.json`：批次 ID、快照读取开始时间、导出完成时间、订单总数、有效站点名称及 ID、单位、文件 SHA-256。

每次导出创建独立目录；全部写入成功后才将临时目录改名发布。异常返回非零退出码，不发布半成品，不覆盖上次批次。下游只读取非 `.export-` 开头的批次目录。

## 字段和口径

| 字段 | 含义 |
|---|---|
| station_id | 站点 ID |
| status | 原始订单状态，保留全部六种状态 |
| created_at / started_at / paid_at | 原始带时区时间；未发生的可选事件保留 null |
| created_day / started_day / paid_day | 对应北京时间日期 YYYY-MM-DD |
| amount_cents | 数据库记录金额，单位分；null 转 0 |
| energy_wh | 数据库记录电量，单位 Wh；null 转 0 |

不输出用户 ID、手机号、订单编号、头像或凭据。导出所有历史订单，不预筛选近 30 日，不预聚合，也不把未付款金额算作营收。后续 MapReduce 按创建日期计算订单数、付款日期计算已完成订单营收、开始充电日期计算电量。元数据只包含当前有效站点，供后续排行使用；无效站点历史订单仍保留在订单导出中。

充电中订单只导出数据库已记录的电量和金额，不调用 Qt 的动态计费估算。缺失创建时间、无时区或无效时间、负数/非整数金额电量、已完成但没有付款时间的订单会使整批失败，供修正源数据后重试。

## 一致性与只读保证

连接使用 SQLite URI `mode=ro` 和 `PRAGMA query_only=ON`，不会创建缺失数据库。站点和订单在同一个读事务中读取，能够读取已提交的 WAL 数据；不使用会忽略并发变化的 `immutable` 模式。关闭连接后释放读事务。

WAL 模式下允许业务并发写入，导出期间提交的付款在下一批出现。非 WAL 模式下长读事务可能延迟写入，应在低负载时导出；此程序不替业务数据库切换日志模式。SQLite 可能维护共享内存协调信息，但导出不会改写业务数据或执行迁移。测试分别核对数据库主文件和 WAL 文件字节不变。

`snapshot_at` 是首次查询前记录的 UTC 时间，标记快照读取开始，不是 SQLite 事务版本号。日期归属使用 UTC+8，与当前项目北京时间口径一致。

## 验证

```bash
python -m unittest discover -s analytics-hadoop -p test_export_orders.py -v
```

测试使用项目真实迁移结构创建临时数据库，覆盖空表、跨日、全部状态、旧订单、空值、隐私字段、非法时间、失败清理、禁止覆盖、缺失数据库、WAL 读取和并发付款快照一致性。

## 第二步：MapReduce 统计

`mapper.py` 将每笔订单映射为可累加指标；`reducer.py` 汇总相同键，同时可作为 Combiner。
`pipeline.py` 接收第一步的完整批次，核对 SHA-256 和行数，计算并原子发布一个统计 JSON 文件。

在项目根目录进行本地验证（将批次目录替换为导出命令输出的路径）：

```bash
python analytics-hadoop/pipeline.py --snapshot analytics-hadoop/runtime/exports/批次目录 --mode local --output analytics-hadoop/runtime/dashboard.json
```

也可直接从业务数据库只读导出后计算：

```bash
python analytics-hadoop/pipeline.py --database server-qt/runtime/showcase.db --mode local --output analytics-hadoop/runtime/dashboard.json
```

本地模式在内存排序，适用于小规模验证，结果明确标记 `local-mapreduce`，不表示在 Hadoop 上运行。

### Hadoop 集群执行

在已配置 Hadoop 客户端的 Linux 主机运行，要求 HDFS、YARN 正常且每个计算节点能运行 `python3`：

```bash
python3 analytics-hadoop/pipeline.py \
  --snapshot /path/to/export-batch \
  --mode hadoop \
  --streaming-jar /path/to/hadoop-streaming-version.jar \
  --hdfs-root /user/charger/dashboard \
  --output analytics-hadoop/runtime/dashboard.json
```

替换示例路径，Streaming JAR 使用当前 Hadoop 安装中的实际文件。作业基于 [Apache Hadoop Streaming](https://hadoop.apache.org/docs/stable/hadoop-streaming/HadoopStreaming.html) 的标准输入/输出协议，并通过 `-files` 分发脚本。

执行流程：创建唯一 HDFS 批次目录 → 上传订单至 input、元数据至批次根目录 → 提交 YARN 作业（2 个 Reducer）→ 检查 `_SUCCESS` → 读取所有 `part-*` → 校验处理笔数 → 发布结果。保留各批输入输出用于核对；本程序不删除 HDFS 数据。失败不会自动切成本地模式。

元数据文件不进入 Mapper 输入目录。输出文件使用临时文件原子替换，计算或校验失败时上次结果保持不变。同一输出路径通过 `.lock` 文件防止并发覆盖；进程被强制终止后，确认旧进程已退出才能人工移除残留锁并重试。导出批次发布后应视为不可变，运行期间不要编辑其文件。

### 统计结果契约

| 字段 | 含义 |
|---|---|
| schema_version | 当前为 1 |
| engine | local-mapreduce 或 hadoop-mapreduce |
| run_id / source_batch_id / source_sha256 | 计算批次与输入溯源 |
| snapshot_at / generated_at | 源快照时间与计算完成时间 |
| window_start / window_end | 北京时间连续 30 日，包含快照当天 |
| input_orders | 本批处理的全部订单数 |
| total_revenue_cents | 全部历史 COMPLETED 订单金额之和，与现有 Qt 累计口径一致 |
| revenue_trend | days=30，items 含 date、order_count、energy_wh、revenue_cents |
| station_ranking | 最近 30 日有效站点营收 Top 10，含 station_id、station_name、revenue_cents、order_count |
| hdfs_path | Hadoop 作业批次路径，本地模式为 null |

近 30 日窗口按输入快照时间确定，之后重跑不会漂移到运行当天。该功能不提供历史时点状态回放。
趋势订单数按创建日计算（包含全部状态）；电量按开始充电日归属；营收按已完成订单付款日归属。
排行中的 order_count 是窗口内已付款完成订单数，与趋势的订单数含义不同。
无订单日期补零；有效站点无收入也参与排序，营收相同按站点 ID 升序；无效站点不进入排行，其历史收入仍计入全平台趋势与总额。

### 当前验证范围

```bash
python -m unittest discover -s analytics-hadoop -p 'test_*.py' -v
```

18 项测试通过，包括导出测试和新增的 SQLite 独立查询核对、30 日边界与补零、未付款排除、Top 10、三个 Mapper/Combiner 与两个 Reducer 的标准输入输出计算、校验失败保留原结果。Hadoop 提交命令测试使用替身验证参数顺序、YARN 配置、成功标记及读取全部分片。

2026-09-12 已在虚拟机 Hadoop 3.2.1 的 HDFS/YARN 环境完成真实作业：业务库空订单快照和 23 笔合成订单均成功，独立 SQLite 查询核对通过。详情见 [验证记录](VALIDATION.md)。Qt 统计接口及 Vue 接入已实现；Qt/Vue 实时链路已在浏览器验证，Hadoop 批次页面的浏览器展示仍待独立验证。

可对真实作业输出进行独立核对（通过原始时间字段在 SQLite 中重新按 UTC+8 计算，而非复用 Mapper）：

```bash
python analytics-hadoop/verify_result.py --snapshot /path/to/export-batch --result analytics-hadoop/runtime/dashboard.json
```
