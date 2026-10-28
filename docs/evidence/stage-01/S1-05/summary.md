# S1-05 开发者验证

| 字段 | 当前事实 |
|---|---|
| 卡号 / 契约 | S1-05 / 0.2.1 |
| 状态 | DONE；最新19固定输入完成开发者验证，S1-06未开始 |
| 前置 | S1-04 DONE，848bea3；复用既有仓储/设备适配器/正式模拟器 |
| 执行 | 当前会话独立实现、开发者验证、整改和集成；无独立审计AI |
| 环境 | Windows %REPO%源码主副本；仅ssh locker-vm的VMware Ubuntu x86_64提供Linux证据；不用WSL |
| 输入快照 | credential-final-20261028-19.tar.gz；SHA256 d2438a48f1c758a510e2c623d6e053668eb9e48e45e1320c30857946e540ad23；3968个文件逐项manifest核对 |
| 产出 | 本卡22个HTTPS/WS入口、23个service.sock方法、Core认证/用户/卡管理、持久化历史分页、变化提示发布器；视觉两入口仍归S2-14 |
| 边界 | HTTP/IPC不访问SQL、不写GPIO；普通HTTPS无presence/维护/模拟或任意命令；无生产默认密码和公网注册 |
| 依赖 | CivetWeb1.16 MIT、libsodium1.0.22 ISC、OpenSSL3.5.9 Apache-2.0；3811个锁定vendor输入，配置不下载 |
| 集成变更 | CR-S105-1补Core占位；CR-S105-2为本机两个既有端口增加可选receipt及请求/幂等ID，真源/生成头/调用方一并更新；内部C调用方须重编，JSON公开版本不变 |

## 实现与验收对应

| 项目 | 实现与证据 |
|---|---|
| 认证 / 角色 | 真实Argon2id，用户创建幂等密码指纹亦使用Argon2id防止快速猜测旁路；随机token仅存SHA256；Core再次检查当前账号/角色/停用/过期/撤销；重新启用不恢复旧token；最后启用管理员不可降权/停用；测试真实TLS与SQLite |
| 事务 / 幂等 | 用户唯一索引、管理审计、会话撤销、卡绑定/唯一索引与幂等响应同事务；到柜证明/维护审计及接受响应先落库，随后才武装/恢复；重放原结果不重复脉冲，异意图409 |
| 资源 / 格式 | HTTP4096字节、IPC8192字节、IPC响应65535字节；拒绝截断/NUL/重复键/未知字段/非法UTF8；HTTP/IPC/Core共用命令DTO验证；Unicode转义及合法代理对支持 |
| 分页 / 隐私 | limit1–100、默认20；游标绑定revision；完整记录按响应预算分页。历史读取持久化loans、设备事实与人工审计；普通用户的操作、借用、快照和事件按归属过滤 |
| IPC | SO_PEERCRED、socket所有者/inode/权限及当前用户会话联合核验；管理员还核allowed_admin_uid；实际测试拒绝UID、文件权限、伪造来源、坏包及跨socket监督方法 |
| 刷卡 | 当前session/boot/epoch、未应用序号、MCU时间锚及来源年龄；绑定30秒，到柜15秒；只关联观察当时的pending operation。历史、重复或无可信锚不产生新证明 |
| WS | 首次连接及变化/丢失均提示重新查询；独立发送线程，Core发布try-lock且不做网络IO；慢读/突发不阻塞Core；客户端只准掩码、FIN、无RSV且≤125字节的控制帧，超大数据帧在分配前关闭 |
| 停机 | 超时返回BUSY保留存活对象，destroy等待线程退出；先停HTTP/IPC再停Core/publisher；运行时测试证书/密钥退出清理 |
| 固定上限 | 用户100、每账号会话16；登录清理本账号失效会话，来源/账号限流有界，保证撤销在32条mutation事务预算内 |

## 实际命令与结果

| 命令 / 输入 | 退出码 / 结果 |
|---|---|
| python -B docs/evidence/stage-01/S1-05/verify-vm.py --run credential-final-20261028-19 --sanitizers | 0；3968文件核验，Debug/ASan+UBSan/TSan配置、严格构建、CTest全部0；各33/33，35.92/39.26/40.62秒 |
| final-16 Debug配置、严格构建、全量CTest | 0 / 0 / 0；33/33，35.54秒 |
| final-16 ASan+UBSan配置、构建、全量CTest | 0 / 0 / 0；33/33，38.72秒 |
| final-16 TSan配置、构建、全量CTest | 配置/构建0，脚本退出1；31/33，两个网络用例报告mg_stop/worker的volatile stop_flag竞争，阻塞交付 |
| stop-race-fix-17 Debug / ASan+UBSan / TSan | 全部配置/构建/CTest退出0，各33/33；35.79/38.65/40.86秒，竞态关闭，无屏蔽 |
| final-boundaries-18 Debug / ASan+UBSan / TSan | 全部退出0，各33/33；35.84/38.63/40.40秒；后续凭据整改由19替代 |
| verify-matrix.py 对credential-final-19 Debug / ASan / TSan fixture | 各退出0，54/54逐路由角色及RPC资源负例；脚本SHA另记external-test-sha256.log |
| check_contracts / check_stage_plan / check_vision_contracts / check_feasibility / check_network_dependencies | 全部退出0；只证明对应静态范围，不证明硬件 |

正式回归走真实HTTPS→Core/SQLite→设备适配器/模拟器：创建借还、IPC到柜/实时卡证明、模拟单次脉冲、门/物品事实、本人确认、关闭借用保留历史；重放不加脉冲、他人404、提前确认409、管理员边界、Unicode用户名显示、账号重新启用后旧token仍拒绝。额外54项矩阵逐一检查匿名/未知token和普通用户访问管理员入口及WS握手。模拟脉冲不是硬件定时关断证据。

## 失败、文件与交接

| 字段 | 当前事实 |
|---|---|
| 中间失败 | users-02版本数字类型不符；ipc-04权限拒绝表现为RST；history-06旧测试错误地期待归还后无历史；contract-cards-09误清peek/连接BrokenPipe/卡来源枚举及释放等待；transport-final-11守卫锚计数；13/14磁盘耗尽导致未形成有效验证。全部保留旧日志，不移植旧PASS |
| 最终整改 | 修复正确类型、坏连接处理、历史预期、只清pop槽、CARD来源/原pending窗口与新鲜度、构建期帧守卫；16检出CivetWeb volatile停止标志竞争，17用C11原子标志关闭，无屏蔽；18补齐IPC资源404；19消除凭据幂等快速哈希旁路。最终输入不复用旧二进制 |
| 文件清单 | CMakeLists、contracts真源/生成头、scripts/build_contracts.py及依赖校验；host/common队列、host/domain认证/管理/历史/本机端口、host/http、host/ipc；正式单测/网络契约/集成fixture；模块README、内部接口、状态入口及本卡证据。精确输入见snapshot-manifest.json，变更见本卡git diff |
| 临时残留 | 无调试输出/生产凭据/测试私钥入源码；旧编译ELF/静态库及失败上传残留已清理，源码和日志保留；锁定发布包的configure/Makefile.in为正式输入，不能当临时文件删除 |
| 磁盘 | 用户授权扩大清理；VM清理后可用12.60GiB，使用率78%；历史包无损压缩并核对原始字节SHA；见[磁盘清理记录](../../environment-disk-cleanup-2026-10-28/summary.md)；最终三套构建后df可用8.4GiB、使用率86% |
| 未验证 | RK3568 ABI、手机/真实UI、UART/锁/相机与掉电；hardware profile仍UNVERIFIED。S1-06装配与初始化、S1-07阶段门禁未开始 |
| 耗时 / 轮数 | 本轮19个固定快照尝试（含2次环境失败、1次配置失败）；因实际竞态及凭据缺陷整改，sanitizer随新输入复验，旧PASS不移植。可证开发窗口2026-10-28 17:55:32至21:13:58+08:00，3小时18分26秒（含构建、环境清理及负控）；先前会话累计工时不虚报 |
| 下一卡 | active_task=null、recommended_next=S1-06；本卡本地提交，不执行S1-06、不push |

凭据负控：将最新仓储断言链接到18的旧实现，真实TLS用例以预期断言失败退出1；最新19相同断言通过。日志credential-final-20261028-19/credential-negative-control.log末行记录NEGATIVE_CONTROL PASS。负控失败是验证测试能检出缺陷，不是最终实现失败。
