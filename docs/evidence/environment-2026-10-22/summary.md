# STM32开发环境验证

日期2026-10-22。范围为用户授权的环境准备，不执行45张产品任务卡，不改变任何阶段门禁。

## 板卡与工具判定

当前材料指向天空星STM32F407VGT6高配，商品型号LCKFB-LSPI-SkyStar-STM32F407VGT6-PRO（C32710423）。官方资源列1MB内部Flash、192KB SRAM、8MHz HSE、W25Q128；实物未购/未连接，具体引脚与外围profile保持UNVERIFIED。未把青春版VET6、GD32或筑基底板视为该核心板。

实际发现并执行E:/Keil5/ARM/ARMCC/bin工具，版本为MDK-Lite5.14/ARMCC5.05u1。已安装DFP2.13.0的pdsc包含STM32F407VGTx，并有STM32F4xx_1024.FLM；工程引用正确器件/Flash容量，不使用课程ZETx/512KB配置。

依据：[官方核心板资源](https://wiki.lckfb.com/zh-hans/fdb/skystar.html)、[商品型号](https://www.lcsc.com/product-detail/C32710423.html)、[Keil Lite限制](https://www2.keil.com/limits)。

## 实际产物与检查

- 固定FreeRTOS V11.2.0、littlefs v2.11.2官方提交，记录归档及254个依赖文件SHA、组件许可。
- 原库211个源码/头/启动文件与独立vendor快照逐字节一致，ZIP指纹保留于original-library-check.json，原库未修改。
- ARMCC两目标各实际编译32个单元、汇编、链接并导出HEX。HSI16环境ROM5372字节、RAM17816；HSE8→168环境ROM6952字节、RAM17816。
- 已执行uVision隐藏批处理构建两个目标，均0 errors/0 warnings；HSI输出Code4792、RO448、RW132、ZI17684，HSE输出Code6372、RO448、RW132、ZI17684。程序/头/配置信息由生成器维护，无旧工程源组。
- 链接map确认SVC/PendSV/SysTick来自FreeRTOS port，TIM6_DAC来自独立HAL时间基准，主数据区0x20000000起且小于128KB，不在CCM。
- 依赖再次校验、项目设计四项检查须保持通过；日志/summary JSON在本目录，构建产物仍在build下。

## 尚未验证及限制

无板卡烧录、调度计数观测、晶振测量、UART/DMA/ADC/SPI实物测试、Flash mount/掉电恢复、锁或业务功能测试。littlefs虽编译，未引用函数可被链接器剔除，不能宣称文件系统已运行。环境测试入口是正式诊断代码，不是临时业务mock。

MDK-Lite有32KB容量限制，基础目标通过不表示完整产品无限制；后续大固件需合法完整版或GNU方案。本轮访问Arm GNU下载站遇证书校验错误，未降低TLS验证或安装不明工具链；当前可运行入口是ARMCC/Keil，不声称已有GNU环境。

原HAL、BOM、硬件profile和E:/equipment-vision未修改；无产品卡标DONE，无自动烧录、提交、推送。用户请求的库已实际落盘，不再停留在下载建议。下一产品实现入口仍S1-01。
