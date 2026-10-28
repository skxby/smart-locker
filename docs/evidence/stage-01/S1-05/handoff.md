# S1-05 交接

| 字段 | 当前事实 |
|---|---|
| 状态 / 输入 | DONE；最新19完成全部验证，S1-06 NOT_STARTED；契约0.2.1，S1-04前置848bea3 |
| 入口 | README → docs/stages/README → stage-plan → 本报告；只按实际状态继续，不运行S1-06 |
| 复现 | python -B docs/evidence/stage-01/S1-05/verify-vm.py --run <新批次> --sanitizers；VM固定包/manifest校验，src与build/logs分开 |
| 完整矩阵 | python3 -B verify-matrix.py <固定批次build-{debug,asan,tsan}/tests/contract/network/test_https_flow_fixture> <同build/network-dependencies/bin/openssl> <固定src>；Debug/ASan/TSan各54项已过，脚本SHA另存 |
| Core调用 | HTTP/IPC先locker_core_authenticate，再query或submit/wait_result；等待只用随机submission_token。两个local端口非NULL receipt取得真实结果，NULL仍只是异步投递OK |
| 事务 | 用户/卡/审计/会话撤销/幂等结果原子提交；本机证明/维护接受结果先持久化，之后才武装/RESUME_READY。ACK、接受响应和准备意图都不代表门已打开 |
| 参数 / 内存 | 配置复制；Core/publisher借用；有界DTO按值入队，结果复制给工作线程。消费队列槽、凭据工作副本与完成JSON释放前清理；不记录token/密码 |
| 线程 | Core是仓储唯一写者；CivetWeb1–4 worker，WS独立发送线程，IPC串行服务线程；发布器16条环形提示try-lock，消费者各自游标 |
| 停机 / 销毁 | 串行stop/destroy；先HTTP/IPC、再设备/Core、最后publisher/repo。timeout返回BUSY时保留对象；destroy等待结束，IPC只删除自己inode |
| 身份 / 限流 | 仅Bearer头/固定IPC包络token；Core复核当前角色及启用/撤销/过期；每账号≤16会话，停用/角色变化撤销。来源每分钟5次、账号失败槽64、全局口令间隔1秒；最后启用管理员受保护 |
| 大小 / 分页 | HTTP4096、IPC8192/65535字节；固定字段、UTF8/Unicode/代理对和u64文本验证；历史limit1–100、默认20，revision变化使旧游标409；按归属过滤及完整记录预算分页 |
| IPC边界 | SOCK_SEQPACKET/MSG_TRUNC，SO_PEERCRED+文件权限/inode+会话；管理员还核UID。无source/actor参数赋权，无跨socket监督代理、模拟或任意进程接口 |
| 卡 / WS | 卡只使用当前会话、未应用且有可信MCU时间锚的观察；绑定30s/证明15s，关联原pending operation。WS先resync后快照，变化/丢失只提示重查，客户端数据帧在分配前拒绝 |
| 依赖 | CivetWeb1.16、libsodium1.0.22、OpenSSL3.5.9；3811锁定输入，无配置下载。CMake只在build生成受限帧解析及原子停止标志副本，原vendor不变；正式发布包configure/Makefile.in须进入版本库 |
| 接口变化 | CR-S105-2已更新真源/生成头/全部C调用方，内部C端口须重编；公开JSON0.2.1不变。测试fixture中的模拟控制只编进测试目标，生产库不开放 |
| 失败路径 | 未知/停用/过期token拒绝，管理员越权403、他人资源404、无物理准备503；同键异意图409，历史重传不武装，响应发送失败不撤销/重放动作 |
| 检查结果 | 19的Debug/ASan+UBSan/TSan配置、严格构建、CTest全部退出0，各33/33；35.92/39.26/40.62秒，权限矩阵各54/54。16停止竞争由17关闭，18资源404补齐，19凭据幂等指纹使用Argon2id；旧实现负控被新断言检出。静态五检查全部0，详细命令与SHA见summary |
| 证据位置 | VM locker-workspace/logs/S1-05/credential-final-20261028-19；源码S1-05/batches/credential-final-20261028-19/src；压缩输入artifacts/S1-05/credential-final-20261028-19.tar.gz；正式日志回传本目录 |
| 文件归属 | 根CMake、contracts真源/头、生成器与依赖锁；common队列清理；domain auth/users/history/本机端口；http/ipc模块；network/unit/integration测试及当前文档。其他硬件/Keil工作区改动保留，不混入卡提交 |
| 残留 / 债务 | 无本轮临时凭据/无引用验证代码进生产；正式测试、失败日志保留。基线部分文件实际CRLF，保留原格式，新增源码LF；diff检查显式识别CR行尾，统一换行独立处理 |
| 环境清理 | 旧build只保留诊断，复验新建build；源码/日志未删除。历史.tar.gz解压字节SHA与旧.tar一致；映射见environment-disk-cleanup记录；清理后可用12.60GiB，最终构建后8.4GiB |
| 度量 | 本轮19快照尝试，含2次环境失败及1次配置失败；实际可证窗口2026-10-28 17:55:32至21:13:58+08:00，3小时18分26秒；包含环境清理/构建/负控，不虚报此前累计工时 |
| 停止点 | 验收通过，本地一张卡一次提交、不push；active_task=null，recommended_next=S1-06。S1-06及阶段门禁/硬件验收未开始 |
