# S2-04 交接

| 项目 | 当前事实 / 接入方法 |
|---|---|
| 卡 / 契约 / 状态 | S2-04 / 0.2.1 / DONE；S2-05 NOT_STARTED，阶段2 NOT_RUN，profile UNVERIFIED |
| 构建 | `python -B scripts/build_firmware_gnu.py --clock hse8_168 --toolchain-root C:/Users/%USER%/Documents/locker-hardware-tools/xpack-arm-none-eabi-gcc-15.2.1-1.1`；HSI改hsi16。目标仅S204。ARMCC完整目标超Lite限制，不使用失败产物 |
| 对象 / 初始化 | 静态w25q128_t配置context/transfer/now_ms/wait_ms→init验证EF4018/16MiB/4096/256/cs0→取得静态lfs_config→attach平台port→由Storage init→bind三个sink→再启动生产者 |
| 平台port | lock/unlock仅保护队列/完成标志，Flash IO在锁外；wait让出CPU；is_owner必须识别唯一Storage任务。对象、port、sink、fs缓存活到任务全部停止。独立板测只有一个应用任务，生产集成需真正的TaskHandle判别 |
| 异步调用例 | Control构造非零且未占用token，`mcu_journal_commit_intent(journal,&intent,token)`仅返回QUEUED；Storage周期process_one，committed(token,status)复制至Control队列。不得在QUEUED或失败回执后驱动 |
| 事件 / barrier | append复制DTO，Storage分配真实epoch/boot/seq；成功且确实新增才event_committed。BARRIER与append同FIFO，barrier_after_token指最后观察append；失败不能造快照。精确重试不重发历史LIVE |
| 同步read / ACK | 同一FIFO，页1..4；after必须填真实device_id/epoch及合法boot/seq，初始cursor为同身份+0/0。非Storage等待完成，不能超时提前销毁栈completion；回调/ISR不得调用这些同步API |
| 管理 / 终结 | RECOVER返回状态；FINALIZE/RELEASE按epoch/当前boot/session/op/command/action核对。PULSE_STARTED只可保守终结UNKNOWN，不能直接release。Control仍负责闭门/无脉冲/physical_revision/时效/session复核 |
| 格式 / 事务 | LKRJNL01/version1，6532B；384头+64×96事实+CRC32。意图/水位/上下文同提交；staging完整write/sync/close/rename成功才回执。C native DTO只用于内存/SWD，不能sizeof裸发UART |
| 容量 | 56条时禁新意图，保留8条；最大64，不回收任何事件。ACK持久但不删除事实，不清水位/活动意图/连续性。S4-01再实现完整ACK后回收 |
| 失败 / 停止 | 实际append失败递增损失并锁存；IO/full/回调投递失败不成功回执。停止生产者和同步RPC→排空FIFO→Storage shutdown，成功提交boot_open=false后unmount。shutdown失败也关闭上下文但不写“清洁”；队列未空返回BUSY不关闭 |
| 非清洁启动 | 未清洁关闭会持久化dropped=UINT64_MAX、continuity=false/DEGRADED；保留水位/未决意图，ACCEPTED/STARTED变UNKNOWN。可读/ACK/记后续事实/受守卫终结释放，禁新意图；session、release、ACK、清洁重启均不清锁存 |
| 当前实板 | 最终HSE HEX SHA87785d9c7c1e94a7ea83ff798c57d941ded39db2584cc54c5c0edc721fa19354；RAM60064B/ROM34924B，CCM未分配。独立SWD目标，无UART/Control；Journal已清洁关闭，boot4、水位1、active=false、7条事实、ACK1/3、DEGRADED/未知损失保留 |
| 维护边界 | 不为继续开发自动format测试epoch。先取回/归档未ACK诊断事实，完成现场日志维护与业务核验，再显式现场建新epoch；没有远程清HISTORY_INCOMPLETE API。新鲜profile/供电/锁硬截止仍须各自实测 |
| 实物 / 软件 | 真实掉电为提交成功后拔USB且灯灭≥3秒；恢复同epoch、原前三事实逐字节一致，ACK/水位未丢。写/擦/sync中途是NOR夹具144位置，不是实物波形/中途断电证据 |
| VM命令 / 结果 | VM源码 ~/locker-workspace/S2-04/src；build/logs在src外。run-vm-validation.sh：Debug/ASan/TSan各3/3、主机36/36；后续metadata-final及清理后precommit：最终Debug3/3、主机36/36，exit0。复跑可用 `cmake --build ~/locker-workspace/build/S2-04/debug-final -j2` 与 `ctest --test-dir ~/locker-workspace/build/S2-04/debug-final --output-on-failure`。P2每种sanitizer一次，最终元数据整改未重复sanitizer |
| 指纹 | sanitizer批次source-manifest.json；最终Debug tested-inputs-final.json；GNU build-gnu-*-final.json、原254文件锁和GNU补充锁。主机引用S2-02的2872文件清单，两端复核一致 |
| 证据校验 | 用正式pyOCD Python环境执行 `tests/firmware/journal/verify_evidence.py --evidence docs/evidence/stage-02/S2-04 --elf build/firmware-s204-gnu/hse8_168/locker_s204.elf`；只读，不驱动硬件 |
| 备份 | 原外部Flash16MiB在C:/Users/%USER%/Documents/locker-hardware-backups/2026-10-30/w25q128-before-s204.bin，SHA见summary。首次只读镜像保存为同目录s204-initial.{hex,elf}，旧构建路径已复用，不能按旧报告盲烧 |
| 失败记录 | board-program-02 loader init失败在擦除前；03回读通过但reset-catch运行判定已明确降级，board-startup-final才是运行证据。不要删除失败JSON或把未初始化挂载拒绝当自动format理由 |
| 范围 / 文档 | Journal/Flash、正式测试、GNU补充头/启动/链接/脚本、README与状态；详细目录和SHA见integration-manifest.json；用户原HAL与既有未提交硬件/环境文件不入本卡提交 |
| 耗时 / 轮数 | 中断跨夜不计纯开发；10-30约09:11–09:58续作约47分钟，含备份/实物操作；约7轮VM Debug、4轮主机全量、sanitizer各1轮，成功烧录2轮、真实断电1次 |
| 下一卡 | S2-05，必须先读卡/本报告/debt及S2-03；本轮不实现四任务装配/锁硬截止，不push |
