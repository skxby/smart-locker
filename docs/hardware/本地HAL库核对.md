# 本地 STM32F4 HAL 库核对

日期：2026-10-20。只读检查，原下载目录、ZIP和其中的例程/编译产物没有修改或删除。

## 1. 已确认

| 项目 | 本地证据 | 结论 |
|---|---|---|
| 路径 | `STM32F4xx_HAL_Code/` 与同名ZIP | 可作为用户提供的依赖基线 |
| HAL版本 | `STM32F4xx_HAL_Driver/Src/stm32f4xx_hal.c` 的版本宏 | **1.8.5** |
| CMSIS | CORE/cmsis_version.h 与 stm32f4xx.h | Core宏为5.6，设备层宏为2.6.11；不是据文件夹名猜版本 |
| UART DMA | uart.h声明与uart.c实现 | 有ReceiveToIdle_DMA与RxEventCallback，无需为此升级HAL |
| 定时器 | tim.h/tim.c | 有OnePulse接口；实际输出配置仍须示波器验证，函数存在不等于锁截止已实现 |
| 工程 | USER/project_demo/keil_project/*.uvprojx | Keil工程，记录ARMCC5.05；不是已经适配天空星的产品工程 |
| ZIP对应 | HAL主源文件与ZIP同路径内容比较 | 一致；不把旧Objects/HEX当本项目编译证据 |
| 原包未含 | FreeRTOS、littlefs、完整USB CDC | 原库不改；独立环境已补FreeRTOS/littlefs，见[环境入口](../../firmware/README.md)。本项目不依赖USB CDC，未引入该中间件 |
| 许可 | 本地未发现独立LICENSE，文件头引用组件许可证 | 后续引入正式版本时补齐官方许可文本与来源，不伪称已完成供应链核验 |

## 2. 现有工程与候选硬件差异

- 工程Device为STM32F407ZETx，IROM为512KB；候选VGT6为LQFP100、1MB。阶段2新应用工程调整器件、Flash算法、链接容量与引脚，不更改HAL源码适配型号。
- 配置HSE_VALUE=25MHz，main的PLL为M=25/N=360/P=2，并有F429注释；候选板官方HSE为8MHz，F407目标168MHz。不能复用这份时钟初始化直接驱动柜锁。
- 工程显式写了STM32F407XX，而本地头文件判断STM32F407xx。C宏区分大小写；须核对实际编译命令/Pack附加项并统一为正确宏，不能凭旧编译产物断言新工程能构建。
- 样例定义了SVC/PendSV/SysTick，接FreeRTOS后必须处理异常入口归属和HAL时基，避免重复定义或HAL tick停走。
- **USE_RTOS保持0。**本地hal_def.h明确在USE_RTOS=1时#error；这个宏不是启用FreeRTOS的开关。RTOS互斥/任务由应用适配层建立。
- 新固件仅引用必要HAL模块与一个F407启动文件；不glob全部Legacy驱动，也不混编另一版本Pack自带HAL。

## 3. 对当前架构与接口的影响

公开HTTP、UART v1、借还状态机不需要因该库更改。既定BSP端口仍封装GPIO、ADC、SPI、DMA、定时脉冲，HAL句柄不外泄。

默认复用本地HAL1.8.5原样快照。独立环境已锁定FreeRTOS/littlefs并通过ARMCC编译/链接，版本与命令见[环境说明](../../firmware/README.md)；本核对表的原库事实不等于真实外设/借还柜固件已验证。阶段2仍须完成应用适配、实物与资源测量。
