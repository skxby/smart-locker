# S2-01 交接

| 字段 | 当前事实 |
|---|---|
| 状态 / 契约 | DONE / 0.2.1；阶段2门禁NOT_RUN；下一卡S2-02 |
| 入口与命令 | build_firmware.py --target s201，hsi16/hse8_168；check_contracts、check_stage_plan、check_vision_contracts；tests/firmware/board独立CMake |
| 退出码 | 两时钟ARMCC与三设计检查0；上板安全检查0；VM BSP Debug/ASan+UBSan各1/1，主机全量复验36/36退出0 |
| 失败路径 | SSH关闭后用户重启恢复；LF修正测试命令；首轮主机中断矩阵失败，复验通过，原始日志与debt保留 |
| 硬件 | 双电源稳态默认关闭通过；四项拒绝诊断=15，PB6输入/低；下载回读及Flash第0扇区外保护通过 |
| 未实现 | profile UNVERIFIED；S2-02输入/SPI/ADC，S2-03 DMA，S2-05真锁/复位瞬态与硬截止波形 |
| 修改文件 | app/main.c、board/board.c与README、hal/timebase_tim6.c与board/timebase.h、Keil S201工程/生成器、build_firmware.py、正式BSP测试、报告与当前入口/索引 |
| 残留 | 明确的旧临时对象和误命名产物已删除；正式证据、诊断脚本和原HAL保留 |
| 耗时与轮数 | 历史未精确计时；本轮约12分钟；新增诊断一轮、主机回归两轮 |

用户已授权继续S2-02，停于S2-03开始之前。当前MOS 12V断开、真锁拆离；STM32/ST-LINK供电，门磁/微动/RC522接好、PC0未接。传感器接好不代表功能通过；PC0标定必须补实物证据。详细结果见summary.md和board-safety-rejection.json。
