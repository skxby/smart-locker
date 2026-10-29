# S1-06 开发者验证

| 字段 | 当前事实 |
|---|---|
| 卡号 / 契约 | S1-06 / 0.2.1；前置S1-01至S1-05 DONE，基线426d04e |
| 结论 / 停止点 | DONE；active_task=null、recommended_next=S1-07；S1-07未开始，阶段1 gate仍NOT_RUN |
| 产出 | locker-service、locker-admin；显式本机首位管理员/单工具初始化、进程/维护锁、模拟装配、受限socket、健康搬运及运行说明 |
| 环境 | Windows %REPO%主副本；VMware Ubuntu x86_64，Linux7.3.0-rc5、GCC15.2.0、CMake4.2.3；Linux证据全部来自ssh locker-vm，无WSL |
| 最终输入 | final-20261028-03.tar.gz；SHA256 be5b0a736bf36ed6f6fb0a8e6e51774ec5771f130e2eb3905bc7d69b3211d439；3982文件逐项manifest验证；回传后代码/构建输入仍一致，收尾仅更新当前文档/状态 |
| 时间 | 最终三组验证在2026-10-28 21:37:41–21:47:52+08:00完成；2026-10-29恢复连接并回传证据，不把旧日志改日期 |
| 依赖 | 复用SQLite3.54.1、cJSON1.7.19、CivetWeb1.16、libsodium1.0.22、OpenSSL3.5.9；无新下载/新依赖/原vendor改写；调研及许可说明见host/runtime/README.md |
| 执行 | 主会话负责实现、契约、VM和验收；Sol角色子代理分别提供正式测试与只读风险咨询；不设开发B/独立审计门禁，咨询不称独立审计PASS |

## 验收与证据

| 要求 | 实际结果 / 证据 |
|---|---|
| 空目录 / 初始化 | 服务不自动建库/建管理员；init显式输入用户名、工具名和密码，管理员+用户名索引+工具同事务；真实SQLite核对只有一个管理员和一个slot1工具 |
| 密码 | Argon2id；密码不在argv/环境或日志；受限stdin拒绝公开普通文件和公开命名FIFO；PTY实测关闭回显、成功后恢复、SIGINT取消后≤3秒恢复且不建库 |
| 单实例 / 离线互斥 | service/init共用instance.lock，取得锁后才开业务库、DB关闭后才释放；测试init等待输入时服务被拒，服务运行时第二服务/init被拒；锁inode不删除 |
| 文件 / 错误路径 | 0700状态目录、0600业务库/socket；重复init、已有孤立-wal/-shm/-journal、错误/重复键/嵌入NUL配置、端口占用、损坏DB均明确非零退出；已有状态字节保留 |
| 真实运行 | 正式三个可执行程序，独立sim journal、AF_UNIX字节流、受信TLS真实登录；服务两轮启动/退出/重启后原账号/工具保留；服务与模拟器正常退出清理自己的socket |
| 核对 / 来源 | API在未完成设备维护核对时保持RECONCILING；工具不可用；公开Device与持久device_state的simulated=true，关闭CR-S104-1及启动READY误报 |
| 健康 / 实例 | 透明代理按uart-layouts验证CRC/字段；HELLO和HOST_HEALTH同host/session，UI零UUID/年龄UINT32_MAX、监督UNKNOWN；正常健康发送频率无双发送者 |
| 真实Core停滞 | SQLite BEGIN IMMEDIATE持写锁，同时真实HTTPS登录使Core写事务阻塞；线上的progress保持149，age依次1008/2011/3013/4016ms；解锁后progress恢复。Runtime继续搬运不会伪造新鲜度 |
| 资源上限 | 正式装配固定Core64、HTTP4、WS16，配置不提供增大选项；Core容量/满队列、publisher16环及慢WS回归沿用原正式用例。新增生命周期测试只用日志确认配置值，不把日志当压力测试 |
| 退出 | 先停HTTP/IPC，再关闭设备IO/回调，停止Core，销毁依赖、关DB后释放锁；stop未成功不释放依赖；真实重启与失败启动均验证资源清理 |

## 命令与真实结果

复现入口：`python -B docs/evidence/stage-01/S1-06/verify-vm.py --run <新批次> --sanitizers`。
该脚本传固定包并验证manifest，在VM新批次src之外生成三套build及logs。

| 命令 / 配置 | 退出码 / 实际结果 |
|---|---|
| cmake -S src -B build-debug -DCMAKE_BUILD_TYPE=Debug -DLOCKER_BUILD_TESTS=ON -DLOCKER_BUILD_NETWORK=ON -DLOCKER_WARNINGS_AS_ERRORS=ON；cmake --build build-debug -j2；ctest --test-dir build-debug --output-on-failure | 配置0、构建0、CTest0；35/35，48.43秒 |
| 同上，CMAKE_C_FLAGS=-fsanitize=address,undefined -fno-omit-frame-pointer -g，build-asan | 配置0、构建0、CTest0；35/35，51.91秒 |
| 同上，CMAKE_C_FLAGS=-fsanitize=thread -fno-omit-frame-pointer -g，build-tsan | 配置0、构建0、CTest0；35/35，55.99秒 |
| scripts/check_contracts.py、check_stage_plan.py、check_vision_contracts.py、check_network_dependencies.py、check_feasibility.py | 收尾全部0；仅设计/依赖一致性，不当作硬件证据 |
| git -c core.whitespace=cr-at-eol diff --cached --check | 0；保留已跟踪文件原换行格式，新增源码LF |

原始配置/构建/CTest及环境日志在[vm-final-03](vm-final-03/)。
[Debug完整测试输出](vm-final-03/export/debug-lasttest.log)、
[ASan/UBSan输出](vm-final-03/export/asan-lasttest.log)、
[TSan输出](vm-final-03/export/tsan-lasttest.log)保留成功路径、健康年龄和PTY真实观察。
[source-manifest.json](vm-final-03/export/source-manifest.json)绑定构建输入。
VM路径为`/home/%USER%/locker-workspace/S1-06/batches/final-20261028-03`及
`/home/%USER%/locker-workspace/logs/S1-06/final-20261028-03`。

## 改动、残留与限制

| 项 | 说明 |
|---|---|
| 实现 / 正式测试 | host/runtime/*、host/cli/*、tests/integration/runtime/*、根CMake、tests/unit/domain/test_domain_state.c |
| 必要共享适配 | CR-S106.md：契约semantics/生成器/生成头统一增加Core装配标志，domain_device发送门及domain_state/domain_import来源/核对投影；内部C调用方须重编，JSON/UART版本不变 |
| 文档 / 配置 | 当前README、AGENTS、阶段索引/入口、执行规则、VM流程、internal-interfaces、模块README、deployment/host配置与依赖说明同步 |
| 残留 | 正式运行标识、回归fixture、verify-vm.py和诊断日志保留；未添加公开调试路由、临时mock/开关、生产默认凭据或私钥；包/build在忽略目录，VM无本卡遗留服务进程 |
| 既有改动 | 原硬件文档、固件env/Keil及其证据原样保留，未纳入本卡提交；不擅自清理uvoptx等非本轮文件 |
| 平台边界 | CLI初始密码仅12–128可打印ASCII字节；当前HTTPS绑定仅IPv4，同UID私有IPC；异常退出残留socket须先确认旧进程结束再本机处理；详见debt.md和运行说明 |
| 未验证 | 真实UART/GPIO/MCU/锁硬截止、RK ABI、Flash/littlefs或RK介质掉电、真实UI/监督/相机/RKNN、整柜借还；不新增材料费用，不改UNVERIFIED硬件profile |

S1-07跨模块完整故障矩阵未执行。本卡通过不等于阶段1 PASS或硬件功能完成。
