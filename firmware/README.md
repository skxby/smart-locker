# STM32F407VG 开发环境

环境目标：天空星STM32F407VGT6高配核心板（ST芯片，LQFP100、1MB Flash）。这是独立的环境编译/调度测试工程，尚无借还柜业务、BSP引脚分配、锁驱动或Flash块设备；不代表S2-01实物验收完成。

## 使用入口

图形配置参考：[CubeMX F407VG逐项设置](../docs/development/cubemx-f407vg-config.md)。先在项目外生成独立参考工程，复用本项目锁定HAL/FreeRTOS，不覆盖当前环境工程；参考设置不代表已生成`.ioc`或完成BSP/锁驱动。

用Keil打开 [locker_environment.uvprojx](keil/locker_environment.uvprojx)，选择：

- Env_hsi16：内部HSI 16MHz，默认环境目标，不依赖外部晶振。
- Env_hse8_168：按官方高配版8MHz HSE配置PLL到168MHz；须到货核对晶振/板型并实测，不能将编译通过当振荡器运行通过。

本机命令行（在项目根目录，python可替换为实际Python路径）：

```text
python -B scripts/setup_firmware_dependencies.py
python -B scripts/build_firmware.py
python -B scripts/build_firmware.py --clock hse8_168
python -B scripts/generate_keil_environment.py
```

依赖已落盘，setup再次运行校验全部文件SHA，不覆盖已改动文件；首次网络获取固定官方提交，不跟随master。build默认使用E:/Keil5/ARM/ARMCC/bin，可用--keil-root指定其他安装目录；不修改系统PATH/注册表。产物在build/firmware-env/<clock>/，Keil产物在build/keil-env/，均与原课程工程分开。

## 已配置

| 项目 | 实际配置 |
|---|---|
| 工具 | 已安装Keil5.14、ARMCC5.05u1、STM32F4xx_DFP2.13.0；实际编译/链接通过 |
| 芯片/启动 | STM32F407VGTx，STM32F407xx宏，只有一个F407启动文件 |
| 链接 | Flash 0x08000000/1MB；数据仅放0x20000000/128KB主SRAM，CCM64KB暂不分配，避免DMA误放 |
| HAL | 本地HAL1.8.5只读复制；配置HAL核心/RCC/PWR/Flash/GPIO/DMA/ADC/SPI/UART/TIM/IWDG，未初始化外接引脚 |
| FreeRTOS | V11.2.0固定提交，RVDS/ARM_CM4F、heap_4、16KB堆；SVC/PendSV/SysTick归内核 |
| HAL时间基准 | TIM6 1ms独立时基；TIM6_DAC IRQ已实现，后续BSP不得重复占用TIM6 |
| littlefs | v2.11.2固定提交，源码编译、无malloc/stdio诊断；实际mount/掉电恢复未做 |
| 下载入口 | 预选本机ST-Link DLL、1MB Flash算法；不自动下载。接板后在Debug Settings核对SWD/频率/芯片ID与实际探针 |

FreeRTOS中断阈值为逻辑优先级5（编码80），最低15（编码240）；ARMCC5汇编使用无U后缀的数值。调用FromISR的外设优先级必须满足内核约束，不把本配置当所有外设中断已分配。HAL的USE_RTOS保持0。

环境程序不配置外部GPIO输出，不连接或挂载RC522、W25Q128、UART链路、相机或锁。调试可观察env_task_progress、env_littlefs_version、env_fault；这是保留的环境诊断入口，不是假业务成功。littlefs文件虽编译，未引用的函数由链接器删除，不宣称整个文件系统已在运行。

## 库、许可和原文件

third_party/stm32f4是原库源码快照；FreeRTOS/littlefs从官方固定提交获取，许可保留；CMSIS头自带许可声明并补官方Apache文本。版本/提交/归档SHA/254个文件SHA在 [dependencies.lock.json](../deployment/firmware/dependencies.lock.json)。用户原STM32F4xx_HAL_Code目录/ZIP及原Objects/Listings未修改。

复用依据：[ST HAL](https://github.com/STMicroelectronics/stm32f4xx-hal-driver)、[FreeRTOS](https://github.com/FreeRTOS/FreeRTOS-Kernel)、[littlefs](https://github.com/littlefs-project/littlefs)。FreeRTOS选已固定的V11.2.0作可复现基线，不称最新版；使用成熟移植层，未自写RTOS/文件系统。

## 板上验证入口

Env_hsi16已授权烧录并逐字节回读，软件复位运行约6秒内任务计数递增、env_fault=0，HAL与RTOS时基均约1000 tick/s。原程序1MB备份保留，其余Flash扇区未改变。HAL_Init前先更新SystemCoreClock，避免启动变量168MHz与实际HSI16不一致导致TIM6分频错误。见[上板验证记录](../docs/evidence/environment-board-2026-10-28/summary.md)。断电冷启动及HSE尚未实测；此记录不改变产品卡状态，硬件profile仍UNVERIFIED。

S2-01安全应用构建命令为 `python -B scripts/build_firmware.py --target s201`，HSE配置追加 `--clock hse8_168`；产物在 `build/firmware-s201/<clock>/`。独立Keil项目为[locker_s201.uvprojx](keil/locker_s201.uvprojx)，由 `python -B scripts/generate_keil_s201.py` 生成。该目标创建四个休眠任务、使用TIM6时基与64位毫秒计数，并对所有未验证BSP动作返回NOT_READY；没有业务UART、传感器或锁动作。资源和内存边界见[board/README.md](board/README.md)。两个配置均已编译链接、烧录回读并在板上运行；低电流负载双电源稳态默认关闭及拒绝驱动通过；完整profile、真锁和硬截止仍待后续卡验收，见[单卡报告](../docs/evidence/stage-02/S2-01/summary.md)。

## 工具与硬件边界

当前工具报告MDK-Lite，官方限制32KB程序容量；本环境小程序能构建不代表后续完整柜体固件永远能在Lite链接。超限时使用合法完整版或另配GNU Arm工具链，不缩减正式功能/保护逻辑，也不修改许可。本轮尝试访问Arm GNU下载站证书校验失败，未关闭TLS校验、未安装替代工具链；GNU构建当前不作为可运行入口。

板卡型号、HSE8MHz/W25Q128来自[官方高配资源表](https://wiki.lckfb.com/zh-hans/fdb/skystar.html)与[商品型号](https://www.lcsc.com/product-detail/C32710423.html)。用户已于2026-10-27报告器件全部到货，板型/电压/连接/调度实测以[到货记录](../docs/evidence/hardware-receipt-2026-10-27/summary.md)为准，目前未登记通过证据；不将筑基底板外设/引脚误当核心板板载资源。SWD排针需焊接、Type-C不是独立调试器/VCP；SPI实例、CS引脚、锁TIM输出和供电电平仍取实物与原理图证据，硬件profile维持UNVERIFIED。
