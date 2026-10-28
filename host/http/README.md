# HTTPS、WebSocket 与变化提示（S1-05）

`LOCKER_BUILD_NETWORK=ON` 构建锁定的 CivetWeb、OpenSSL、libsodium、变化发布器、HTTPS/WS 与 IPC。当前依赖构建适配 Linux x86_64；RK3568 ABI、实际局域网部署与硬件验证属于后续门禁。当前验收状态见 `docs/evidence/stage-01/S1-05/summary.md`。

## HTTPS

`locker_http_start` 复制配置，借用已启动的 Core/publisher；读取证书链和独立 PEM 私钥，最低 TLS 1.2，只创建 TLS 监听器，worker_count 为1–4。证书、监听地址和端口由可信装配层提供，不提供默认密码、注册或直接开锁入口。

`/api/v1` 实现本卡22个公开入口：会话登录/当前用户/注销，device、snapshot、tools及工具详情，操作创建/详情/确认/取消/管理员核验，loans、events、reviews，用户列表/创建/修改，card-observations、绑定/解绑卡，以及 `/ws`。视觉两个入口归 S2-14，当前不开放。presence、维护、模拟传感器和进程命令不通过普通 HTTPS 暴露。

Bearer 只来自 Authorization 头；不接纳 URL token 或 actor_id 赋权。查询/命令仅调用 Core 公开端口，不访问仓储。响应通过内部随机 submission_token 等待，X-Request-ID 只用于关联。UUID 使用小写规范格式；资源缺失或不可见统一404。管理员路由在适配器与 Core 两层检查角色。

请求体上限4096字节，JSON Content-Type，拒绝 Transfer-Encoding、重复键、未知字段、NUL和非法UTF-8；字符串支持标准 Unicode 转义及合法代理对。HTTP/IPC/Core 共用 mutation DTO 校验。操作/管理命令必须携带非nil UUID幂等键。版本使用十进制字符串，包含 uint64 上界检查。历史分页 limit=1–100，默认20；游标与快照版本绑定，过期返回409。用户只能读取自己的操作/借还记录/相关事件；快照同样过滤其他用户的操作和借用。

登录按实际远端地址固定64槽限流，每来源每分钟最多5次尝试；不信任代理转发身份。Core另有账号失败和全局口令工作限流。单账号最多16条会话，登录同事务清理本账号失效记录；停用或角色变化同事务撤销原会话，重新启用不会恢复旧token。用户上限100；最后一位启用管理员不能被停用或降权。管理变更、审计与幂等结果同事务提交。创建用户的密码先按幂等键加盐经Argon2id，再参与请求指纹；不存放可绕过口令KDF的快速密码摘要。

## WebSocket 与发布器

WS 握手使用 Bearer 头。连接先收到 resync_required，再查询快照；变化与队列丢失也只发失效重查提示，不发送业务命令、凭据或其他用户资源ID。客户端控制帧限125字节，必须掩码、FIN、无RSV；其余客户端数据帧在读取负载/分配内存之前关闭。

原始 CivetWeb 源码及依赖锁保持不变。CMake 对固定版本的单一锚点做严格检查，在 build 内生成带帧守卫和C11原子停机标志的编译副本；原始 vendor SHA 校验仍执行。

发布器保留最近16条共享环形提示，每个消费者持有自己的游标。Core只复制固定大小消息并尝试加锁，不执行网络IO；落后或发布争锁丢失返回CONFLICT，消费者重查。WS网络发送与会话复检由独立线程完成，慢连接不阻塞Core发布。

## 生命周期

调用方串行 stop/destroy，先停 HTTP/IPC、再停设备和 Core，最后销毁 publisher/仓储。stop(timeout_ms) 超时返回BUSY，对象和借用依赖必须继续存活；timeout_ms=0等待停止完成，destroy等待工作线程退出后才释放对象。密钥仅由装配层配置；正式测试在运行时生成证书与私钥，不进版本库。
