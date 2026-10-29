# S2-02 输入、RC522与SPI开发者验证

| 字段 | 当前事实 |
|---|---|
| 卡号 / 契约 / 前置 | S2-02 / 0.2.1 / S2-01 DONE（e62bd06）；阶段一PASS |
| 状态 | DONE；PC0实物标定按用户2026-10-29明确授权后移；阶段2门禁NOT_RUN；S2-03未开始 |
| 入口 | `python -B scripts/build_firmware.py --target s202 --clock hse8_168`；HSI用hsi16；`python -B scripts/generate_keil_s201.py --target s202`；firmware/keil/locker_s202.uvprojx |
| 产物 | 输入规范化、30ms去抖纯函数/诊断、ADC换算与无效保护、RC522三级UID/BCC/CRC/HLTA、双SPI互斥/独立片选、SWD诊断与正式测试 |
| 构建 | ARMCC5.05u1/MDK-Lite5.14，两目标exit 0、各36单元、254依赖哈希匹配；HSI/HSE ROM 12496/13904 B，RAM 20296 B；低于32KiB工具限制及128KiB主SRAM |
| ADC分支 | `--check-adc-build`独立编译exit 0，ROM 15312 B；未烧录；软件换算/有效范围回归通过，不代表毫伏实物标定 |
| 上板 | 四版镜像均守卫既有Flash、限定第0扇区、逐字节回读及尾部保护通过；最终[board-input-pullups.json](board-input-pullups.json)，HAL/RTOS约1000 tick/s，fault=0、拒绝诊断=15 |
| 最终指纹 | HEX SHA256=25a23854ed0f7c157b3d763dd45356893e90aa453f6d4701de07e1bdfb66a7ee；profile仍UNVERIFIED |
| 闭门 / 有物 | 最终镜像PE0=0、PE1=0，规范枚举CLOSED=1/PRESENT=2，去抖结果相同；见pullup-closed-card-01..03.json及final-card-success.json |
| 开门 / 无物 | 最终镜像连续5次PE0=1、PE1=1，规范/去抖均OPEN=2/ABSENT=1；见pullup-open-01..05.json |
| RC522实物 | 模块VersionReg=0x82，配置寄存器回读通过；普通卡63 A9 FB 06、钥匙扣A2 1A 2B 07均4字节成功；普通卡在最终镜像复验成功，成功观察610次；钥匙扣证据为上拉调整前同一驱动版本 |
| UID边界 | 正式协议fixture验证4/7/10字节、BCC/CRC损坏、碰撞、截断/非整字节、传输错误、无卡超时、级联不一致；失败清空UID；7/10字节暂无实物卡，不冒称上板通过 |
| Flash / SPI | JEDEC=EF4018、STATUS=0；最终普通卡记录SPI1/2事务3193/99525，HAL错误0、争用0；单次SPI最大0/1ms（tick精度）、读卡最大33ms；无写/擦/挂载，真实busy组合验证留S2-04 |
| 未授权不开锁 | 读卡无动作调用；pulse入口持续NOT_READY；所有板上镜像PB6维持输入、ODR bit6=0；MOS 12V断开、真锁拆离；不算真锁或硬截止验收 |
| Debug / 主机 | VMware Ubuntu本卡2/2；上拉整改后本卡2/2、既有主机全量36/36退出0；上拉前主机全量也36/36 |
| Sanitizer | 本卡ASan/UBSan 2/2、TSan 2/2，各运行一轮；随后仅GPIO上拉和fixture断言调整，按P2只重跑Debug/全量CTest；不宣称目标板RTOS被TSan验证 |
| 设计检查 | check_contracts、check_stage_plan、check_vision_contracts，最终结果见checks.json；仅设计一致性 |
| 失败与整改 | 慢速SWD无法在10ms内连续读取40字，改为短暂停核/恢复且不写内存；0x82首版拒绝，增加明确兼容及配置回读；门磁无内部上拉时重复出现低/高游移，启用内部上拉后开/闭稳定；原失败记录保留 |
| 待验与边界 | PC0分压/100nF/基准和毫伏标定后移至基础门禁前补证，当前supply_valid=false；外部10k接触可靠性待安装复核；7/10实物卡、Flash真实busy、复位瞬态/硬截止另见debt.md；UART DMA未开发 |
| 文档 / 残留 | 当前入口、卡规约、板级资源和驱动README同步；正式fixture、SWD诊断与原始测量保留；传输tar清理，先前硬件/环境未提交改动不混入本卡 |
| 耗时 / 轮数 | 19:48起至20:33左右，约45分钟墙钟；含实物等待/会话中断，纯开发时间未单计；4版上板、6轮本卡Debug、2轮主机全量、各1轮sanitizer |
| 下一卡 | S2-03，仅推荐；按本次授权停在其开始之前，不自动开发 |

原始标签中有“卡片已在场”被用户随后更正为未放卡的三条记录，已保存original_physical_label与operator_correction，原数值不变，不将其当作有卡失败。卡片不在场时本模块可能报告NOT_READY或空FIFO的IO_ERROR，均清空UID，不伪造成功。开门无物的第一次读数不稳定，只有启用上拉后的连续证据用于最终通过。

复用调研、许可/维护/风险/适配成本比较及诊断字段见[驱动说明](../../../../firmware/drivers/README.md)。未引入Arduino/C++库，不改本地HAL/锁定依赖、公开契约或生成物。
