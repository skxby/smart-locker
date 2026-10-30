# 主机模拟运行入口（S1-06）

`locker-service` 装配已有 Core、仓储、设备适配器、HTTPS/WS 和本机 IPC；
`locker-admin init` 显式创建首位管理员与单工具。配置接受 `SIMULATOR_ONLY`（模拟器 socket）和 `DEVICE_SERIAL`（/dev 下的真实 UART），由 `device_path` 决定后端。
模拟器拥有独立 SQLite journal，业务服务只打开 `state_dir/business.db`。
S2-06 起本模块可指向真实 UART；GPIO/HAL/UI/监督进程仍不在本模块。阶段一门禁仍由 S1-07 执行。

## 构建和空目录运行

在 VMware Ubuntu（`ssh locker-vm`）的本地 Linux 文件系统运行。
源码快照与 build/runtime/logs 分开；不要在共享目录放数据库或 socket。

```bash
src="$HOME/locker-workspace/S1-06/batches/<已验证批次>/src"
build="$HOME/locker-workspace/build/S1-06/manual"
cmake -S "$src" -B "$build" -DCMAKE_BUILD_TYPE=Debug \
  -DLOCKER_BUILD_NETWORK=ON -DLOCKER_BUILD_TESTS=ON -DLOCKER_WARNINGS_AS_ERRORS=ON
cmake --build "$build" -j2
ctest --test-dir "$build" --output-on-failure
work="$HOME/locker-workspace/runtime"
mkdir -p "$work/simulator" "$work/tls"
chmod 700 "$work/simulator" "$work/tls"
```

从 `deployment/host/simulator.example.json` 复制配置到工作区 runtime 中；
替换 USER、全部绝对路径、`ui_uid` 和 `admin_uid`（用 `id -u` 取得运行用户 UID）。
配置必须包含样例中全部11个字段，不接受重复键、未知字段、相对路径或硬件模式。
`state_dir` 少于80字节，须为无符号链接/无 `..`/无末尾斜杠的规范绝对路径；
init 可创建最后一层目录，父目录须已存在。现有状态目录须由当前UID所有且权限0700。
HTTP固定4工作线程、Core队列64、WS提示环16，配置不提供放大这些上限的选项。

TLS需提供与访问主机名/IP匹配、由客户端信任的证书及对应私钥。
私钥文件设0600。仅作本机测试时可在 runtime/tls 内生成一次性测试证书：

```bash
umask 077
"$build/network-dependencies/bin/openssl" req -x509 -newkey rsa:2048 -nodes \
  -config /dev/null -keyout "$work/tls/server.key" -out "$work/tls/server.crt" \
  -days 1 -subj /CN=localhost -addext subjectAltName=DNS:localhost,IP:127.0.0.1
config="$work/simulator.json"
"$build/host/cli/locker-admin" init --config "$config" \
  --username cabinetadmin --tool-name '柜内固定工具'
```

管理员名与工具名均由调用者显式提供。管理员名3–32个字母/数字/`_.-`，
工具名最多64字节。密码在控制终端关闭回显后读取，限12–128个可打印ASCII字节；
Ctrl+C/终止信号取消读取并恢复回显。无控制终端时须显式加 `--password-stdin`，
stdin只接受匿名管道，或本人所有、无额外硬链接且0600的命名管道/普通文件；
不要把口令写进 shell 命令行、环境变量或日志。测试凭据、证书私钥不入库。

初始化先持有 `state_dir/instance.lock`，再独占创建业务库，最后将管理员、用户名索引和
工具在同一仓储事务内提交。已有主库或任一 SQLite sidecar 都拒绝初始化。
失败保留文件供诊断，不自动清库、续写部分初始化或重置密码。
营业服务不创建管理员或空数据库；缺初始化/损坏/权限错误均明确失败。

## 启动、退出与重启

先在一个终端启动独立模拟器，再在另一个终端启动服务：

```bash
"$build/host/simulator/locker-device-sim" \
  --uart "$work/simulator/uart.sock" --control "$work/simulator/control.sock" \
  --journal "$work/simulator/device.db"
"$build/host/runtime/locker-service" --config "$config"
```

两程序都用 Ctrl+C 或 SIGTERM 正常退出。先停服务，再停模拟器；
服务停止HTTPS/IPC、关闭设备IO和回调、停止Core、销毁依赖、关闭DB，最后释放锁。
stop超时继续等待，不能释放仍有使用者的对象。锁文件保留原inode，不能在运行中删除；
后续离线维护工具须使用同一锁，在取得它之后打开业务库、关闭库之后释放它。
服务或init持锁时第二实例明确失败。service.sock由IPC仅删除自己创建的inode；
异常退出留下socket时需先确认旧进程结束，再由本机操作员处置，不自动覆盖。

服务每次生成一个新host_instance_id，共用于Core、HELLO和HOST_HEALTH。
启动及重启先核对；设备尚无当前会话/快照或尚未完成maintenance核对时，公开mode保持
RECONCILING，工具不可用。HTTPS开始监听不代表允许借还；管理员仍须通过已有受限本机
IPC和当前会话执行维护核对。未知旧动作不自动重发。

正式服务设置 `runtime_health_owned=true`，由主循环每秒搬运Core不可变源样本，
相同计数的再次读取不改变原采样时刻。Core阻塞时年龄持续增长；UI实例零UUID、
UI年龄UINT32_MAX、监督UNKNOWN。未装配Runtime的旧库级测试仍可使用Core内置健康发送；
正式服务关闭该发送路径，避免两个发送者的计数竞争。
`device_simulated=true` 同时用于公开投影与持久化设备状态，不再把模拟来源写为false。

## 网络和失败边界

样例仅绑定127.0.0.1。局域网人工测试必须显式修改bind_address（当前仅IPv4）并正确部署
证书信任；不能关闭TLS校验。无HTTP模拟、到柜证明、直接开锁或任意进程命令路由。
模拟控制入口只在0600私有socket，说明见 `host/simulator/README.md`。
IPC还要求允许的peer UID与当前用户会话；0700状态目录/0600 socket要求同UID访问，
跨UID组权限部署不属于本卡样例。

参数/配置错误退出2，运行/初始化失败退出1，正常退出0；错误输出不回显配置正文或口令。
端口占用、证书错误、模拟器不可连接、业务库错误或socket路径占用均失败退出。
连接运行中断后保持不可新操作，不做自动重启；后续现场重连/部署能力按对应卡验收。
Linux x86_64证据不能证明RK ABI、真实锁硬截止、掉电安全或整柜可用。

## 复用依据

本卡复用锁定的CivetWeb1.16(MIT)、SQLite3.54.1(Public Domain)、cJSON1.7.19(MIT)、
libsodium1.0.22(ISC)、OpenSSL3.5.9(Apache-2.0)，不新增依赖或修改vendor。
[CivetWeb上游装配示例](https://github.com/civetweb/civetweb/blob/master/docs/Embedding.md)
提供start/stop和工作线程选项；本项目已有适配器，适配成本小于另换网络栈。
进程互斥使用Linux flock与固定inode，不自制PID文件锁；不上自动重启服务管理器。
上游示例的默认文件服务不符合本项目边界，继续使用已受限的现有REST/WS适配。
许可、固定源码和安全修补副本规则以 `deployment/host/dependencies.lock.json`、
`network-sources.json`、`scripts/check_network_dependencies.py` 为准；不据浮动上游代码升级依赖。
