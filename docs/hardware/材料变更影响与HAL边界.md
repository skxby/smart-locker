# 材料变更影响与 HAL 开发边界

日期：2026-10-23。输入为用户更新后的材料清单与项目计划；本次核对不代表已收到硬件、已验收商品或已完成固件。

## 1. 结论

借还业务、Android/RK职责、HTTP资源和UART v1消息格式保留。变化集中在板级配置、HAL适配、下载/日志通路、传感器极性、供电保护与实物验收门槛。

STM32明确采用 **STM32CubeF4 HAL + FreeRTOS**。公共领域类型与协议不依赖HAL句柄；所有HAL调用放在固件BSP/驱动目录，不用标准外设库工程替代，不另写业务层寄存器驱动。

## 2. 逐项影响

| 材料/条件变化 | 对架构/接口的影响 | 实施规则 |
|---|---|---|
| MCU改为待购天空星STM32F407VGT6高配核心板 | Cortex-M4/F407与既有领域/协议兼容；LQFP100与原先ZGT6等板型引脚不同 | 新建未验证硬件profile；禁止照搬课程原理图引脚 |
| 板载W25Q128 | 保留Journal/littlefs接口；Flash几何在驱动层配置 | 读取JEDEC并核对容量、扇区和页大小；不把“板载”理解为已格式化/可直接开锁 |
| DAPLink、虚拟串口的假设 | 官方核心板资料未把独立调试器列为板载资源，DAPLink有单独配套文档 | 预留外接SWD调试器与日志串口；主控Type-C不能自动等同独立CMSIS-DAP/CH340 |
| 高配板32.768kHz晶振、未装备用电池座 | 不改变协议时间模型；RTC不承担超时或动作去重 | 仍使用boot+单调时间+主机锚点；不强制购买CR1220，不另加RTC模块 |
| LY03、手动解锁尾杆 | 锁仍为短脉冲；不能从驱动电平推断锁舌状态 | 不增加虚构LOCK_OPEN_SUCCESS；无活动指令的开门/变位记录UNCOMMANDED_ACCESS并冻结新借还 |
| MOS模块替换 | 驱动高/低有效可能不同，不能固定“低=关闭” | profile记录有效电平，独立测试上电默认关闭、限时关断、续流和供电压降 |
| MC-38常闭候选、KW11类两线常开带线开关 | 只影响GPIO到规范状态的映射，不翻转API Door/Item枚举 | 实测门开/关、工具有/无的原始电平后填profile；不按NO/NC标题猜逻辑 |
| ADuM1201隔离板 | 领域/协议不变，改变UART布线与双电源域验证 | 该系列两侧供电为2.7–5.5V，不能直接作为1.8V端方案；核对RK IO域，不把隔离板当独立电源 |
| 电阻包、保险座、洞洞板被替换 | 原分压/保护子电路有漏项或错误候选组合 | 12V不能使用10k上臂/22k下臂；增加100nF、470µF/25V、实际保险管、端子与电源线核对 |
| 旧纸箱、无电钻、尼龙五金 | 不改软件，但改变物理证据的可信边界 | 只验收桌面功能样机；锁/开关加刚性支撑，纸箱不作为防盗结构；搬到硬柜后重做位置/极性/循环测试 |
| 有示波器、万用表、烙铁、RK原配电源 | 可执行波形与通断检查；不需额外采购测量工具 | GPIO通断用万用表；UART幅度/毛刺用示波器和原理图确认，不能用UART平均电压猜IO域 |

官方依据：

- [天空星核心板资源表](https://wiki.lckfb.com/zh-hans/fdb/skystar.html)：高配有W25Q128与LSE，排针赠送但未焊，USB接主控，VBAT座未贴装。
- [配套DAPLink](https://wiki.lckfb.com/zh-hans/fdb/lckfb-daplink-emulator.html)：独立仿真器连接天空星调试口；不能把套件配件当核心板板载。
- [ADuM1201厂商说明](https://ez.analog.com/cn/interface-isolation/f/q-a/102663/adum1200-and-adum1201-supply-voltage)：两侧供电范围，未覆盖1.8V。

## 3. HAL职责与资源分配

| 适配层 | HAL使用范围 | 上层看到的接口 |
|---|---|---|
| Board/Input | HAL_GPIO_ReadPin、去抖和有效性 | 规范Door/Item/UNKNOWN，不暴露GPIO电平 |
| Lock | HAL_TIM配置硬件one-pulse/等效定时输出、HAL_GPIO安全态 | configure/start_bounded_pulse/force_off；截止独立于任务调度 |
| UART Link | HAL UART + circular DMA + IDLE事件 | UART-v1字节端口，不把HAL callback等同业务帧 |
| SPI | HAL_SPI与独立CS，Flash/RC522串行化访问 | 有界事务、Journal提交与读卡事实 |
| Supply | HAL_ADC、实际量程与RC/标定 | supply_valid、毫伏数或UNKNOWN |
| Alarm | GPIO/定时节奏，模块输入电平配置 | set_alarm，不直接从业务层调用HAL |
| Platform | HAL时基、CMSIS与FreeRTOS移植层 | boot内64位单调时间、复位原因、任务进度 |

阶段2默认复用本地HAL V1.8.5，已经在源码确认ReceiveToIdle_DMA与RxEventCallback，不必为这些API升级或另下HAL。UART采用该Circular DMA/IDLE路径，版本/宏/工程调整见本地HAL核对；不维护第二套标准库实现。函数存在仍不等于工程已编译或硬件已验证。

HAL句柄在BSP/驱动私有头内；Linux、模拟器、领域核心与公共C契约不include stm32f4xx_hal.h。允许使用HAL官方宏以及FreeRTOS/CMSIS必需移植代码，不允许因此在业务层直接操作ODR、BRR等寄存器。

HAL毫秒时基使用独立于锁脉冲的TIM资源，FreeRTOS节拍与HAL时基按CubeMX方式配置并检查中断优先级。HAL callback中只交接计数/缓冲索引，用FromISR接口；不写Flash、不阻塞printf。扩展HAL_GetTick为64位单调时间，验证32位回绕。DMA缓冲只能放DMA可访问的SRAM，不能误放F407 CCM。

## 4. 配置、维护与兼容性

- [未验证profile](../../config/hardware/skystar-f407vgt6-pro.unverified.json)记录候选器件与待测电平/引脚，不具备驱动资格。
- [profile schema](../../contracts/hardware-profile.schema.json)和BSP配置端口固定字段。profile未校验、输入未标定或安全关断不合格时不得READY。
- public HTTP为24个入口，UART仍为11类请求、wire version=1；业务Door/Item枚举不随NO/NC采购改变。
- 内部新增BSP configure/alarm端口，以及仅本机管理员CLI使用的maintenance.reconcile。该维护只核对当前账本/实物并解除允许解除的hold，不开锁、不改原日志，不清Flash故障或未决操作。
- 设备资料变更不自动产生新journal_epoch。迁移/初始化日志仍需现场显式维护，旧事件/幂等/动作水位不可随意丢弃。

## 5. 阶段门槛

阶段1继续主机骨架与显式模拟器，不需要等待新板到货，也不做面包板试锁。

阶段2先做硬件与HAL配置任务，再驱动实际锁：确认ST芯片与板版号、焊接排针、调试器/控制/日志通路分开、Flash JEDEC、输入极性、MOS默认态与硬截止、ADC分压、UART双侧电平/掉电反灌、刚性安装。真实持久化动作意图必须在首次业务开锁前接入。

上述实际测试结果写入硬件验证证据后，才将profile从UNVERIFIED变更为VERIFIED。静态schema检查不能替代这些证据。
