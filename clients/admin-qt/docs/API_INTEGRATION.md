# PC 管理端联调说明

## 后端基线

- 默认地址：`http://127.0.0.1:8000/api/v1`
- 请求格式：HTTP/REST + JSON
- 认证方式：`Authorization: Bearer <access_token>`
- 管理员登录：`POST /admin/login`
- 客户端同时检查 HTTP 状态码和响应字段 `code`

登录页可选择“使用内置演示数据”。取消勾选后填写后端地址，即进入真实接口模式。Token 使用 `QSettings` 统一保存，密码和 Token 不写入日志。

## 已接入接口

| 页面 | 方法与路径 | 用途 |
|---|---|---|
| 登录 | `POST /admin/login` | 管理员认证和 Token |
| 首页 | `GET /admin/revenue?days=7/30` | 营收指标与趋势 |
| 首页 | `GET /dashboard/pile-status` | 五种电桩状态全量聚合 |
| 电桩 | `GET /admin/piles` | 站点、状态、编号筛选和分页 |
| 电桩 | `GET /admin/piles/{id}` | 累计数据、心跳和状态日志 |
| 电桩 | `POST /admin/piles/{id}/restart` | 故障桩重启 |
| 站点 | `GET /admin/stations` | 站点统计分页列表 |
| 站点 | `POST /admin/stations` | 新增站点 |
| 站点 | `PUT /admin/stations/{id}` | 修改站点 |
| 站点 | `POST /admin/stations/{id}/piles` | 新增电桩 |
| 用户 | `GET /admin/users` | 手机号模糊查询和分页 |
| 用户 | `GET /admin/users/{id}` | 用户聚合详情 |
| 用户 | `POST /admin/users/{id}/freeze` | 冻结 |
| 用户 | `POST /admin/users/{id}/unfreeze` | 解冻 |

## 单位转换

- `_cents`：界面除以 100 显示为元。
- `_wh`：界面除以 1000 显示为 kWh。
- `_w`：界面除以 1000 显示为 kW。
- `_seconds`：详情中换算为小时。
- ISO 8601 UTC 时间原样接收，后续可按展示要求转为本地时间。

## 错误处理

客户端已映射参数错误、认证失败、权限不足、记录不存在、电桩状态冲突、活动订单阻止冻结、编号重复以及服务不可用等错误。冻结、重启等危险操作会先二次确认，最终业务约束仍以后端返回为准。

## 大规模站点数据边界

`stations_database.json` 中的 2,614 个目录站点和 32,247 个模拟电桩应由后端导入数据库。PC 客户端只通过分页接口读取，不把全量数据编译进程序，也不直接连接数据库。
