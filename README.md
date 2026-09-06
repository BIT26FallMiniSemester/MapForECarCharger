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

## 当前演示环境

```bash
set -a
. /etc/map-for-ecar/server.env
set +a
server-qt/build/charger-server --database server-qt/runtime/showcase.db \
  --host 127.0.0.1 --port 9000
```

`showcase.db` 包含 4 个明确标记为 `DEMO` 的可预约站点、12 个受管电桩，以及 2,614 条来自北京市公共数据开放平台的只读站点目录。公共目录缺少可信电价和单桩资料，因此不会伪造成可预约站点。

用户端使用 `13900000000` 体验新订单，`13600000000` 查看充电中订单，`13500000000` 查看待支付订单；管理端使用 `admin / admin123`。地址解析、附近路线距离和路线规划必须使用腾讯地图 WebService，Key 缺失或不可用时返回 `50301`，不会伪造直线距离。

用户端构建和完整流程见 `clients/user-qt/README.md`，管理端见 `clients/admin-qt/README.md`。
