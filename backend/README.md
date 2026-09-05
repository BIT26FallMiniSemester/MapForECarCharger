# Backend

FastAPI 后端是业务数据的唯一写入入口，使用 SQLAlchemy 2.x、Alembic 和 SQLite。

## 本地运行

```bash
cd backend
python3 -m venv .venv
.venv/bin/pip install -e '.[dev]'
.venv/bin/alembic upgrade head
.venv/bin/python -m app.seed --reset
.venv/bin/uvicorn app.main:app --reload
```

Swagger: `http://127.0.0.1:8000/docs`，API 前缀：`/api/v1`。

部署者应从 `.env.example` 创建不进入 Git 的 `backend/.env`，配置 `TENCENT_MAP_KEY`。地图 Key 只由后端读取，不得放入 Qt 客户端、URL 日志或仓库。开发环境默认允许跨域联调；生产环境必须限制 `CORS_ORIGINS` 并替换默认密钥。

预约默认保留 15 分钟，可通过 `RESERVATION_TIMEOUT_SECONDS` 调整。用户再次查询或操作订单时会清理自己的过期预约。

附近站点和路线统一由后端调用腾讯地图 WebService。当前使用地址解析、驾车距离矩阵以及驾车/步行路线规划接口；腾讯地图不可用时返回明确的 `50301`，不会回退为本地直线距离。

## 充电计算

本轮不依赖设备遥测。订单处于 `CHARGING` 时，后端根据服务器时间、额定功率和订单电价快照动态计算展示值；结束充电时保存最终时长、电量和金额。

## 公共充电站数据

`app.seed --reset` 会同时导入北京市公共数据开放平台的 2,614 条社会公用充电站记录。
单独刷新数据可执行：

```bash
.venv/bin/python -m app.station_import
```

默认导入 `data/processed/beijing_public_charging_stations.json`。该文件在原始公共目录上补齐了经纬度；原始数据仍没有电价或单个设备编号。附近查询只向腾讯地图提交已经配置电价并接入实际电桩的站点，避免把不可预约目录记录作为业务站点返回。导入器不会把快慢充接口汇总数展开为虚构电桩。

## 验证

```bash
.venv/bin/ruff check app tests scripts migrations/versions
.venv/bin/pytest
.venv/bin/python scripts/export_openapi.py
```

数据库文件、`.env`、日志和虚拟环境不得提交。
