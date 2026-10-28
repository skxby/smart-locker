> 归档：S1-05早期部分实现过程，不是当前执行规格；当前入口为上级summary.md与handoff.md。

# S1-05 开发者验证记录

| 字段 | 结果 |
|---|---|
| 状态 | IN_PROGRESS；不是整卡交付 |
| 契约 | 0.2.1 |
| 环境 | VMware Ubuntu，ssh locker-vm；Windows 为源码主副本 |
| 已实现 | 锁定依赖的离线构建；固定 16 条环形变化提示发布器；Core 登录、令牌认证、当前用户查询及注销处理器（本轮 Debug 回归通过） |
| 依赖修复 | 从已有 OpenSSL 3.5.9 归档补齐 24 个 build.info；no-tests/no-docs 仍读取这些构建描述；未引入上游测试私钥 |
| 验证 | resume-20261028-05：Debug 配置/构建/CTest 均退出 0，28/28 通过，包含真实 Argon2id 正反口令及依赖版本检查 |
| 源指纹 | resume-20261028-05.tar SHA256：9ab44cc64cf24e666ce487a2c7fb93829c4df9981f64a0cbc6023ff52f5f9c33 |
| 发布器验证 | resume-20261028-06：Debug 配置/构建/CTest 均退出 0，29/29 通过；SHA256 d305b3cd9d7f15edaecd90d217efd3e828745b08b4047e84796541584fe9f2b0；之后仅发布器注释/缩进及文档、换行格式调整 |
| 失败路径 | 02 缺 fuzz/build.info；03 缺 doc/build.info；原始失败日志保留 |
| 设计检查 | check_contracts、check_stage_plan、check_vision_contracts、check_feasibility、check_network_dependencies 通过；只证明各自检查范围 |
| 未实现 | 用户/卡管理处理器、完整 REST、WS 连接与 IPC 服务；HTTP 已接来源限流，IPC 来源限流待实现 |
| 未运行 | 完整 HTTP 权限矩阵、IPC 负例、整卡 ASan/UBSan/TSan 收尾验证、硬件验证 |
| 提交 | 未完成卡，不做整卡提交，不 push |
| 下一步 | 继续 S1-05，禁止开始 S1-06 |

VM 日志位于 `/home/%USER%/locker-workspace/logs/S1-05/resume-20261028-02` 至 `resume-20261028-06`。快照位于 artifacts/S1-05，源码位于 S1-05/batches/<批次>/src，构建与源码分离；每批次验证 manifest，不覆盖旧源码与证据。

本次涉及根 CMake、host/http、tests/contract/network、依赖文件清单及本卡记录。其他板卡/接线未提交改动保留。临时下载包与传输归档留在被忽略的 build 目录；正式 vendor、测试及失败日志不清理。当前模块说明见 host/http/README.md。README 与机器索引均记录 S1-05 正在开发。

本轮认证变更：`domain_auth.c` 在 Core 线程验证 Argon2id，仅持久化随机令牌 SHA-256，读取时复检当前用户/角色/停用/过期/撤销；按账号每分钟最多 5 次失败，固定 64 槽，活跃槽满时拒绝新增账号工作，全局口令工作间隔至少一秒。HTTP/IPC 来源限速尚未实现。完成槽 principal 存取新增 token 核对。测试仓储修复 after_key 分页，新正式测试分别覆盖领域失败路径与真实 Core+SQLite 公开调用链。auth-20261028-01 编译失败（测试 fixture 参数遗漏）；02 存在分页循环超时、旧占位断言和旧时钟取值失败；03/04 为过时快照，构建已中止，日志保留。auth-20261028-05 因旧 SESSION_CURRENT 占位断言失败；最终 auth-20261028-06：Debug 配置/构建/CTest 退出码均 0，31/31 通过，22.27 秒；归档 SHA256 c7b3c2d84267c64db82c561639ee44663867b7c3a99c3f816c00cca6cff1cb3c。日志和逐文件指纹已回传本目录 auth-20261028-06/。另以同快照全部领域源码、已验证依赖做聚焦编译，两项认证测试分别 32/26 个断言通过。该结果不代表 HTTPS/IPC、角色矩阵或整卡门禁通过。

## HTTPS 接入验证（2026-10-28）

| 字段 | 记录 |
|---|---|
| 范围 | 新增 host/http/http.c 与 locker_http 构建目标；接入会话三入口及 device/snapshot/tools 只读入口；无 WS/IPC/管理入口 |
| 安全边界 | TLS ≥1.2，独立证书链/私钥配置，IPv4 监听地址校验，最多 4 worker；4096 字节 body 上限，禁 Transfer-Encoding/URL 参数；来源 64 槽、每分钟最多 5 次尝试；HTTP 仅调用 Core 公开端口 |
| 正式测试 | tests/contract/network/test_https.py 驱动真实 Core+SQLite fixture；证书和密钥运行时生成，退出清理 |
| 已验证路径 | 非信任证书拒绝、缺令牌拒绝、错误口令拒绝、正确登录与当前用户、注销及重复注销、已撤销令牌拒绝、超大 body、HTTPS presence 路由缺失、URL actor 参数拒绝 |
| 失败记录 | https-01 为早期中间实现；https-02 Debug 编译通过且原 31 测试通过，新测试因 CTest 未解析 Python 未运行；手动运行暴露测试 OpenSSL 缺默认配置；修复后暴露无 Content-Length 的 GET 被误拒绝。https-03 保留中间输入与日志，不用于最终结论 |
| 修复 | 测试目录显式查找 Python；证书生成指定空配置；无请求体 GET 接纳 CivetWeb 的 -1 长度表示，仍拒绝 Transfer-Encoding 与登录未知长度 |
| 聚焦回归 | 固定 https-02 依赖与正式 fixture 链接候选 HTTP 源码，严格编译及 HTTPS 用例退出 0；此项仅为整改定位，不替代最终完整快照回归 |
| 最终快照 | https-20261028-04；SHA256 8f65886a099021051fd6424a793c4dab9bb3a24e6c29f900f89aa00b5019e0c9；Debug 配置/构建/CTest 均退出 0，32/32通过，25.51秒；日志与source-manifest已回传 https-20261028-04/ |
| 生命周期限制 | stop 当前等待 CivetWeb 回收线程，timeout_ms 尚不提供强制中断保证；调用方先停 HTTP 再销毁 Core，stop/destroy 串行 |
| 当前状态 | IN_PROGRESS，未满足整卡验收，不提交整卡、不推进 S1-06；硬件仍未验证 |

## REST 详情与请求关联（2026-10-28）

| 字段 | 记录 |
|---|---|
| 状态 | IN_PROGRESS；本轮不是整卡交付 |
| 范围 | GET /tools/{tool_id}、/operations/{operation_id} 经 Core 查询；校验小写 UUID，资源不存在/无权统一 404；X-Request-ID 校验并贯穿成功/错误响应 |
| 正式测试 | test_https.py 增加关联 ID、非法 UUID/路径后缀、未登录查询、缺记录 404；使用真实 TLS + Core + SQLite |
| 整改 | rest-01/02 编译成功，全量 31/32，操作缺记录返回 503；原因是 loader 以 NOT_READY 表示缺记录。仅详情查询转换为 INVALID，HTTP 映射404，不改命令处理路径 |
| 待验 | rest-03 HTTPS通过，旧领域测试仍断言缺记录NOT_READY，按公开查询语义修正为INVALID并保留命中成功断言；rest-20261028-04 配置/构建/CTest 退出码均0，32/32通过，25.15秒；SHA256 60340e7aaad332ecc8048beae6330932f2c34fda4984cf6c3ec464a1e9cb8e70；日志与manifest已回传；整卡 sanitizer 尚未执行 |
| 未完成 | 完整 REST/分页、用户及卡管理、WS、IPC/权限矩阵；loans/events 当前 Core 存在未完成实现，不得直接开放为完整历史；硬件未验证 |
| 其他改动 | 阶段入口同步 active_task=S1-05；现有硬件/接线等工作区改动保留，不混入本卡 |
| 残留 | 本轮无临时脚本/密钥/调试输出进源码；验证归档放被忽略 build，正式失败日志保留 |
| 提交 | 本卡未达到 DONE，不提交整卡、不推进 S1-06 |

## 操作 REST 续开发（2026-10-28）

| 字段 | 记录 |
|---|---|
| 状态 | IN_PROGRESS，不是整卡交付；不推进 S1-06、不提交整卡 |
| 实现范围 | POST /operations、/operations/{id}/confirm、/operations/{id}/cancel 经 Core 公开命令端口；必填非 nil 幂等键，限定 JSON 字段、UUID、枚举、字符串 uint64 版本与取消理由字符数；拒绝 actor_id 与超限请求 |
| 响应 | 创建成功适配 201/Location；确认与取消先查资源可见性；Core 未就绪 503、缺记录404；幂等冲突返回 IDEMPOTENCY_CONFLICT，保留 CONFLICT 内部状态，正式领域测试覆盖错误码及本次关联 ID |
| 验证命令 | python -B docs/evidence/stage-01/S1-05/verify-vm.py --run operations-20261028-04 |
| 退出码 | Debug 配置0、严格构建0、CTest0；32/32，25.43秒；真实 TLS/Core/SQLite 测试新增未登录、缺/非法幂等键、非法字段/版本/枚举/UUID、设备缺失拒绝及确认/取消缺资源负例 |
| 输入 | 快照SHA256 ec817ea5d982b9548b4e9f52969260570f9e3afedccedeecf6763e92a394171a；3958文件逐项核对。回归后仅测试文件换行恢复为原文件CRLF及文档更新，无语义变化 |
| 证据 | operations-20261028-04/ 下 configure/build/ctest/environment 日志与 snapshot-manifest.json；VM日志 locker-workspace/logs/S1-05/operations-20261028-04 |
| 中间批次 | 01 缺依赖检查脚本、配置失败；02/03 被后续修复和新增断言替代，构建已中止，原始日志保留；未算通过 |
| 设计检查 | check_contracts、check_stage_plan、check_vision_contracts、check_feasibility、check_network_dependencies 全部退出0；不代表硬件测试 |
| 未完成 | 操作成功TLS闭环及完整幂等/权限矩阵、剩余REST/分页/状态细分、用户/卡管理、WS、IPC、整卡ASan/UBSan/TSan；未验证RK ABI与硬件 |
| 残留 | 无本轮调试输出/私钥/一次性脚本入源码；清除py_compile产生的pyc；正式测试、vendor、失败日志保留；已有其他卡/硬件修改未清理 |
| 度量 | 本轮4个快照批次：1次配置失败、2次中止、1次全量成功；最终批次17:41:53–17:45:39（3分46秒），不是整卡实际总耗时 |
