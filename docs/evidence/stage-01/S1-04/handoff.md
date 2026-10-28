# S1-04交接

| 字段 | 值 |
|---|---|
| 卡 / 契约 / 状态 | S1-04 / 0.2.1 / DONE，开发者验证 |
| 当前实现 | Core单所有者、按值入队、SQLite事务账本、申请/证明/确认/核验、事件连续性、outbox检查点、完成槽token身份与重启不重放 |
| 接手关闭 | CR-S104-2同步设备发送阻塞；DevicePort增加有界发送FIFO/独立writer及背压回归 |
| 验证 | Debug27/27、ASan/UBSan27/27、TSan27/27；最终仅新增归还集成场景，该可执行在最终快照两种sanitizer各1/1；全部exit0、零报告 |
| 运行入口 | takeover-verify.py --run 新批次 --sanitizers；源码快照与构建/日志隔离在VM的locker-workspace内 |
| 后续 | S1-05 HTTPS/IPC；阶段一门禁仍NOT_RUN |
| 可追溯耗时 | 本次首个源码快照2026-10-28 13:30:20，到交接文档约13:55，约25分钟；此前接手阅读/修改和历史开发未计，整卡总耗时未复核 |
| 轮次 | 本次不调用独立审计；普通回归、负向对照、一次全量sanitizer及新增归还集成的定向sanitizer。历史审计轮次见原始档案，不将其结论改为PASS |

## 接口与生命周期

公开声明只取contracts/include/locker_contracts.h。HTTP/IPC工作线程提交locker_core_submit/query，结果经locker_core_wait_result关联token；QUEUED不等于业务成功，result.status才是业务结论，result.json已经是完整信封。等待超时是NOT_READY，不表示动作未执行。

Core唯一写借还账本、唯一作业务设备发送决策；适配器writer只复制/发送冻结帧，不改账本、不授权动作。DevicePort请求返回OK表示本地队列接纳；16项pending和16项FIFO均有界，饱和BUSY。写失败后停止接纳、丢队列并要求核对，不能重发EXECUTE。host_tx按真正写出前采样；排队不延长原证明或MCU not_after期限。

装配顺序：create device→create/start Core（绑定sink）→open device（HELLO）；销毁顺序：core_stop→device_close（唤醒并join reader/writer，回调停止）→core_destroy→device_destroy。业务线程不能在设备回调里销毁Core；回调中的DTO只借用到返回，需要复制。

## 下游必守

- principal由可信认证层构造，不信客户端actor；私有presence/maintenance核对SO_PEERCRED及会话/owner/revision。
- 发送前持久化SEND_ATTEMPTED；ACK不是开门；结果未知只QUERY，历史补传不提供开门授权；重启未决操作进核验。
- 终结、loan、幂等和人工审计同一事务，提交成功才安装内存副本与发布；管理员NO_CHANGE也需原基线和理由。
- 证明15秒；照片/视觉尚不在本卡；WAIT_CONFIRM冻结代际、时限120秒，缺口/UNKNOWN必须核验。
- get_progress只读取真实循环发布样本；重复读取不续命，不经HTTP/IPC阻塞队列。
- auth/HTTP及后续监护/视觉接口未交付，不能以NOT_READY占位当作对应卡完成。投影债务见debt.md，公开上线前由集成负责人闭合。

## 证据定位

summary.md和takeover-validation.json为当前结果，takeover-snapshot-manifest.json/source-manifest.txt为125项真源指纹。takeover-final/记录完整三套27项；takeover-return/记录最终Debug27项及新增集成两套各1项；两份源码差异只有该集成文件。takeover-negative/记录旧同步实现造成的两个正式回归失败。

旧summary/handoff/verification已归档history/，旧audit为历史问题输入；不再运行多AI审计流程。一次性工具已移到用户Documents开发档案，不依赖/tmp/full-verify.sh或HOME根目录脚本。
