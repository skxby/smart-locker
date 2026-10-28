> 归档：S1-05早期部分实现过程，不是当前执行规格；当前入口为上级summary.md与handoff.md。

# S1-05 继续开发交接

| 字段 | 当前事实 |
|---|---|
| 卡号/契约 | S1-05 / 0.2.1，IN_PROGRESS |
| 前置 | S1-04 DONE；本卡不能自动推进 S1-06 |
| 本轮范围 | host/domain/src/domain_commands.c（仅操作详情缺记录映射）、docs/stages/README.md、host/http/http.c、host/http/CMakeLists.txt、host/http/README.md、tests/contract/network/CMakeLists.txt、test_auth_repository.c、test_https.py、本卡 summary/handoff |
| 构建入口 | python -B docs/evidence/stage-01/S1-05/verify-vm.py --run <新的批次名>；只用 ssh locker-vm |
| Core 调用 | HTTP 认证用 locker_core_authenticate；详情新增 /tools/{tool_id}、/operations/{operation_id}；查询用 locker_core_query；命令 locker_core_submit 返回 QUEUED 后，用内部 submission_token 调 locker_core_wait_result(1000ms)，不按外部 request_id 寻址 |
| 参数/内存 | http_start 复制配置，借用 Core，publisher 尚未用于 WS；每请求分配一个响应 DTO，释放前清零；登录命令与 token 工作副本清零，不记录凭据 |
| 线程/销毁 | CivetWeb 1–4 worker；来源槽 mutex 与 Core 队列隔离；先 stop HTTP 再销毁 Core；stop/destroy 串行，timeout_ms 暂无强制中断保证 |
| TLS/依赖 | CivetWeb 1.16、libsodium 1.0.22、OpenSSL 3.5.9；TLS≥1.2；可信装配配置 IPv4 地址、证书链和独立私钥；测试密钥运行时生成 |
| 已有入口 | /api/v1/sessions 登录、/sessions/current 查询/注销、/device、/snapshot、/tools；HTTP 未开放 presence、维护或传感器注入 |
| 失败边界 | 4096 字节 body 上限，拒绝 Transfer-Encoding/URL 参数；来源每分钟最多5次尝试，最多64个活跃来源；错误/撤销 token 拒绝；未知路由404 |
| 未实现 | 用户/卡管理 Core 处理器、完整 REST 与分页/错误映射、WS连接及重查、受限 service.sock 和权限矩阵；Device.mode 投影历史债务待接完整公开适配时关闭 |
| 验证 | 原31项回归通过；HTTPS聚焦真实链路通过；详情与请求关联回归见 summary 的 rest-20261028 批次记录 |
| 失败记录 | https-02新测试未解析Python；https-03证书生成默认配置缺失；无Content-Length GET在聚焦回归发现误拒绝，已修复。保留日志，不放宽用例 |
| 未验证 | 整卡ASan/UBSan/TSan、完整HTTP权限矩阵、IPC负例、WS慢消费者、RK ABI/硬件 |
| 残留 | 本轮无调试输出、固定凭据或私钥进库；正式vendor/测试/历史失败日志保留；其他板卡和接线改动未纳入本卡 |
| 耗时/轮数 | 本轮耗时以 https-01 至 https-04 environment.log UTC起止及会话记录核对；4个完整快照构建轮次，另有聚焦编译与HTTPS整改定位；本卡尚未交付，不虚报总耗时 |
| 下一步 | 继续同卡，补齐剩余接口与验收（loans/events Core 仍有占位，不可直接开放为完整历史）；最终清残留、Debug全量、ASan/UBSan/TSan、同步状态后本地提交，不push |

本轮续开发验证：rest-01至04共4轮完整Debug快照；前两轮操作缺记录映射失败，第三轮HTTPS通过但旧领域断言失败，第四轮32/32通过（25.15秒），配置/构建/CTest均0。验证时间跨度 0:12:04（首轮environment起点至最终轮终点，不冒充整卡总耗时）。最终指纹和日志见summary及rest-20261028-04/。新增正式测试与失败证据保留；本轮无待用户裁决的临时残留。用户/卡、分页与事件、业务命令REST、WS、IPC和整卡sanitizer仍未完成；未提交整卡。

操作REST续开发：新增创建/确认/取消适配；命令仍由Core唯一写者处理，无presence或传感器公开入口。HTTP命令参数只允许契约字段，带幂等UUID；Core幂等冲突结果包含具体错误信封。operations-20261028-04严格Debug全量32/32通过，命令、快照SHA和失败批次见summary；原始日志和3958文件manifest已回传。成功TLS操作闭环、完整权限/幂等矩阵及其余接口仍未验收。4批次（1配置失败/2中止/1成功）；最终验证批次3分46秒，本卡总耗时尚未结算。测试源回归后仅恢复原CRLF换行；本轮pyc已清理，无需用户裁决的临时残留。保持IN_PROGRESS，不commit、不push、不跨卡。
