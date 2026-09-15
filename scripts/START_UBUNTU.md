# Ubuntu 一键初始化与启动

在 Ubuntu 桌面终端执行，不要使用 sudo bash，也不要从无图形会话的 SSH 启动界面。

```bash
cd /home/zjs/MapForECarCharger
bash scripts/start_ubuntu.sh
```

首次检查、构建并安装 Vue npm 依赖；检查现有模拟库；已有 ADS 批次复用，缺失则只读导出 SQLite 并运行 Spark ODS→DWD→DWS→ADS；预测缺失时生成模拟演示预测；启动 Qt 后端、Flask、一个用户窗口、一个管理窗口和浏览器。业务数据库不会清空或重新生成。

仅检查依赖：

```bash
cd /home/zjs/MapForECarCharger
bash scripts/start_ubuntu.sh --check-only
```

缺少 Qt/Java/Python 依赖时自动安装（需要 sudo 和网络；PySpark 下载约 450 MB）：

```bash
bash scripts/start_ubuntu.sh --install-deps
```

Node/npm 必须预先安装。当前虚拟机已有 Node 24，无须重复安装；新 Ubuntu 可使用官方 Node 24 压缩包安装到用户目录，或安装发行渠道提供的 Node 20.19+、22.12+、24+ 和 npm。Hadoop 必须预先配置，脚本不自动下载 Hadoop、不格式化 NameNode。

更新业务历史统计，额外将快照保存到已有 HDFS：

```bash
bash scripts/start_ubuntu.sh --refresh-data --hdfs
```

默认 Spark local[2] 使用本地数仓；--hdfs 增加 HDFS 快照存储，不表示所有 Spark 分层均在 HDFS 计算。普通重复启动复用历史批次，刷新历史时使用 --refresh-data。

预期：终端最后输出 Ready，用户端和管理端各一个窗口，浏览器打开 http://127.0.0.1:5174/，顶部能切换七页。管理账号 admin / 123456，演示用户手机号 13900000000。运行中日志位于 runtime/desktop-launch/。首次数仓在本机此前约 2 分钟，加上构建和下载会更久。

```bash
curl -fsS http://127.0.0.1:5000/health
curl -fsS http://127.0.0.1:5174/api/v1/topics | python3 -m json.tool
ss -ltnp | grep -E ':9000|:9001|:5000|:5174'
```

预期接口返回 JSON，四个端口监听。用户端新建订单并完成支付，下一轮大屏刷新应出现业务变化；Spark 历史结果和预测不是实时重算。自动生成的 demo 预测不代表真实业务预测模型通过训练。

脚本重复运行只关闭自己 PID 文件记录且命令匹配的程序；遇到原手工启动服务占用端口会停止并报错，请在原终端 Ctrl+C 后再运行。任何阶段失败会显示日志路径，已启动服务保留用于排查。

指定其他项目、数据库或已训练预测：

```bash
export PROJECT_ROOT=/home/zjs/MapForECarCharger
export LAUNCH_DATABASE_PATH="$PROJECT_ROOT/server-qt/runtime/showcase-sim.db"
# 只有文件存在时才启用下行：
# export LAUNCH_ML_PATH="$PROJECT_ROOT/ml/outputs/forecast-live-test.json"
bash "$PROJECT_ROOT/scripts/start_ubuntu.sh"
```

地图配置仍从 /etc/map-for-ecar/server.env 读取，不输出 Key。脚本默认各服务仅监听本机，供 Ubuntu 完整测试。
