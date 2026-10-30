# S2-04 Journal

契约版本0.2.1。Journal复用锁定的littlefs v2.11.2，通过[W25Q128块设备](../drivers/flash/README.md)访问板载16 MiB NOR；没有RAM持久化替代、自动格式化或锁驱动。当前验证结果以[本卡报告](../../docs/evidence/stage-02/S2-04/summary.md)为准。

## 所有权与调用

Storage是文件系统和恢复状态的唯一所有者。平台先静态配置块设备，再以 `mcu_journal_attach` 绑定永久有效的 `lfs_config`、临界区、等待与Storage身份回调。`mcu_journal_init(config,&journal)` 只挂载既有文件系统，读取并验证检查点，再原子提交本次boot递增和BOOT事实；任何一步失败都不返回可用对象，不尝试format。挂载成功后、启动其他调用任务前绑定三个sink。

`mcu_journal_append`、`commit_intent`、`control`按值复制进8槽FIFO，QUEUED只说明入队。Storage周期调用 `mcu_journal_process_one`，Flash IO不持队列临界区。token非零，在队列与处理中均不得重复；回调收敛前调用方不能复用token。`committed`、`control_result`仅在Storage执行，参数借用至返回，消费者须复制到自己的有界队列；回调不等待、不递归调用Journal。`event_committed`仅发布本次实际新增且成功提交的事实，精确意图重试和RELEASE重试不重新发布历史事实。发送失败保留持久事件，禁止新动作，后续按原键补取。

同步 `read_page/ack` 也排入同一FIFO。非Storage调用方等待平台wait回调让出CPU，Storage调用方自行排空至该RPC完成。等待没有提前超时返回：请求持有调用栈上的completion，必须执行到完成后才能销毁该栈。同步API不能在ISR或sink回调内使用。Storage与依赖先于生产者启动、晚于全部生产者和同步RPC停止；停止时先排空队列，最后由Storage执行 `shutdown`。静态singleton与平台对象不在任务运行中重绑或释放。

## 持久格式与提交边界

文件 `journal`：384字节头、64个96字节事实槽、4字节IEEE CRC32，共6532字节；magic `LKRJNL01`，格式版本1。整数明确按小端编码，UUID按16字节复制；不保存C结构体填充、枚举本机宽度或指针。头包含设备UID字符串、epoch、boot、动作水位、ACK、活动意图/结果、最近已释放guard墓碑、连续性/丢失计数及boot_open标记。事实的设备/epoch由头补齐，96字节字段布局对应UART事实，持久文件不等于UART帧。

每次修改从已提交状态复制候选，写 `staging`，检查完整write、sync、close，再atomic rename替换 `journal`；成功后才替换内存状态及回token。意图、水位、活动上下文和COMMAND_ACCEPTED事实在同一文件提交。失败不发送成功回执，不回退已接纳水位；不确定的底层提交结果交由重启恢复核对，不自动重试动作。恢复拒绝CRC、版本、boot/seq断档、ACK非法位置及活动状态矛盾；非清洁停机后将当前epoch丢失计数持久化为UINT64_MAX、连续性失效并保持DEGRADED，禁止新意图；清洁shutdown只提交boot_open=false，不清任何历史故障。ACCEPTED/PULSE_STARTED跨boot变UNKNOWN_RESULT，已明确的终结结果保留。

RECOVER回当前已提交状态；BARRIER在同一FIFO确认最后观察append token，边界失败不能成功；FINALIZE验证epoch/当前boot/原session/operation/command/action，保留水位；PULSE_STARTED只能保守终结为UNKNOWN_RESULT。RELEASE拒绝ACCEPTED或仍PULSE_STARTED，持久清空后才成功，并保留最近释放guard以供精确重试。闭门、无脉冲、physical_revision及会话有效性的实物复核仍由S2-05 Control负责。

## 容量和错误

最小实现保留全部64个事实，不回收任何已ACK或未ACK明细。达到56条时禁新意图并报告FULL，余下8条保留给已接纳动作的后续事实；满64条后追加失败，连续性失效，重启若无法持久化BOOT则init失败。ACK本身持久化但不清事件、不清活动意图或水位。完整分段容量/ACK后回收策略归S4-01；当前容量是明确的有限底线，不把16 MiB总容量说成可无限记录。

IO、实际存满或回执投递失败锁存不可接纳新动作；实际追加事实失败递增当前丢失计数，无法持久化的故障由下次非清洁启动转换为持久未知哨兵；RECOVER/read仍可交付最后已知的已提交记录。有效但连续性降级的文件系统仍允许记录后续事实、ACK和受守卫的终结/释放，不能藉此重新取得新动作资格。一个动作的容量预检NO_SPACE不会吞掉预留的后续事实。未找到的cursor返回gap；错误设备/epoch或半零cursor拒绝；页大小1..4，历史页 `live_transport=false`。挂载损坏或连续性无效不能取得READY资格。

首次现场初始化使用内部 `provision`，必须先核验/备份现有NOR并得到现场授权，传入新epoch及真实设备UID；该函数只用于受限现场初始化，不由UART、HTTPS或挂载失败路径调用。独立SWD板测目标和工具见[板测说明](board-validation.md)。

## 复用依据

[littlefs官方文档](https://github.com/littlefs-project/littlefs)定义sync/close提交和rename原子性；使用现有BSD-3-Clause锁定源码与依赖SHA，不改原库。SPI块适配与库方案比较、W25Q官方命令依据在驱动README。全检查点方案用少量代码保持跨字段同提交，代价是每条事实写约6.4 KiB、容量保守；持续运行的磨损/回收由后续卡扩展，不能据此跳过真实掉电验收。

正式软件测试入口 `cmake -S tests/firmware/journal -B <VM外部构建目录> -DCMAKE_BUILD_TYPE=Debug`，随后build/CTest；包含真实littlefs算法上的NOR故障夹具、Flash协议夹具和pthread单所有者并发测试。它们不验证实板供电、电平、SPI时序或硬件锁截止。
