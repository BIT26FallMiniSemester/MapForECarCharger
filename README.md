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

`showcase.db` 使用北京市公共数据开放平台的 2,614 条真实站点，保留名称、地址、运营商、行政区和备案快慢充接口数。初始化会按每个站点备案的快充/慢充接口数创建受管电桩，全部设为 `IDLE`，并统一使用 1.50 元/度的课程演示电价；原始接口数为 0 的 9 个站点各补 1 个课程演示桩。因此所有真实站点初始都可进入完整充电流程。这里的空闲状态和电价仅用于课程演示，不代表运营商实时数据。

用户端只保留测试用户 `13900000000`；管理端使用 `admin / admin123`。首页地图、地址解析、附近站点驾车距离和路线规划均通过 Qt 后端调用腾讯地图，Key 不进入客户端。管理端订单、用户计数及电桩状态每 2 秒刷新。

用户端构建和完整流程见 `clients/user-qt/README.md`，管理端见 `clients/admin-qt/README.md`。
