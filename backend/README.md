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

## 公共充电站数据

`app.seed --reset` 会同时导入北京市公共数据开放平台的 2,614 条社会公用充电站记录。
单独刷新数据可执行：

```bash
.venv/bin/python -m app.station_import
```

原始文件没有坐标、电价和设备编号，导入记录仅用于目录查询；配置电价和实际电桩后才可预约，补齐坐标后才会进入附近查询和推荐。数据来源和校验摘要见 `data/README.md`。

## 验证

```bash
.venv/bin/pytest
.venv/bin/python scripts/export_openapi.py
```

数据库文件、`.env`、日志和虚拟环境不得提交。
