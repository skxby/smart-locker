# S1-07 主机集成门禁

| 字段 | 当前事实 |
|---|---|
| 卡号 / 契约 | S1-07 / 0.2.1；S1-01至S1-06报告均为DONE |
| 状态 | DONE；阶段1 gate PASS；S2-01 NOT_STARTED |
| 环境 | Windows主副本 %REPO%；Linux运行仅在ssh locker-vm的VMware Ubuntu x86_64 |
| 范围 | 新增正式进程级测试 `tests/integration/runtime/test_stage_gate.py`，连接真实locker-service、locker-admin、locker-device-sim、受信HTTPS、私有IPC、AF_UNIX UART及两个SQLite文件；接入CTest |
| 进程级结果 | 完整借出/归还、两用户竞争、幂等重试、错误卡拒绝、丢EXECUTE应答后单次动作、服务重启、断链后重启与新会话核对、TLS/权限/IPC边界、双SQLite核对均通过；进程测试连续3次通过。 |
| 已关闭缺陷 | Core事件环先处理、后刷新单调时间，实时读卡偶发被时间锚判为“未来事件”；`domain_core.c`在每条事件处理前刷新时间。VM进程测试连续5次通过，全量Debug/ASan+UBSan/TSan各36/36。 |
| 已知限制 | 新增测试在临时目录使用一次性证书、密码及数据库；本卡软件验证仅覆盖VM x86模拟。用户确认硬件方面全部测试通过，尚未提供本次声明对应的项目清单、实测值和原始记录，硬件profile仍为UNVERIFIED；不将口头确认写作S2实物门禁PASS。 |

## 场景门禁

| 场景 | 状态 / 证据 |
|---|---|
| 完整借出/归还 | PASS：真实服务、模拟器、HTTPS/IPC/UART字节和双SQLite进程测试。 |
| 竞争与重试 | PASS：进程测试两用户竞争、同键重试/异正文冲突；`integration.test_core_interrupt_matrix`覆盖并发。 |
| 用户和管理员权限 | PASS：进程测试匿名、错误口令、IPC匿名拒绝；`network.https_flow`覆盖角色及人工核验理由。 |
| 传感器与确认冲突 | PASS：进程测试提前确认拒绝；`integration.test_core_interrupt_matrix`覆盖ACK、UNKNOWN、再次变位等冲突。 |
| 实时与历史读卡 | PASS：进程测试现场卡与错误卡；`domain.test_domain_device`覆盖历史补传、重复及旧会话。 |
| 副作用响应丢失 | PASS：进程测试丢EXECUTE应答后动作水位仍只加一；`integration.test_core_interrupt_matrix`覆盖结果未知的查询/核对。 |
| 服务重启 | PASS：进程测试两次重启后账本、动作计数保留并重新核对；`integration.test_core_interrupt_matrix`覆盖未决动作。 |
| 设备重连屏障 | PASS：进程测试断链、重启建新UART会话、RECONCILING→人工核对→READY；`integration.test_core_interrupt_matrix`覆盖屏障物理版本冲突。 |
| 日志与事件缺口 | PASS：`device.test_sim_journal`、`device.test_sim_protocol`覆盖去重、缺口锁存和提交失败拒绝动作。 |
| 数据库事务失败 | PASS：`storage.test_repository`及领域/中断矩阵覆盖失败事务无半条业务意图。 |
| 网络/IPC边界 | PASS（新增进程测试、`network.https_sessions`、`network.https_flow`）。 |
| 背压与退出 | PASS：`integration.test_core_interrupt_matrix`的设备背压场景、common队列测试及`runtime.lifecycle`退出重启。 |

门禁以进程测试、正式模块集成/契约/单元测试的组合判定；表中逐项标明实际层级，不把模块测试说成真实服务注入。VM固定批次`s107-matrix01` Debug、ASan+UBSan、TSan的配置、构建、全量CTest均退出0，各36/36；[进程测试连续3次](vm-matrix01/stage-gate-repeat.log)退出0，[原始日志](vm-matrix01/debug-ctest.log)、[ASan日志](vm-matrix01/asan-ctest.log)、[TSan日志](vm-matrix01/tsan-ctest.log)。静态契约、阶段索引、视觉契约、可行性检查各退出0。先前失败日志保留为缺陷证据。

## 残留与文档

测试证书/私钥、临时数据库和socket由测试临时目录清理；正式回归测试、失败与通过日志保留。外部硬件/Keil未提交改动原样保留。README、阶段入口、机器索引及模拟器README已核对；未新增生产调试开关或模拟HTTP接口。开发者验证2轮（clock01与matrix01），本卡累计实际耗时约55分钟。当前VM服务不会在断链后进程内自动重连；进程测试通过显式服务重启恢复，后续现场运行应单独验证重连策略。下一卡为S2-01，尚未开始。
