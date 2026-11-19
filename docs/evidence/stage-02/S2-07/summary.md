# S2-07 真实硬件台架闭环

日期：2026-10-30｜契约：0.2.1｜状态：**BLOCKED（软件工具已验证，实物前置待补）**。
范围：台架测试工具与联调。用户授权先台架测试、项目测试通过后部署柜体；柜体安装验收纳入S5-05。S2-08未启动。

## 实现与入口

| 位置 | 当前能力 |
|---|---|
| tests/hardware/bench_client.py | 交互HTTPS借还/确认/取消/管理员核验；本机IPC presence/maintenance；固定现有API，不提供直接开锁/传感器注入/清库 |
| tests/hardware/README.md | 台架条件、本人申请/到柜/物理变化/确认流程，未知结果不重放、维护边界与复用依据 |
| tests/hardware/test_bench_client.py | 9项客户端边界回归，CTest接入；不是实物证据 |
| CMakeLists.txt | 引入tests/hardware回归 |
| run-vm-validation.sh | VMware固定快照校验、Debug全量CTest、收尾ASan/UBSan；日志在VM工作区src之外 |

复用既有test_stage_gate.py的业务协议和Python标准库，不新增依赖/公开接口。调研与许可/维护/适配说明见tests/hardware/README.md。
TLS严格验证证书与SAN；登录只读控制终端密码，会话仅内存保存；证据遮盖凭据，独占新建0600文件。客户端单线程，每个IPC连接一包并关闭，HTTPS请求后关闭；不拥有账本或设备驱动。
私有IPC验证规范路径、0600/UID、SO_PEERCRED及inode；只消费现有服务端鉴权，不伪造peer。保留调用者版本/幂等键，不自动重试；超时记录未知结果，先查原操作与实物。

## 前置与实物状态

| 项 | 事实与剩余条件 |
|---|---|
| S2-06 | DONE，f165668恢复补证见../recovery-preflight/summary.md；历史36/36不计本轮测试 |
| 锁 | 用户报告LY-03带尾杆、12V/1.15A、通电缩回/断电伸出、行程10mm、通电<10s；产品硬截止仍≤1000ms |
| MOS | 页面YYNMOS-1/3.3V；用户确认PB6/P2-23至PWM与信号GND接通，10kΩ假负载初测见下表 |
| 保险 | 用户确认3A/250V，串在12V+与DC+之间；F/T特性、DC分断与线材/电源配合尚未核实 |
| 二极管 | 页面选1N4007 DO-41，用户确认有环K接锁正极；实物测试/重复续流能力待补 |
| PC0 | 用户确认未接，未标定，supply_valid=false；不得借客户端解除真锁资格 |
| profile | 仍UNVERIFIED；需供电/保护/带载默认关闭与截止等真实证据，不凭接线或假负载初测改VERIFIED |
| Journal | S2-06最近报告DEGRADED、continuity_ok=false，未ACK记录保留；恢复产品固件不代表日志故障解除 |
| 机械 | 无正式柜体不阻塞；台架锁体/锁扣稳定定位、板卡绝缘及线束固定尚需核实 |
| 账户 | 测试管理员和普通用户现场可用性待核对，密码/token不入库 |

[Vishay 1N400x规格参考](https://www.vishay.com/docs/88503/1n4001.pdf)列平均正向电流1A与特定波形下浪涌额定值；不证明用户器件厂家。对1.15A线圈的续流须核实际电流、衰减与重复条件，不直接套浪涌值。未新增采购。
商品截图含收货信息，不复制至仓库；用户要求不再索要接线照片，后续以实际测点/原始数值/波形补证。

## MOS假负载初测与固件恢复

真锁脱开、OUT±接10kΩ、PC0未接。用户当前无示波器，只记录万用表和指示灯初测，不能证明精确脉宽、瞬态或真锁带载通过。

| 项 | 结果与证据 |
|---|---|
| 双电源默认关闭 | 用户实测PWM对信号GND=2mV，OUT±=0V |
| MCU无电、12V保持 | 用户实测两项均0V；步骤要求隔离USB/ST-LINK/CH340等可能倒供电源，保留PWM/GND。仅稳态 |
| 原镜像/备份 | mos-precheck.json、mos-flash-backup.json；测试前内置1MiB备份位于build/hardware-S2-07/mos-bench/internal-flash-before.bin |
| 首次烧录失败 | 算法初始化HardFault发生在erase前，第0扇区指纹未变，mos-bench-init-failure.json |
| 测试镜像 | 用户确认12V关/PWM断开后，reset_and_halt并初始化核心状态，复用S2-05镜像只替换第0扇区，其他扇区逐字节未变；mos-bench-program.json、mos-bench-ready.json |
| 第1次单发1秒 | count 0→1；后续用户确认当次12V未开，不用于判断MOS导通失败；mos-bench-trigger-1-1000ms.json、mos-observation-01.json |
| 第2次单发1秒 | count 1→2；PB6实时采到高→低/TIM4停止，用户灯亮但表笔未稳；mos-bench-pulse02-samples.json |
| 第3次单发1秒 | count 2→3；用户实测PWM对MOS信号GND为3.3V→0V，两灯熄灭；mos-bench-pulse03-samples.json、mos-pwm-meter-result.json |
| 第4次单发1秒 | count 3→4；用户确认OUT±升到接近12V后回到接近0V，未提供精确峰值；mos-bench-pulse04-samples.json、mos-output-meter-result.json |
| 产品固件已恢复 | 用户确认隔离条件后恢复S2-06产品镜像；五任务、tick递增、PB6低，restored-product-program.json；完整1MiB与测试前备份SHA一致，restored-backup-match.json |

各次均独立确认就绪后触发并留intent，无自动重复。同步采样不halt、不改定时器寄存器，逻辑电平不替代实际电压/精确波形。假负载输入/输出初测符合预期，不放宽PC0/profile/Journal门槛。最近硬件确认：12V关闭、PWM脱开、真锁脱开，按恢复时用户确认记录；本轮未要求接回真锁。

## 验证与验收矩阵

客户端边界回归（Windows）12/12 通过；VM Debug 全量 37/37（含最终固件改动后重跑）；
ASan+UBSan 全量 37/37（跑在 journal.c 修复版本上，其后仅改栈/堆尺寸与一个诊断全局）。
日志见 vm-debug-ctest.log、vm-asan-ctest.log；四项设计检查 PASS。
未改 C 线程/共享锁，不新增 TSan 轮次。

| 场景 | 实物结果 |
|---|---|
| 本人借出/归还：HTTPS→presence/真实卡→锁/输入→查询→本人确认→终结/释放 | NOT_RUN |
| NO_CHANGE、未确认取物待核验、尾杆hold及本机维护 | NOT_RUN |
| 并发竞争、只ACK、提前确认、物理UNKNOWN | NOT_RUN |
| 取消遇可能执行、主机支路断电、MCU重启 | NOT_RUN |
| ACK丢失、日志失败；恢复核对且不重复驱动 | NOT_RUN |

客户端已部署 RK 并逐字节 SHA 核对，Python3.8.6 编译、本机 TLS1.3/SAN 通过；
未登录 401、未信任证书拒绝（rk-client-*.log/json）。S2-08 保持 NOT_STARTED，
阶段2门禁 NOT_RUN；active_task=null、recommended_next=S2-07；未完成卡不提交、不 push。

## 账户基线与 epoch 演进

原 admin 密码无人记得，`locker-admin` 无密码重置，经授权改用独立状态目录
`/userdata/locker/state-s207`（旧 `/userdata/locker/state` 原样保留）；密码由用户现场设置，
本会话不接触。账本：admin 1、tool A1 1、operations 0。

Journal 在外部 W25Q128（16 MiB）上，连续性锁存是 epoch 级的，只有 provision 能清，
且任何非干净关闭都会再次锁存。本轮因此重建 5 次：

| epoch | 用途 | 结果 |
|---|---|---|
| `9628099c…` | S2-04 遗留 | DEGRADED、30 事实，16 MiB 全片归档 `8e6f7420…` |
| `a054d7fd…` | 首次重建 | 宿主零导入 |
| `99eca8a8…` | 装诊断后 | 定位 `import_status=3` |
| `a75ce458…` | 固件哨兵修复后 | 导入 4→11 条、continuity=true |
| `6fad5fcd…` | 栈修复后 | HEALTHY、干净关闭，当前设备侧 epoch |

## 两个根因（都已修复）

**① 导入死锁**：固件内部事实（BOOT/COMMAND_ACCEPTED 等，零初始化）带 `supply_mv=0`，
而 `repository.c:879` 要求未知必须是 `0xffffffff`，整页被判 INVALID 且**静默丢弃**，
于是不落库→无连续性→RECONCILING 死锁。模拟器不走该路径，S1 门禁从未暴露。
修法：在 `journal.c` 的唯一 append 入口规范化。见 [host-import-defect.md](host-import-defect.md)。

**② 整机静默**：导入修好后出现的 `hello=0 pend=1 lost=0` 无限重开，实为 Storage 任务栈溢出
（`pxStack=0x20002308`，保存 SP `0x200022ec`）触发 `env_assert_failed()` 中断自锁；
SWD 读到 tick 与 `s205_diag[0]` 冻结、`PRIMASK=1`、`CFSR/HFSR` 全 0。
修法：Storage 栈 6 KB、堆 24 KB、溢出任务名留在 RAM。修复后 60 s 内 0 次重开。
见 [stack-overflow-freeze.md](stack-overflow-freeze.md)。

工具侧顺带修：`journal-maintenance.py` 的 FreeRTOS 符号改为从 map 解析（硬编码地址在 `.bss`
后移后曾误报“scheduler dead”）；`flash-and-trigger.py` 加证据防覆盖。

## 当前状态

设备侧 epoch `6fad5fcd…` HEALTHY、干净关闭；宿主 `device_state` 仍停在旧 epoch、
`continuity_ok=false`，需现场关门（door=CLOSED）、真实工具就位（item=PRESENT）、
管理员登录后 `reconcile` 才能进入 RUNNING。当前 door=OPEN、item=ABSENT、12V 关闭、
真锁未驱动、PC0 未接，本轮未发任何脉冲。详见 debt.md。
