# 主机依赖与运行入口

主机Linux x86_64构建复用仓库内锁定源码，不在配置期下载；原vendor字节不改。
可启动模拟服务、显式初始化和退出步骤见 [Runtime说明](../../host/runtime/README.md)，
无凭据配置见 [simulator.example.json](simulator.example.json)。

| 依赖 | 版本 | 用途 | 许可 |
|---|---|---|---|
| SQLite | 3.54.1 | 业务仓储与独立模拟器journal | Public Domain |
| cJSON | 1.7.19 | 仓储校验、Runtime配置和CLI序列化 | MIT |
| CivetWeb | 1.16 | HTTPS/WS | MIT |
| libsodium | 1.0.22 | Argon2id、随机token和凭据清理 | ISC |
| OpenSSL | 3.5.9 | TLS与测试证书工具 | Apache-2.0 |

固定来源、不可变标识与逐文件摘要以 [dependencies.lock.json](dependencies.lock.json)
和 [network-sources.json](network-sources.json) 为准；许可原文在 [licenses/](licenses/)。
`python -B scripts/check_network_dependencies.py` 校验网络依赖输入。
SQLite/cJSON由locker_storage_thirdparty构建，网络依赖由host/http/third_party构建；
所有编译/配置/install输出在CMake build目录，源码目录保持只读。
CivetWeb的接收帧边界和原子停止标志由CMake生成构建副本，保留锁定vendor原件。

S1-06复用已验证的S1-05依赖，不升级库、不新增包，不因上游浮动分支变化换掉锁定输入。
POSIX时钟、线程、flock、termios均来自系统libc；主机模块没有HAL依赖。
开启LOCKER_BUILD_NETWORK后同时构建locker-service、locker-admin、已有locker-device-sim；
LOCKER_BUILD_TESTS开启正式回归。网络依赖按目标三元组核验：x86_64 为主机模拟，aarch64 为 RK3568（`deployment/target/aarch64-linux-gnu.cmake`，静态链接）。

运行配置两个模式：`SIMULATOR_ONLY`（对接 locker-device-sim 的 socket）与 `DEVICE_SERIAL`（对接 /dev/ttyS4 真实 UART）；默认样例loopback。私有socket、业务DB、模拟journal、测试密钥
都进入源码之外的runtime目录，不提交。业务服务与本机init共用同一个实例/维护锁，
不自动重启、不清库、不生成默认账号。局域网测试须显式绑定及正确部署证书信任。
固件锁 [deployment/firmware/dependencies.lock.json](../firmware/dependencies.lock.json)
独立维护，本卡不修改原HAL、固件库或设备profile。
