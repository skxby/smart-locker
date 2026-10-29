# S2-01 HAL/BSP 开发者验证

| 字段 | 当前事实 |
|---|---|
| 卡号 / 契约 | S2-01 / 0.2.1；前置S1-07 DONE、阶段一门禁PASS |
| 状态 | IN_PROGRESS；S2-02未开始；硬件profile仍UNVERIFIED |
| 入口 | `python -B scripts/build_firmware.py --target s201`；ARMCC5.05u1 / MDK-Lite5.14 |
| 产物 | `firmware/app/main.c`、`firmware/board/board.c`、板级资源说明、S2-01独立CLI及Keil工程；四任务休眠，无假健康进度；未验证profile和动作均拒绝 |
| 构建 | ARMCC CLI HSI16、HSE8/168均exit 0；各33编译单元，254依赖哈希匹配；ROM 5448/7028 B、主SRAM 17808 B。独立Keil工程两目标构建日志均0 Error、0 Warning |
| 设计检查 | `check_contracts.py`、`check_stage_plan.py`、`check_vision_contracts.py` 均exit 0；不证明硬件功能 |
| 下一卡 | S2-02不可开始；先完成S2-01的MOS默认关闭实物验收 |

## 实物观测与待核对项

用户报告 STM32、ST-LINK、MOS控制线和锁线已连接，12V 到 MOS DC± 尚未连接；10kΩ 电阻并联在 PWM 与 MOS 控制端 GND 之间，没有在此处接二极管。STM32上电、12V断开时，用户最终确认 PB6→MCU GND 与 MOS PWM→MOS GND 都约为0V；此前“PB6为3.3V”是用户更正的误报。全部电源断开后，用户确认 PB6 引出孔到 MOS PWM 螺丝端在通断档导通，PWM 到 MOS 控制端 GND 在电阻档读到约10kΩ，STM32 GND 到 MOS 控制端 GND 在通断档蜂鸣。12V未接时的0V读数只证明控制端现状，不证明锁上电默认关闭。

用户确认两只100nF均没有，A板470µF/25V已接好。正式电路有两处：A板保险后12V输入的470µF/25V与100nF并联于12V+和12V−；B板PC0分压节点另有100nF到地。此前仅指出PC0支路、认为MOS低电流测试可不装100nF的说明不完整，已向用户更正。用户授权先不验证两只100nF，继续独立低电流默认关闭测试；该测试不覆盖完整输入去耦或PC0采样。12V到MOS的线已接、OUT±跨接第二只10kΩ、真实锁拆离、PC0未接；12V适配器尚未通电。电源实际极性、保险丝和470µF极性仍待测量/照片核对。后续S2-02的ADC采样验收另行留证。

用户提供的[断电接线照片](mos-dummy-load-unpowered.jpg)显示测试电阻两端位于板背面标为OUT−、OUT+的焊点，用户确认真实锁已拆离，红线拟作12V+、黑线拟作12V−。照片不能验证正面端子夹紧、保险丝是否串在正极、470µF极性或电源实际极性；裸露电阻脚与线头需固定绝缘。拍照时尚未给12V通电，不能将视觉核对写作电气PASS。

后续用户报告已给12V线路通电，并用万用表电流档一笔跨红色12V+、另一笔跨黑色12V−得到0.33A。该接法短接电源，0.33A不是MOS/10kΩ负载电流，可能使保险丝熔断或电源保护动作，不能作为功率或默认关闭证据。已要求拔掉适配器，将红表笔移回VΩ、黑笔留COM，确认保险丝和先前“12V”是否为直流电压档实测；在核清前不再通电。

用户随后用直流电压档实测 MOS DC+ 相对 DC− 为+12V，真实锁保持拆离、10kΩ测试负载跨OUT±、PC0未接；PWM相对MOS控制端GND为0V、OUT+相对OUT−为29mV。用户随后明确 STM32 在该次读数时未上电，因此这组结果只证明 MCU 失电、MOS 12V供电时的稳态关闭，不能当成MCU上电或复位默认关闭验收。普通万用表也不能捕捉短暂瞬态。已要求先断12V、单独给STM32上电并重新测PWM；真实锁、功率能力和硬截止仍未验收。

随后断开MOS 12V、只给STM32 Type-C供电时，用户测得PWM对MOS控制端GND约0V。待两侧均上电后测OUT差分电压；不能把两次分别供电的结果拼作双电源闭环。

两侧同时上电（12V接、STM32由Type-C供电、真锁拆离、10kΩ跨OUT±、PC0不接）后实测PWM=0V、`OUT+`对`OUT−`=0.2mV，稳态默认关闭成立。随后做MOS输入电平排查：5V经1k注入（节点4.55V）时模块输入、输出指示灯均亮，`OUT−`对`DC−`=0V；3.3V经1k注入（节点3.0V）时不导通。去掉1k、3.3V直连（节点3.3V）后模块导通，`OUT−`对`DC−`≈0V、`OUT+`对`OUT−`≈12V，撤去注入恢复关闭，重复测试结果一致。结论：该模块输入阈值落在3.0~3.3V之间，1k串联电阻与10k下拉分压是此前不导通的直接原因；`PB6`可直连`PWM`，如需限流串≤100Ω，驱动高电平须≥3.2V，裕量仅0.2~0.3V，不能据此判定“3.3V兼容已验证”。该组数据来自台面3.3V注入，不等于GPIO实际驱动的电压与波形，后者待S2-02实测。

本轮 `Get-PnpDevice -PresentOnly` 显示 STM32 STLink 状态OK，但只读 pyOCD attach 在950kHz及100kHz时返回 `STLink error (9): Get IDCODE error`。用户随后发现ST-LINK杜邦线脱落并重新连接；再次尝试时pyOCD先报探针不可用，后报USB OUT超时。借鉴既有诊断的USB复位入口尝试打开探针，Windows返回访问被拒绝，复位尚未执行。用户保存关闭Keil并重插ST-LINK USB后，100kHz只读SWD成功：DBGMCU_IDCODE=0x101F6413、Flash容量寄存器1024KiB、PB6为输入模式且输入/输出位均为0。

在MOS 12V断开的条件下，先烧录HSI16安全镜像，再烧录HSE8/168安全镜像；两次均在擦写前核对既有Flash内容与原1MiB备份，限定第0扇区写入，镜像逐字节回读及第0扇区外保留核对PASS。[HSI原始结果](board-run.json)测得SystemCoreClock=16MHz、HAL/RTOS约997.6/997.4 tick/s；[HSE原始结果](board-run-hse8_168.json)测得168MHz、两者约998.2 tick/s。两次s201_fault=0，PB6维持输入模式且ODR bit6=0。HSI运行下将uwTick置于0xFFFFFFF0、高位设7，约120ms后读到低位108、高位8，复位恢复正常计时。HSE运行下在TIM6_DAC_IRQHandler入口设置硬件断点，PC准确命中，随后移除断点并复位运行。

HSE首次命令因手输SHA-256少一字符被镜像守卫拒绝，发生在Flash读取/擦写之前；改用正确SHA后通过。首个断点方案在main入口未命中（reset_and_halt未在入口前可靠停住），改在周期TIM6入口验证通过；失败均未被记作产品功能PASS。当前板上为HSE8/168安全镜像，无执行器驱动。

## 尚需验收

| 项目 | 当前结果 |
|---|---|
| 产品目标SWD下载/断点、Flash回读 | PASS：两时钟镜像逐字节回读、Flash尾部未变、TIM6硬件断点命中 |
| 上电锁控制默认关闭 | 双电源稳态PASS：12V接、STM32上电，PWM=0V、OUT差分=0.2mV；复位瞬态普通万用表捕捉不到，未验证。模块输入阈值3.0~3.3V，PB6可直连但须以≥3.2V驱动 |
| HAL时基/FreeRTOS异常入口 | 两时钟板上运行约1000 tick/s，TIM6/SysTick分工通过运行观察 |
| HSE8/168MHz与精确SKU | HSE目标板上运行于168MHz；Flash容量读出1MiB，精确SKU未独立证明 |
| DMA缓冲/CCM | 产品目标尚无DMA；scatter只用128KiB主SRAM，S2-03重新核算 |
| 64位时间底层计数回绕 | 板上注入通过：高位7→8、低位0xFFFFFFF0→108，复位恢复 |
| 未验证profile拒绝驱动 | 源码与ARMCC编译核对；板上未运行本目标 |

保留原HAL/锁定依赖及先前硬件、Keil未提交改动。12V未接、没有锁动作。一次性编译探针 `build/s201-board.o` 与首轮误命名的 `build/firmware-s201/hsi16/locker_env.*` 属忽略目录内的临时产物；自动审批拒绝了针对这些路径的清理命令，未改用其他方式绕过，后续收尾需清理。S2-01未满足全部验收，不作DONE/commit/push。当前开发者验证2轮（HSI/HSE）；实际耗时未精确计时。
