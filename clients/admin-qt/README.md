# 充电林运营管理平台——PC 管理端

辛宏磊负责模块的可独立运行演示工程。项目基于 Qt 6/C++，面向 Ubuntu 22.04+，采用宽屏管理端布局。

## 演示账号

- 账号：`admin`
- 密码：`admin123`

## 已实现功能

- 管理员登录、错误提示和本地 Token 保存。
- 登录窗口采用安全的对象生命周期管理，登录后切换主窗口不会异常退出。
- 首页展示今日、本月、累计营收、订单数、累计充电量。
- Qt Charts 绘制近 7 日/30 日营收和订单趋势，并对缺失自然日补零。
- 电桩 `IDLE`、`RESERVED`、`CHARGING`、`FAULT`、`OFFLINE` 五状态分布。
- 电桩列表按站点、状态和编号筛选；详情展示累计数据、心跳和状态日志。
- 仅允许 `FAULT` 电桩执行模拟远程重启，操作前二次确认。
- 站点列表、站内电桩详情、新增和修改站点、新增唯一编号电桩。
- 用户列表、脱敏手机号模糊查询、详情、冻结和解冻。
- 成功、校验、权限/冲突类错误统一弹窗反馈。
- 电桩、站点和用户列表均支持上一页/下一页；真实接口模式按 `page/page_size` 请求。
- 登录页连接 Qt Socket 后端，默认地址为 `127.0.0.1:9000`。
- 统一解析 `{code,message,data,request_id}`，支持请求关联、分帧和业务错误码。
- 金额、电量、功率按接口约定在分/元、Wh/kWh、W/kW之间转换。

## Ubuntu 22.04+ 构建

安装 Qt 6 开发组件（不同镜像的软件包名称可能略有差异）：

```bash
sudo apt update
sudo apt install build-essential qmake6 qt6-base-dev qt6-charts-dev
```

在工程目录执行：

```bash
qmake6 ChargingAdmin.pro
make -j$(nproc)
./ChargingAdmin
```

也可以使用 Qt Creator 打开 `ChargingAdmin.pro`，选择 Qt 6 Desktop Kit 后构建运行。

## 联调说明

默认后端地址为 `127.0.0.1:9000`。客户端通过 4 字节大端长度头加 UTF-8 JSON 的长连接调用 `server-qt`，不使用 HTTP 或 Python 后端。演示前运行：

```bash
server-qt/build/charger-server --database runtime/demo.db --seed-demo --port 9000
```

管理端已连接登录、运营总览、营收趋势、电桩状态、电桩/站点/用户列表及管理操作等 Socket actions。

客户端不直接连接数据库，所有真实数据均应由后端接口提供。

更完整的字段、单位和错误码说明见 `docs/API_INTEGRATION.md`，测试场景见 `docs/TEST_CASES.md`。

## 演示建议

1. 用错误密码演示登录异常，再用演示账号登录。
2. 首页切换 7 日/30 日图表并说明统计口径。
3. 在电桩页筛选 `FAULT`，查看详情并执行重启。
4. 新增站点，再为选中的站点新增电桩并验证编号唯一性。
5. 搜索用户，查看消费摘要并演示冻结/解冻二次确认。
