# S1-05实施设计

当前卡DONE，契约0.2.1，前置提交848bea3。单会话开发和验证，不启动审计AI。源码Windows主副本，VMware locker-vm固定指纹快照构建；不使用WSL、不改变现有STM32接线。

## 复用与边界

复用锁定CivetWeb1.16（MIT）、libsodium1.0.22（ISC）、既有SQLite/cJSON与Core队列。已对CivetWeb和libsodium上游归档实算SHA，与依赖锁一致。libsodium归档包含configure，不要求在离线VM安装autotools。

TLS比较：锁中OpenSSL4.0.3是候选而非强制；采用OpenSSL3.5.9 LTS，官方支持至2030年，4.0支持至2027年，3.5对CivetWeb1.16更易保持API兼容。核对官方归档SHA并锁定tag/commit后，从源码在build外置目录编译；不使用VM现有较旧的3.5.5，不关闭证书校验。上游维护/补丁与CivetWeb兼容性通过真实构建和TLS负例核查，不把资料阅读当运行通过。

来源：https://github.com/civetweb/civetweb/tree/v1.16 、https://doc.libsodium.org/password_hashing/default_phf 、https://openssl-library.org/source/ 。不引入新的HTTP栈、密码算法或数据库。

## 集成变更与所有权

CR-S105-1：S1-04按计划保留了SESSION_CREATE/REVOKE/CURRENT、用户/卡管理和locker_core_authenticate占位；若HTTP/IPC直接补数据库写入会违反唯一写者边界。本卡由当前唯一集成负责人统一补Core端处理器及正式回归；REST/RPC方法与仓储schema保持既有真源；内部C端口的关联结果扩展由同一集成负责人维护真源和全部调用方，不新增同义公开API。

登录/会话和管理变更在Core线程读写仓储。HTTP/IPC不能SQL或直接repo_commit。Argon2id使用libsodium；只存token哈希，命令执行再次验证会话/用户/角色。账号和来源的失败限速有界，凭据不记录日志。用户唯一索引、最后管理员约束、停用撤销会话、卡绑定与审计同事务。

HTTP与IPC共用有界JSON与命令DTO校验，分别维护传输适配，最终只暴露现有http/ipc/events生命周期端口。CivetWeb仅TLS监听，4个worker、4096字节body上限；严格字段/重复键/UTF-8/分页校验，路由白名单，普通HTTPS无presence、模拟或直接开锁。

service.sock使用SOCK_SEQPACKET，8192字节请求、65535字节响应；固定包络，无BOM/NUL/重复键/多对象。核SO_PEERCRED与配置UID、socket/父目录权限；可信peer仍需当前登录会话，JSON中source/actor不可赋权。退出只移除自己创建的socket。

WS握手头Bearer，不接受URL token或命令；最近16条共享环形失效提示和每消费者游标，Core发布只复制/try-lock，慢连接断开或resync，不等待网络。销毁前停止HTTP/IPC、WS和所有工作线程，再释放借用的Core/publisher。

## 验证计划

真实Core+SQLite+模拟器之上测试HTTPS和SEQPACKET，不用固定成功替身代替验收。测试证书/密钥在VM运行目录生成，不进库。覆盖证书不信任、未知/过期/停用/越权、登录限速、重复幂等与异正文、超大body/分页、IPC UID/权限/包络负例、WS慢消费者和正常退出。

普通Debug全量先行；收尾ASan/UBSan与涉及线程的TSan；同步当前文档、清临时残留，真实PASS才DONE，本地commit不push。两项S1-04非阻塞投影债务由集成负责人检查，Device.mode按Core当前阶段投影；simulated来源按S1-06之前的既有交接处理。

CR-S105-2：本机两个端口增加可选receipt输出，Presence/Maintenance原生DTO增加请求与幂等ID。非NULL输出由Core完成槽返回真实接受/拒绝及幂等结果；NULL保留既有进程内异步用法。证明/维护审计与接受结果先在单一事务提交，随后才武装/恢复就绪，不以QUEUED冒充200。

资源约束：用户最多100，账号最多16条会话；登录清理失效会话，停用/角色变化与会话撤销、审计、幂等结果同事务完成。历史分页按完整响应预算，revision改变使旧游标失效。刷卡必须当前会话、未应用序号、可信MCU时间锚和原到柜窗口；卡只关联观察时的待办操作，不能用于后建操作。

CivetWeb集成守卫：原vendor字节不改，构建副本限制客户端帧负载；TSan揭示1.16默认volatile stop_flag并非线程同步，构建副本将其改为C11 _Atomic int，读/写使用顺序一致原子语义。每个固定锚点只允许一次命中；升级不匹配时配置失败，不静默放弃保护。不添加TSan屏蔽。

用户创建幂等指纹中的密码字段使用以幂等UUID为盐的Argon2id派生结果，随后才参与规范请求摘要；失败不得回退为原文快速哈希。账户password_hash仍独立使用随机盐Argon2id字符串。这样既能比较同键异密码意图，也不为数据库离线猜测提供快速校验旁路；真实仓储回归检查不存在原始密码请求的直接SHA指纹。
