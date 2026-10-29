# 本机业务 IPC（S1-05）

`locker_ipc_start` 创建 AF_UNIX/SOCK_SEQPACKET 的 service.sock，借用 Core。父目录须由服务 UID 所有且不允许 group/other 写入；socket 只允许 0600/0660，不覆盖已有路径。每个连接同时核对 SO_PEERCRED 中的 UID、当前路径 inode/所有者/权限以及 Core 重新验证的用户会话。管理员方法还检查 allowed_admin_uid 与 ADMIN 角色。

一个连接处理一个完整 JSON 包，最多 8192 字节；recvmsg 检查 MSG_TRUNC，拒绝 BOM、NUL、重复键、额外字段及无法可信关联的请求。响应最多 65535 字节，204 为 null，嵌套 request_id 与本次包络一致。业务命令等待内部随机 submission_token，不按客户端 request_id 等待。登录按允许的 peer UID 限流，每分钟最多5次；不信任客户端提供的 source/actor_id/principal。

普通方法对应公开 API 的固定方法表；presence.submit 和 maintenance.reconcile 额外构造经过内核验证的 peer。私有请求携带幂等键，Core 将接受结果与证明/维护审计同事务持久化，再进行武装或恢复就绪工作。200 不代表门已打开或设备恢复已完成。监督方法、WebSocket、模拟输入和进程命令均不属于本 socket。

服务线程单独处理有界请求，收发超时1秒。调用方串行 stop/destroy，先停适配器再停 Core。stop(timeout_ms) 超时返回 BUSY，保留存活对象；timeout_ms=0 等待结束。destroy 等待停止后仅删除本实例创建的 inode。配置与 Core/publisher 生命周期由可信装配层负责，不提供默认凭据。

S1-05已完成，正式结果以 docs/evidence/stage-01/S1-05/summary.md 为准；不构成硬件验收。
