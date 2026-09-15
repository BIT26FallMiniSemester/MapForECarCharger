# 充电平台模拟数据、质量规则与数仓分层设计

## 目标与边界

数据源以第一阶段 SQLite 迁移为准。模拟数据进入新的离线分析链路，不回写业务数据库：

```text
第一阶段表结构 → 模拟 CSV/JSON → HDFS ODS → PySpark 质量检测
→ DWD 清洗明细 → SparkSQL DWS 汇总 → ADS 指标 → Flask → Vue/ECharts
```

保留 Qt 业务系统作为第一阶段成果；大数据大屏改由 Flask 提供 ADS 数据。实时订单演示可以继续读 Qt，但必须在页面上与离线批次指标区分。

## 数据规模

默认生成 90 个自然日，固定随机种子，保证每次课堂演示可复现：

| 数据集 | 建议规模 | 说明 |
|---|---:|---|
| users | 10,000 | 注册时间覆盖 90 日，含 NORMAL/FROZEN |
| stations | 使用现有 2,614 条 | 保留北京真实站点维度，分析副本中注入质量问题 |
| charging_piles | 约 32,000 | 从站点快慢充接口数展开 |
| charging_orders | 300,000 | 呈现工作日/周末、早晚高峰、区域和季节差异 |
| recharge_records | 50,000 | 与用户和订单消费形成大致合理的余额变化 |
| pile_status_logs | 500,000 | 支持设备状态变化和故障分析 |

开发快速模式按上述规模的 1% 生成，验收模式使用全量。`admins` 和 `operation_logs` 不进入第一版经营大屏，避免把管理审计数据与业务事实混在一起；后续可单独建设审计主题。

每条 ODS 记录额外增加 `batch_id`、`source_file`、`ingest_time`、`row_id`。`row_id` 是生成阶段的唯一追踪键，即使业务主键被污染也能定位原始行。

## 模拟业务场景

- 07:00–09:00、17:00–20:00 为充电高峰；周末商业区需求上升。
- 核心城区订单密度较高，但站点容量差异影响利用率。
- FAST 桩功率、电量和费用通常高于 SLOW 桩。
- 少量电桩出现 FAULT/OFFLINE，并在状态日志中留下恢复过程。
- 订单完整生命周期满足 `PENDING → RESERVED → CHARGING → UNPAID → COMPLETED`，取消订单在合理阶段转为 CANCELLED。

## 数据质量问题注入

质量问题采用独立规则配置，默认脏数据行占约 8%，允许同一行命中多个规则。每条注入记录写入 `_injected_issues.jsonl`，仅供验收核对，质量检测程序不能读取它来“发现”问题。

| 规则编号 | 数据集 | 问题 | 默认比例 | 清洗策略 |
|---|---|---|---:|---|
| DQ001 | users | 手机号为空或格式错误 | 0.4% | 隔离；不能猜测手机号 |
| DQ002 | users | phone 或 id 重复 | 0.4% | 按 updated_at 保留最新，记录重复数 |
| DQ003 | stations | 经纬度越界/经纬度互换 | 0.3% | 可判断互换则修复，否则隔离 |
| DQ004 | stations | district、operator_name 缺失或有多余空格 | 0.7% | 去空格；维表映射补全，无法补全标 UNKNOWN |
| DQ005 | charging_piles | station_id 不存在 | 0.3% | 隔离 |
| DQ006 | charging_piles | 非法 charge_type/status | 0.4% | 大小写/空格可标准化，否则隔离 |
| DQ007 | charging_orders | order_no 重复 | 0.5% | 按 updated_at 保留最新 |
| DQ008 | charging_orders | user/station/pile 外键不存在或桩不属于站点 | 0.8% | 隔离 |
| DQ009 | charging_orders | started_at 晚于 stopped_at，或 paid_at 早于 stopped_at | 0.6% | 隔离；不交换业务事件时间 |
| DQ010 | charging_orders | 负电量、负金额、负时长 | 0.5% | 隔离 |
| DQ011 | charging_orders | COMPLETED 缺 paid_at/amount/energy | 0.5% | 可由可靠字段重算金额时修复，否则隔离 |
| DQ012 | charging_orders | 金额与 `energy_wh × price/1000` 偏差过大 | 0.6% | 误差≤1分视为舍入；其余隔离 |
| DQ013 | charging_orders | 状态与时间字段矛盾 | 0.6% | 按明确规则修复可选字段，否则隔离 |
| DQ014 | recharge_records | user_id 不存在、金额≤0 | 0.4% | 隔离 |
| DQ015 | pile_status_logs | old_status/new_status 非法或时间倒序 | 0.5% | 标准化合法值，其余隔离 |
| DQ016 | 多表 | 日期格式混用、空格、大小写异常 | 0.8% | 统一 ISO 8601 UTC、trim、upper |
| DQ017 | charging_orders | 同一电桩充电时段重叠 | 0.5% | 保留先开始且可信度高的订单，其余隔离 |
| DQ018 | charging_orders | 电量超过额定功率乘以有效充电时长 | 原模拟库中存在 | 隔离，不凭空修改电量或时长 |

质量报告按 `batch_id + table_name + rule_id` 输出总行数、问题行数、问题率、样例 row_id、检测时间和严重级别。问题率既统计“规则命中次数”，也统计“去重后的问题行数”，避免一行多错导致比例误读。

## ODS 层

ODS 保留原值，不做覆盖性修复，使用 Parquet 存储并按批次分区：

```text
/user/zjs/map-for-ecar/warehouse/ods/ods_users/batch_id=YYYYMMDDHHMMSS/
/user/zjs/map-for-ecar/warehouse/ods/ods_stations/batch_id=.../
/user/zjs/map-for-ecar/warehouse/ods/ods_charging_piles/batch_id=.../
/user/zjs/map-for-ecar/warehouse/ods/ods_charging_orders/dt=YYYY-MM-DD/batch_id=.../
/user/zjs/map-for-ecar/warehouse/ods/ods_recharge_records/dt=.../batch_id=.../
/user/zjs/map-for-ecar/warehouse/ods/ods_pile_status_logs/dt=.../batch_id=.../
```

ODS 时间和金额可以先以字符串保留；原始空值、非法格式不得在读入时自动变成无法区分的 null。每次读取使用显式 schema，并把解析失败标志写入质量结果。

## DWD 层

| 表 | 粒度 | 主要字段 |
|---|---|---|
| dim_user | 一名用户 | user_id、masked_phone、nickname、status、register_time、effective/current 标志 |
| dim_station | 一个站点 | station_id、名称、行政区、运营商、经纬度、服务/位置类型、快慢接口数、status |
| dim_pile | 一个电桩 | pile_id、station_id、pile_no、charge_type、rated_power_w、status |
| dwd_charging_order_detail | 一笔有效订单 | 各维度键、状态、全生命周期时间、duration_seconds、energy_wh、price/amount、日期/小时派生字段、quality_flags |
| dwd_recharge_detail | 一次有效充值 | record_id、user_id、amount_cents、balance_after_cents、recharge_time |
| dwd_pile_status_event | 一次状态变化 | log_id、pile_id、order_id、状态前后、reason、event_time |
| dwd_quarantine | 一条不能可靠修复的记录 | batch、表、row_id、原始 JSON、rule_ids、隔离时间 |

DWD 不保留明文手机号供分析使用；用掩码或散列标识。金额统一为分、电量统一为 Wh、功率统一为 W、时间统一为 UTC timestamp，并另派生北京时间 `biz_date`、`biz_hour`。

## DWS 层

| 表 | 粒度 | 指标 |
|---|---|---|
| dws_station_day | 站点×日 | 创建订单、完成订单、取消订单、充电量、营收、平均客单价、平均时长、活跃桩数 |
| dws_platform_day | 平台×日 | 新增用户、活跃用户、订单、完成率、取消率、充电量、营收、充值额 |
| dws_district_day | 行政区×日 | 站点数、订单数、充电量、营收、平均桩利用率 |
| dws_pile_day | 电桩×日 | 充电次数、充电分钟数、电量、收入、故障次数、离线分钟数、利用率 |
| dws_data_quality_batch | 批次×表×规则 | 总数、命中数、问题率、修复数、隔离数 |

利用率定义为 `充电分钟数 / 当日可服务分钟数`；不能用当前 CHARGING 桩数代替全天利用率。营收只统计 COMPLETED 且 paid_at 有效的订单。

## ADS 层与可视化接口

本轮实际分析维度为 10 个：业务日期、站点、区域、电桩、桩状态、质量规则、运营商、快慢充类型、工作日/周末、充电开始小时。前六个由既有 ADS 趋势、排行、区域、桩利用率、状态和质量规则提供；运营商来自站点排行；后三个在下述交叉对比中使用。指标统一以清洗后 DWD 为来源，不混入被隔离订单。

两组至少双维对比：

| Spark ADS | 对比维度 | 指标 | Flask 接口 |
|---|---|---|---|
| `ads_district_charge_type_30d` | 区域 × 快/慢充类型 | 已完成订单数、电量 Wh、营收分、每站订单数 | `/api/v1/comparisons` |
| `ads_day_type_hour_30d` | 工作日/周末 × 充电开始小时 | 已完成订单数、电量 Wh、营收分、日均订单数 | `/api/v1/comparisons` |

Vue 3 大屏使用两个 ECharts 图并排显示。两个分组都限定最近 30 日已完成订单；`day_type_hour` 的小时来自 `started_at`，不是支付小时。区域图按站点数、工作日/周末图按该窗口实际日数归一化，避免把“站点更多”“工作日更多”误当作单位效率差异。Flask 只读 ADS JSON，不在 Web 请求中启动 Spark。

| ADS 数据集 | Flask 接口 | Vue/ECharts 用途 |
|---|---|---|
| ads_overview | `/api/v1/overview` | 总营收、当日营收/订单/电量、质量分数 |
| ads_revenue_trend_30d | `/api/v1/trends/revenue?days=30` | 营收与订单趋势 |
| ads_station_ranking_30d | `/api/v1/rankings/stations?days=30&limit=10` | 站点排行 |
| ads_district_distribution | `/api/v1/distribution/districts` | 区域分布与地图 |
| ads_pile_utilization | `/api/v1/piles/utilization` | 电桩利用率与状态分析 |
| ads_quality_overview | `/api/v1/quality/overview` | 问题总量、修复率、隔离率 |
| ads_quality_rules | `/api/v1/quality/rules` | 各规则问题数与趋势 |

ADS 推荐写为少量 JSON 或 Parquet 后由 Flask 加载缓存。课程规模下不需要让每次 HTTP 请求直接启动 Spark 作业。所有接口返回 `batch_id`、`generated_at`、`data_as_of`，页面展示数据批次时间。

## 目录设计

```text
spark-warehouse/
  conf/                 # 规模、质量注入规则、路径配置
  generator/            # 干净数据生成与问题注入
  jobs/
    ingest_ods.py
    detect_quality.py
    build_dwd.py
    build_dws.py
    build_ads.py
  sql/                  # DWS/ADS SparkSQL
  schemas/              # 显式 PySpark schema
  tests/                # 小规模固定数据验收
  flask-api/            # 只读 ADS API
  docs/
web-bigscreen/          # 复用并改接 Flask API
```

## 验收原则

1. 固定种子下，注入清单、检测结果和清洗结果可重复。
2. 每个 DQ 规则至少有一个阳性样例和一个不会误报的反例。
3. `ODS 行数 = DWD 有效行数 + 隔离行数 + 明确去重行数`，差异可追踪。
4. DWS/ADS 指标与小样本独立 SQL 结果一致。
5. Flask 不读取 ODS/DWD，不执行 Spark；只提供 ADS。
6. Vue 明确显示批次时间和数据质量信息，不把离线结果称为实时数据。
