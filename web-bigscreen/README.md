# Web 大数据可视化大屏 Demo

本目录是电动汽车充电综合管理平台的 Vue 大屏 demo，使用 Vue 3 + Vite + ECharts + Axios 开发。

## 接口统一说明

本项目已按课程文档中的接口和数据库约定调整：

- 后端基础路径：`/api/v1`
- JSON 字段：`snake_case`
- 金额：分，字段后缀 `_cents`
- 电量：Wh，字段后缀 `_wh`
- 功率：W，字段后缀 `_w`
- 时间：ISO 8601 UTC
- 电桩状态：`IDLE`、`RESERVED`、`CHARGING`、`FAULT`、`OFFLINE`
- 大屏只读后端接口，不直接操作数据库

页面显示时会把分换算为元、Wh 换算为 kWh、W 换算为 kW。

## 本地运行

```bash
cd D:\Code\vue\MiniSemester
npm install
npm run dev
```

浏览器访问：

```text
http://localhost:5173
```

## 打包部署

```bash
npm run build
```

生成的 `dist` 目录可以复制到 Ubuntu 中测试。Ubuntu 上可以进入 `dist` 后执行：

```bash
python -m SimpleHTTPServer 8080
```

然后访问：

```text
http://localhost:8080
```

## 接入真实后端

当前默认使用 mock 数据。后端接口完成后，修改 `src/api/dashboard.js`：

```js
export const USE_MOCK = false
```

并确认后端地址：

```js
baseURL: 'http://localhost:8000/api/v1'
```

如果后端不是 8000 端口，请改成实际地址。

## 当前对齐的接口

- `GET /api/v1/dashboard/overview`
- `GET /api/v1/dashboard/revenue-trend?days=30`
- `GET /api/v1/dashboard/pile-status`
- `GET /api/v1/dashboard/station-ranking?metric=revenue&days=30&limit=10`
- `GET /api/v1/dashboard/realtime-orders`
- `GET /api/v1/predictions/load?horizon_hours=6`

其中 `realtime-orders` 是文档中的 P1 接口；如果后端暂时未实现，可以继续使用 mock 数据。

## 已包含页面模块

- 核心营收指标
- 营收与订单趋势图
- 电桩状态分布图
- 站点营收排行
- 平台设备在线率与站点利用率
- 实时充电订单
- 机器学习负荷预测趋势
- 高峰预警与异常设备提醒
