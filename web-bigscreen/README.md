# Vue 运营大屏（合并版）

默认使用 main 的 Qt 后端真实数据，分别请求 `/api/dashboard`（实时数据）与 `/api/analytics`（批处理历史统计）。每轮完成后等待 5 秒再刷新，避免请求重叠。

## 运行

先按照 `../server-qt/README.md` 构建并运行本合并目录中的 Qt 服务，HTTP 端口为 9001。

```bash
npm install
npm run dev
```

访问 http://localhost:5173。开发和 preview 代理默认指向 http://127.0.0.1:9001。
远程后端可在 `.env.local` 中设置 `DASHBOARD_TARGET=http://服务器地址:9001`，然后重启 Vite。

```bash
npm run build
npm run preview
```

正式部署 dist 时，需要由 Nginx 等服务器把 `/api/` 反向代理至 Qt 的 9001 端口。
Qt 的 9001 首页仍保留原有内嵌大屏；Vue 大屏单独部署。

如需离线演示，在 `.env.local` 设置 `VITE_USE_MOCK=true`，页面明确标记模拟数据。
默认不会在请求失败时自动退回模拟数据。

金额单位为分、电量为 Wh；预测图使用 Qt 原有的预测电量 kWh 和订单数，不把电量冒充瞬时功率。
排行统计最近 30 个自然日已付款订单营收，利用率为当前预约及充电桩占比。
最近订单的功率为充电桩额定功率，金额为数据库已记录金额，不代表实时计费估算。

## Hadoop 结果接入

仅联调 Qt 与 Vue 时，在 `.env.local` 中设置 `VITE_USE_ANALYTICS=false` 后重启 Vite。
此模式不请求 `/api/analytics`，全部图表直接使用 Qt 真实数据；不会启用 mock。
恢复 Hadoop 读取时删除该配置或改为 `true`。

远程 Qt 服务的 PowerShell 启动示例（替换服务器地址）：

```powershell
$env:DASHBOARD_TARGET='http://YOUR_QT_HOST:19001'
$env:VITE_USE_ANALYTICS='false'
npm run dev -- --host 127.0.0.1 --port 5174 --strictPort
```

访问 http://127.0.0.1:5174/。预览服务使用独立数据库副本，原业务库的新增订单不会自动复制到该副本。

先按 `../analytics-hadoop/README.md` 运行作业，然后在启动 Qt 服务的环境中设置：

```bash
export ANALYTICS_RESULT_PATH=/absolute/path/dashboard.json
export ANALYTICS_MAX_AGE_SECONDS=900
```

`ANALYTICS_RESULT_PATH` 必须与统计作业 `--output` 指向同一个已发布文件。Qt 每次请求读取该文件，批次更新后不需要重启。首次改变环境配置需要重启 Qt。

- Hadoop 结果用于累计营收、近 30 日趋势和站点营收排行。
- 今日指标、电桩状态、站点地图、最近订单和预测继续使用 Qt；累计营收明确标为批次值，因此与实时今日营收可能处于不同时间点。
- 结果缺失、损坏或未配置：HTTP 503，Vue 显示回退提示并使用 Qt 历史汇总。
- 结果超过默认 15 分钟：保留批次数据并标记过期；以源快照时间计算年龄，不以重算时间掩盖旧数据。
- 本地计算结果明确标为“本地验证计算”，不会显示为 Hadoop。
- 请求错误互不阻止另一类数据更新，组件退出时取消请求；原有数据保留时实时错误有单独提示。

这里只读取已发布结果，不通过浏览器触发 Hadoop，也没有自动定时运行作业。

验证命令：`node --test tests/analytics.test.mjs`、`npm run build`。Qt 端运行 CMake/CTest 后，可使用 `../analytics-hadoop/smoke_qt.py` 进行真实 HTTP 检查，工具会在数据库临时副本上启动服务并在结束时关闭。
