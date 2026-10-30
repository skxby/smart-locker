# S2-04 真实 littlefs / 最小 Journal 开发者验证

| 字段 | 实际结果 |
|---|---|
| 卡号 / 契约 / 前置 | S2-04 / 0.2.1 / S2-02 DONE（031e140）；S2-03 DONE（a68026e）；阶段一PASS |
| 状态 | DONE；仅本卡最小软件/实物验收，阶段2 gate NOT_RUN，profile仍UNVERIFIED；S2-05未开始 |
| 范围 | firmware/journal、drivers/flash、正式Journal测试/板测客户端；集成GNU启动/链接/构建适配、补充CMSIS头及许可、当前文档/机器状态；不改契约、原HAL或旧依赖锁 |
| 实现 | Storage单所有者，8槽FIFO与同步read/ACK RPC；epoch/boot、意图+水位同提交、96字节事实、持久ACK、RECOVER/BARRIER/FINALIZE/RELEASE、释放墓碑、容量预留、故障/非清洁启动持久锁存 |
| 格式 / 容量 | LKRJNL01/version1，384字节头+64×96字节事实+CRC32=6532字节；显式小端，不写C裸结构。staging写全→sync→close→atomic rename→回执；56条拒新意图，保留8条后续事实；64条满拒绝，不回收任何未ACK记录 |
| Flash几何 | 实读EF4018；16MiB、4KiB扇区、256B页、24位地址；WEL/保护/busy超时/页边界及写擦读回保护。真实挂载空片返回IO_ERROR，不自动format；mount-uninitialized-final.json |
| 备份 / 初始化 | 用户明确授权锁和MOS12V断开、备份后初始化；16777216B全容量只读备份并重新校验SHA，原内容全FF；SHA=dffab0dd410657cb30c7b2fd7f2586a4792e8472e58882b3532581f8111a646d。文件在用户Documents/locker-hardware-backups/2026-10-30，不入仓库；flash-backup-01/provision-01.json |
| ARMCC / GNU | ARMCC完整链接超MDK-Lite32KiB，Ospace/O3仍失败，日志保留；合法xPack GNU15.2.1-1.1两时钟39单元编译链接通过，原254依赖SHA与2补充文件SHA匹配；HSE ROM34924/RAM60064B，HSI ROM32648/RAM60064B；不删保护凑容量 |
| 上板 | 最终HSE HEX SHA=87785d9c7c1e94a7ea83ff798c57d941ded39db2584cc54c5c0edc721fa19354；整片MCU旧状态守卫、前3扇区写入/整片回读、原尾保留；board-program-03.json仅证明回读，运行另见board-startup-final.json |
| 提交 / ACK实物 | boot1，诊断事实及意图成功回执，watermark=1、active=ACCEPTED、3条事实；原生页已保存验收JSON，再ACK至boot1/seq3。此为独立SWD诊断，未装配业务UART/Control、无锁动作；transactions-before-cut.json |
| 真实掉电位置 | 用户断开主板USB、确认灯灭、至少3秒后重接。位置为意图/水位/事实/ACK全部提交并回执后；不是写擦中途或电源波形测量。operator-powercycle.json记录原话；不能把软件故障位置说成实物断电位置 |
| 掉电恢复 | 同UID/epoch，boot2、watermark仍1、active UNKNOWN_RESULT；ACK仍1/3，原3条事实逐字节一致，新增BOOT。非清洁启动将dropped_events=UINT64_MAX、continuity=false持久化并DEGRADED；新意图NOT_READY。recovery-after-powercut / degraded-intent-rejected.json |
| 终结 / 释放 / 再启动 | 已知诊断目标无驱动，持久REJECTED后RELEASE；清洁重挂载及实际MCU复位后boot4，watermark=1、active=false、7条事实、ACK仍1/3；故障/未知丢失数未清。release-does-not-clear-fault / recovery-after-clean-reset.json |
| 最终板上状态 | S204独立SWD目标，Journal已清洁关闭，诊断epoch及未ACK记录保留；持久状态DEGRADED，不允许新动作。PB6始终输入低；UART不运行；未重新format掩盖故障。final-safe-close.json |
| VM Debug / 主机 | VMware Ubuntu，ssh locker-vm；最终本卡3/3，全量主机36/36，均exit0；vm-metadata-final及清理后vm-precommit。2872个主机输入与S2-02清单逐文件SHA匹配，主机代码无变 |
| Sanitizer / P2 | ASan/UBSan、TSan各一轮3/3 exit0，vm-final；覆盖队列/同步RPC/并发与事务主体。随后ACK半零拒绝、持久boot_open/损失锁存整改按P2仅跑Debug+全量CTest，不宣称最终元数据分支另跑过sanitizer |
| 正式故障场景 | 最终NOR夹具：意图35、ACK37、FINALIZE37、RELEASE35个写/擦/sync故障位置；11种CRC/有效CRC但状态或连续性矛盾；队列满/token、精确重试不重发LIVE、满/预留/未ACK、终结/释放幂等、非清洁损失锁存；双pthread生产者与单Storage并发；Flash指令失败/停钟/保护/读回。软件夹具不等于实物 |
| 实物证据复核 | verify_evidence.py只读校验备份SHA、同epoch、回执/游标/水位、断电恢复/锁存、前三事实原生字节；硬件checks输出PASS，清楚列mid-program power cut=false、lock cutoff=false |
| 失败与整改 | SSH一度不可用后用户恢复；首次loader初始化在擦除前HardFault，保留board-program-02及fault-state；改复位即停核/清洁MSP上下文。继承的调试reset catch曾令核停在Reset_Handler，03的运行值已标为不作证据，恢复运行后重验；mount-timeout-state保留 |
| 文档 / 残留 | 当前入口、Journal/Flash/板测/GNU说明及状态同步；清理本轮一次性cmd、pyc和传输tar，保留正式测试、诊断目标、原始失败/测量。既有硬件/环境改动不混入本卡 |
| 耗时 / 轮数 | 2026-10-29约21:22起，中断跨夜；10-30约09:11续作，含约8分23秒Flash备份及实物等待；截至09:58本次续作约47分钟，跨夜中断及首日活跃时间未精确计量，不把跨夜算纯开发。VM Debug约7轮、主机全量4轮、sanitizer各1轮；成功烧录2轮、loader失败1轮、真实掉电1次 |
| 下一卡 / 授权 | recommended_next=S2-05，仅推荐；本次授权到S2-05开发之前，已停在交接点，不push |

最终命令、接口生命周期及接入限制见[handoff](handoff.md)，未完成的后续门禁见[debt](debt.md)。`source-manifest.json`是sanitizer批次快照，`tested-inputs-final.json`是最终Debug输入；不将两个版本混为一次验证。设计checker只证明一致性，结果见checks.json。
