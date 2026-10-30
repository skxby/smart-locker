# STM32环境上板验证

日期：2026-10-28。用户已授权覆盖原程序。Env_hsi16烧录、回读和软件复位运行验证通过；仅基础环境，不改变产品卡状态或profile的UNVERIFIED状态。

| 项目 | 实际结果 |
|---|---|
| 识别 | ST-LINK/V2、三线SWD，DPIDR=0x2BA01477；实读1024KiB，DBGMCU_IDCODE=0x101F6413，不独立证明精确SKU |
| 原程序备份 | C:/Users/%USER%/Documents/locker-hardware-backups/2026-10-28/stm32f407-original-121141.bin；1048576字节，两次完整读取与文件校验一致 |
| 备份SHA-256 | e893d0f0235dac60fa12d9b884486c65adf50a9d589e5ecaa45120f0b3df9ffa |
| 构建命令 | python -B scripts/build_firmware.py --clock hsi16；exit=0，ARMCC5.05u1/MDK-Lite5.14，32单元、254依赖哈希匹配 |
| 内存 | ROM5376字节，RAM17816字节 |
| AXF SHA-256 | b76f7f382ab9348c501423e6ff4b4667956b612fb453994604918684c8386b1b |
| HEX SHA-256 | 4dd05e740630de81ed4775a81715a558d68ba5b86fb094eb03a6668e406c3649 |
| 写入范围 | HEX覆盖0x08000000..0x080014FF（5376字节），只擦第0扇区16KiB |
| 回读校验 | 全部HEX字节一致；0x08004000之后1032192字节与原备份一致；不写Option Bytes、OTP或外部Flash |
| 软件复位运行 | SYSRESETREQ复位，显式暂停确认PC=0x0800025C，再运行；未测断电冷启动 |
| 6次运行采样 | 约6.031秒内task_progress=0→7，fault始终0，状态RUNNING，SystemCoreClock=16000000 |
| HAL/RTOS tick | HAL=2→6011、RTOS=1→6010，两者约996.35 tick/s；含主机采样开销，不是频率标定 |
| littlefs | 只读到版本常量131083；未mount或连接块设备，不证明文件系统运行 |
| HSE目标 | hse8_168重新编译通过，ROM6956/RAM17816；未烧录或验证HSE/168MHz |

最终原始结果见[board-run.json](board-run.json)、[START/EXIT/END日志](flash-and-observe.log)、[构建摘要](build-summary.json)、[构建日志](build.log)。构建摘要hardware_tested=false只表示构建步骤不含实测；板上结果取board-run.json。

## 修正与失败记录

firmware/env/main.c在HAL_Init前调用SystemCoreClockUpdate。供应商system文件初始变量为168MHz，实际启动为HSI16；旧顺序使首次HAL_InitTick算错TIM6分频。首版固件任务正常，但HAL tick约95/s；修正后HAL与RTOS均接近1000/s，未修改供应商原库或锁定依赖。

attempt5-board-run.json保留首版观测；其原始PASS只检查计数递增，不足以判HAL时基通过。当前诊断入口增加800–1200 tick/s验收，首版不满足。最终board-run.json为修正版。

attempt1..4保留初始化超时、复位后未暂停、擦除期间异常和写页失败的真实结果/日志。现场表明复位捕获不稳定，原程序PLL运行时旧FLM把Flash等待周期清零，且SysTick仍开启，出现异常访问。切回HSI并屏蔽中断后读数恢复；完整哈希确认当时仅第0扇区擦为FF，其余仍与备份一致。控制器重新复位后FLM Init解锁检查恢复；最终按擦除/写入分阶段复位、显式暂停完成。没有修改保护位或升级探针固件。

## 复现入口与保留材料

[flash-and-observe.py](flash-and-observe.py)为正式诊断入口，使用pyOCD0.46.0和本机Keil.STM32F4xx_DFP2.13.0的未修改FLM，不自写Flash算法。需显式--program和人工授权，默认拒绝执行；校验备份/镜像哈希、当前Flash内容后才擦写。

最终调用参数为--program --expected-current-sha256 cf6116f0be43f39efc85d0b4120da8c6394a963f4739c969b6581af352c7d4c4。这个hash只属于本次被替换的首版环境镜像，不能照抄作后续烧录依据。

工具从TEMP移至C:/Users/%USER%/Documents/locker-hardware-tools/pyocd-0.46.0正式保留；桌面捆绑Python的PYTHONPATH指向此目录。备份和工具在仓库外，不污染产品依赖锁。诊断入口、失败记录和备份均为正式材料，无本轮临时mock/业务接口。本任务不是产品卡交付，不自动commit或推进stage-plan。

## 未验证

断电冷启动、NRST硬复位、HSE、电源测量、RC522/传感器/UART/外部Flash、锁默认关闭与硬截止、RK链路及整柜借还均未验收。程序不配置外接GPIO输出。S1-04仍IN_PROGRESS，本结果不算S2-01或阶段二通过。
