# MapForECarCharger

电动汽车充电管理课程项目，由用户端 Qt、PC 管理端 Qt、Qt/C++ Socket 后端和 SQLite 数据库组成。

新版后端位于 `server-qt/`，基于 C++17、Qt 6、CMake、QTcpServer、Qt SQL 和 QNetworkAccessManager。两个客户端通过“4 字节大端长度头 + UTF-8 JSON”的 TCP 协议调用后端，业务操作使用 `action` 标识。通信契约见 `contracts/socket-protocol.md`。

核心演示流程为：登录或注册、模拟充值、查找站点、创建订单、预约、开始充电、动态查看电量和金额、结束充电、余额付款，以及管理端统计核对。

`backend/` 保存原 Python/FastAPI 实现，供迁移核对和旧数据转换使用；它不是新版客户端的运行依赖。`contracts/openapi.yaml` 也只描述旧 HTTP 服务。

## Qt 后端构建

```bash
cmake -S server-qt -B server-qt/build -DBUILD_TESTING=ON
cmake --build server-qt/build -j
ctest --test-dir server-qt/build --output-on-failure
```

运行、数据库迁移和旧库导入说明见 `server-qt/README.md`。密钥、数据库、头像、日志和构建产物不得进入 Git。

## 一键准备演示数据

```bash
server-qt/build/charger-server --database runtime/demo.db --seed-demo --host 0.0.0.0 --port 9000
```

`--seed-demo` 可重复执行，会创建 4 个北京站点、12 个电桩、历史和进行中订单、充值记录及运营统计。用户端使用 `13900000000` 登录；管理端使用 `admin / admin123`。附近站点在未配置腾讯地图 Key 时由 Qt 后端本地计算距离，完整演示无需 Python 或外部地图服务。
