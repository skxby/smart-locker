# S1-04 业务核心交付

| 字段 | 当前事实 |
|---|---|
| 卡号 / 契约 | S1-04 / 0.2.1 |
| 状态 | DONE；开发者验证，无新增独立审计 |
| 前置 | S1-01、S1-02、S1-03已完成并复用 |
| 阶段门禁 | 阶段一仍NOT_RUN；S1-07未执行 |
| 接手修复 | 关闭CR-S104-2：DevicePort增加16项按值发送FIFO和独立writer，Core提交不等待socket；过期未发帧丢弃，部分写失败停发，不重放；关闭唤醒两IO线程后再释放fd |
| 范围 | 保留此前host/domain修复；回修host/device/src/port.c和私有头/README；补设备契约与Core集成回归；不改公开契约或生成物 |
| 复现入口 | python -B docs/evidence/stage-01/S1-04/takeover-verify.py --run 新批次名；--sanitizers运行全量三棵树 |
| Linux环境 | VMware Ubuntu，ssh locker-vm；Linux7.3.0-rc5 x86_64、GCC15.2.0、CMake4.2.3；未用WSL |
| 最终快照 | ~/locker-workspace/S1-04/batches/takeover-20261028-return/src；125项SHA-256与Windows主副本一致 |
| 实测结果 | 普通Debug全量27/27；完整生产实现ASan/UBSan、TSan各27/27；之后仅集成测试文件新增连续归还场景，在最终快照补跑该集成测试，两种sanitizer各1/1 |
| 退出码/报告 | 上述configure/build/CTest均0，编译警告0、sanitizer报告0；见takeover-validation.json与takeover-final/、takeover-return/ |
| 下一卡 | S1-05 HTTPS/IPC；本卡完成后停在交接点 |

## 验收与失败路径

| 验收组 | 正式证据 |
|---|---|
| 正常借还与账本 | test_borrow_flow在同一Core/SQLite/模拟器内完成借出→归还；检查两次EXECUTE/两次脉冲、有效借用清空，停止Core后检查同一持久loan的borrow_operation_id、return_operation_id、borrower_id和closed_at |
| ACK与物理证据 | 仅接纳不算开门，物理开门/变位/关门与本人确认共同推进；domain_device与commands相关正式测试 |
| 错误身份/竞争/过期 | 提前确认、错误用户、竞争申请、15秒证明、业务超时、UNKNOWN、重复幂等与失败回滚均有单测及真实Core集成负向断言 |
| 中断/恢复 | BEGIN/INTENT/EVENT/COMMIT/RESULT五阶段，丢应答只查询、抑制脉冲超时、主机重启核验，均不补发EXECUTE |
| 背压与生命周期 | test_async_send_under_socket_backpressure：真实不读取socket+小内核缓冲，提交<200ms、饱和BUSY、写超时NOT_READY、关闭<500ms；旧会话意图拒绝与并发关闭回归保留 |
| Core真实进度 | test_core_progress_under_device_backpressure：同一真实适配器背压下快照查询<250ms、源进度增长、无锁脉冲 |
| 负向对照 | 独立VM副本只恢复旧同步port.c，上述设备/集成两项均失败，CTest exit=8；见takeover-negative/。未修改正式源树或放宽断言 |

所有耗时阈值是回归护栏，不是硬实时承诺。最新完整生产源码在takeover-final三棵树验证；与takeover-return的125项清单比对仅tests/integration/test_core_interrupt_matrix.c变化，最终快照已补跑其Debug/ASan/TSan。两份快照及最终清单保留，不能混称历史r205400已验证当前源码。

## 文档、残留与边界

历史报告保存在history/，旧audit和VM日志保留，不伪造新的审计PASS。当前交接见handoff.md，非阻塞债务见debt.md。一次性.s104-tmp工具包已完整归档到C:/Users/%USER%/Documents/locker-development-archives/S1-04-takeover-20261028/temporary-tools；正式测试、失败日志、快照指纹和复现runner保留。无临时API、假业务数据或新外部产品依赖。

真实RK UART、锁脉冲硬截止、整柜传感器、介质掉电、相机/RKNN均不属于本次主机模拟验收。STM32环境上板证据独立保存在docs/evidence/environment-board-2026-10-28，不算S2-01完成。未实现的S1-05认证/HTTP、S4监护和视觉端口保持既定归属。

## S1-07门禁补救记录

S1-07真实进程测试发现Core处理设备事件前沿用上一轮tick的单调时间，现场读卡偶发被时间锚判为未来事件。`host/domain/src/domain_core.c`在每条事件出队前刷新主机单调时间；不放宽读卡新鲜度或历史补传约束。固定VM批次`s107-matrix01`的Debug、ASan+UBSan、TSan全量CTest各36/36，详情见[S1-07报告](../S1-07/summary.md)。本节为门禁发现的后续补救证据，不改写本卡原始交付结果。
