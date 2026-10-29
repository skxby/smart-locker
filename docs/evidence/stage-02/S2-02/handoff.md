# S2-02 交接

| 字段 | 当前事实 |
|---|---|
| 卡号 / 契约 / 状态 | S2-02 / 0.2.1 / DONE；PC0实物标定按用户授权后移，不记PASS；阶段2 gate NOT_RUN |
| 当前入口 | active_task=null、recommended_next=S2-03；S2-03未开始，本次授权停在其开发之前 |
| 复现命令 | build_firmware.py --target s202 --clock hsi16或hse8_168；generate_keil_s201.py --target s202；独立测试CMake入口tests/firmware/inputs；三项设计检查见checks.json |
| 退出码 | 两时钟ARMCC/ADC compile-only/三设计检查均0；VM本卡最终Debug 2/2、主机全量36/36退出0；本卡ASan/UBSan与TSan各2/2一轮通过 |
| 最终硬件 | PE0/PE1内部上拉，开门/无物连续5次1/1，闭门/有物连续3次0/0，规范与去抖一致；最终普通卡UID 63 A9 FB 06，先前钥匙扣 A2 1A 2B 07；Flash EF4018；SPI错误0；PB6仍输入/低 |
| 镜像 | board-input-pullups.json；HSE HEX SHA256=25a23854ed0f7c157b3d763dd45356893e90aa453f6d4701de07e1bdfb66a7ee；ROM 13904 B、RAM 20296 B |
| 失败路径 | 上拉前门磁游移已由内部上拉整改；0x82由明确版本兼容与寄存器回读/两卡实测验证；慢SWD改短暂停核采集；三条卡片在场标签经用户更正为未放卡，保留原标签和原数值 |
| 测试范围 | 主机fixture跑实际BSP传输函数，模拟HAL/RTOS用于并发和失败路径；不冒充目标板；sanitizer后仅上拉/fixture断言调整，按P2重跑Debug/全量CTest，不重复sanitizer |
| 未验 / 关闭位置 | PC0/100nF/分压/基准与毫伏标定：S2-08基础门禁前补证；7/10字节实物卡暂无，协议回归通过；Flash真实busy：S2-04；真锁/复位瞬态/硬截止：S2-05；外部10k接触及安装距离：S2-07 |
| 修改文件清单 | firmware/drivers/、board/peripherals.*与input_qualification.h、board.c/README、app/main.c、Keil S202/生成器/构建脚本、tests/firmware/inputs/、本卡证据及当前入口/阶段索引 |
| 残留 | 传输tar已清理；正式fixture、诊断、原始日志和测量保留；此前硬件/环境未提交改动不混入；不push |
| 实际耗时 / 轮数 | 19:48起至2026-10-29T20:33+08:00，约45分钟墙钟（含实物等待及会话中断，纯开发未单计）；4次上板镜像、6轮本卡Debug、2轮主机全量，ASan/UBSan及TSan各1轮 |
| 下一卡 | S2-03仅推荐；先读本卡debt、驱动README和资源表，再在新授权下开发UART DMA |

用户已授权PC0不阻塞本卡；固件仍ADC关闭、supply_valid=false，全局profile仍UNVERIFIED，不能据DONE认定整机READY。MOS 12V与真锁保持断开。完整范围、原始证据及限制见summary.md、debt.md、source-manifest.json。
