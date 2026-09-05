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
