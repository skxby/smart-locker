# S2-04 独立 SWD 板测目标

构建入口：`python -B scripts/build_firmware_gnu.py --target s204 --clock hse8_168 --toolchain-root C:/Users/%USER%/Documents/locker-hardware-tools/xpack-arm-none-eabi-gcc-15.2.1-1.1`。HSI用 `--clock hsi16`。两版均39单元、254原依赖哈希和2补充文件哈希核对后编译/链接退出0；GNU HSE ROM34924/RAM60064B，HSI ROM32648/RAM60064B。成功产物位于 `build/firmware-s204-gnu/<clock>/locker_s204.{elf,hex,bin,map}`，同时生成symbols.txt、sections.txt、build.log和summary.json。此处只是编译/链接证据，实际烧录和掉电结果以本卡报告为准。适配来源与许可见[GNU构建说明](../../deployment/firmware/gnu-toolchain.md)。

入口 `validation_main.c` 只创建一个Storage任务，复用HAL/TIM6/FreeRTOS、`board_peripherals_init`，完整链接W25Q128、Journal、littlefs，不运行旧 `board_storage_task`、输入采样、UART或Control。启动只读核对W25Q128 JEDEC/保护状态，不自动挂载、格式化或擦写；未验证锁BSP仍拒绝驱动。

### ARMCC工具容量限制

`scripts/build_firmware.py --target s204` 保留ARMCC编译入口。正常 `-O2 -Ospace` 未删保护/功能；本机 `--help` 确认Ospace为Optimize for codesize。MDK-Lite32KiB下HSE34088B/L6050U、HSI33888B/L6047U；O3/Ospace为HSE34152B、HSI33932B，均退出1。首次日志在 `build/firmware-s204/<clock>/build-before-ospace.log`，O3在 `build-o3-ospace.log`。脚本保留较小的O2/Ospace，不使用超限失败产物烧录。

## 邮箱与操作

仅限独立SWD现场验收，不存在UART/HTTPS或业务公开调试入口。现场须先完整备份并核对原始Flash，确认真锁不接入，再执行受限初始化。命令2会格式化全部16MiB，不能用于掩盖挂载失败。

- `s204_command` 空闲为0。先写参数/data，最后写命令号；Storage完成后发布结果并置0。完成前不得改邮箱或同时发另一命令。
- `s204_address`、`s204_length` 是命令1的字节地址/长度，长度1..8192且不越过16MiB。
- `s204_data[8192]` 存原始读取结果；命令2读取前16字节非零epoch；命令10写入本机原生 `locker_event_batch_t` 副本。原生结构仅供同ABI SWD检查，不是UART协议。
- `s204_state` 是RECOVER复制出的持久状态；`s204_page` 是命令10首页；`s204_fault!=0` 表示断言/异常停机。
- `s204_diag[0]` 为奇偶发布序号。偶数且前后两次相同、`s204_command=0` 时才取一致副本；64位字段读低/高双字。

Storage持有静态Flash/Journal/port/sink生命周期。FIFO临界区仅保护队列/完成标志，不跨Flash IO；wait用vTaskDelay，owner同时拒绝ISR。独立目标只有Storage应用任务。SPI每条命令释放CS和互斥，传输timeout=50000us；HSI16、分频128下260字节约16.6ms，不能用5ms超时。

| 命令 | 动作与边界 |
|---:|---|
| 1 | 原始Flash只读分块备份；结果在data，检查diag[1]=OK |
| 2 | 未挂载时PROVISION；epoch取data前16字节，device_id为芯片96位UID按字节转24位小写十六进制；format、初始检查点、卸载 |
| 3 | 核对Flash→attach→MOUNT→启动检查点→bind→RECOVER；失败不format |
| 4 | append诊断HEALTH_CHANGED，detail=0x53323034（S204）；不伪造物理事实 |
| 5 | 诊断intent：当前epoch/boot、watermark+1，固定诊断session/op/command；没有锁驱动链路 |
| 6 | 当前活动intent FINALIZE REJECTED；目标从不驱动，失败保留原状态 |
| 7 | 当前活动intent RELEASE；不回退水位，无活动返回NOT_READY |
| 8 | ACK through当前last_committed；不删除未ACK记录 |
| 9 | shutdown，成功后清本地指针；不format |
| 10 | 从开头read_page最多4条，返回s204_page/data，历史live_transport=false |
| 11 | RECOVER持久状态，不重放动作 |

挂载后每条命令收尾额外RECOVER刷新状态。diag[1]保留命令本身结果；diag[24..25]为最后回调token/result，可能属于收尾RECOVER。token递增跳过0；排队接纳不算成功，处理后sink回执决定命令结果。

| diag下标 | 内容 |
|---|---|
| 0 / 1 | 发布序号 / 最近命令locker_status_t |
| 2 / 3 / 4 | Flash初始化结果 / 实读JEDEC / mounted |
| 5..6 / 7..8 | boot_id低/高 / watermark低/高 |
| 9 / 10 / 11 | has_active / command_state / 全部提交事件数（上限16页×4） |
| 12..15 | first_available.boot低/高、seq低/高 |
| 16..19 | last_committed.boot低/高、seq低/高 |
| 20..23 | ack_persisted.boot低/高、seq低/高 |
| 24 / 25 | 最后回调token / result |
| 26 / 27 / 28 | committed / event_committed / control_result次数 |
| 29 / 30 / 31 | journal_state / continuity_ok / 完成命令数 |

实物流程：只读完整备份及指纹核对→现场初始化→挂载→诊断事实/意图→不接锁掉电→重新挂载核查boot/watermark/UNKNOWN_RESULT→终结/释放→ACK→重启核查。实际掉电位置/恢复证据写本卡报告；软件NOR fixture不替代板上断电。
