# ST-LINK连接诊断

日期：2026-10-28。范围：PC USB、ST-LINK/V2命令通信、SWD调试端口识别。没有Flash擦写、下载、Option Bytes修改、下载器固件升级或主动目标复位；未通过此诊断宣称S2-01或任何产品卡完成。硬件profile保持UNVERIFIED。

## 实际结果

| 检查 | 结果 |
|---|---|
| Windows PnP | STM32 STLink（0483:3748）和STM32 BOOTLOADER（0483:DF11）均Present/OK |
| 驱动 | 两设备均STMicroelectronics 1.1.0.0，Service=WinUSB；设备枚举不等于命令通信成功 |
| Keil安装 | E:/keil5；STLinkUSBDriver.dll 4.3.4.0、ST-LINKIII-KEIL_SWO.dll 2.0.14.0_KEIL，文件时间2014-12-09 |
| 进程 | 查询时5个UV4进程；3个无主窗口，另有F407课程工程和F429课程工程。没有强制结束任何进程；未证明哪一个独占设备 |
| 独立USB测试 | libusb可claim interface 0，但GET_VERSION命令的OUT阶段超时（USBTimeoutError 10060）；发生在进入目标SWD之前 |
| 下载器USB恢复 | libusb reset与clear_halt(0x02/0x81)成功，随后GET_VERSION回包294783044837，解码V2J37S7 |
| 下载器模式 | USB复位之后GET_CURRENT_MODE返回0；发送正常DFU_EXIT（F3 07，无升级）后返回1。此处是ST-LINK自身模式，不是目标STM32的DFU模式 |
| 独立SWD | 950kHz；设置频率/进入SWD均回8000；读DPIDR回800000007714a02b，ID=0x2BA01477；完成后退出调试模式并释放USB句柄 |
| Keil复验 | F407课程工程调试器原为ULINK2/ME，临时选ST-Link后端口原为JTAG/9MHz，显示No target connected；改成SW后在1.8MHz显示0x2BA01477、ARM CoreSight SW-DP，FW=V2J37S7 |
| 设置保存边界 | 内层ST-Link设置确定已点击；外层工程选项确定操作遇到用户输入，随后窗口被最小化，未确认外层设置已保存到磁盘。用户恢复该窗口后核对ST-Link Debugger/SW并点外层确定即可，不点Download或Start Debug |

## 判断

独立库和原Keil插件均已读到调试端口，三线SWD基本通信路径可用。USB通信曾处于异常状态，正常USB复位及退出下载器DFU后恢复；课程工程的ULINK/JTAG配置也与当前下载器/三线接法不匹配。

USB复位后出现DFU模式不能反推最初故障必然由DFU模式引起。没有证明2014年DLL是唯一根因（同一DLL最终已成功），也没有证明进程数量造成独占。当前不需要据此升级/降级固件、购买下载器或更换已通的三根线；若复发，再在保存退出其余Keil实例、探针单独接PC的受控条件下复测。

DPIDR只识别ARM调试端口，不证明精确STM32 SKU、Flash容量、时钟、板载Flash、FreeRTOS或锁硬截止通过。DFU连接、SWD连接与产品功能验收分别留痕。

## 工具与复用

在系统TEMP的独立locker-stlink-diag-packages目录临时安装pyusb 1.3.1、libusb-package 1.0.30.0、importlib_resources 7.1.0，未修改产品依赖或Keil安装。诊断代码由stdin执行，无一次性脚本写入仓库。命令端点和正常DFU退出/SWD读DP流程依据[pyOCD USB实现](https://github.com/pyocd/pyOCD/blob/main/pyocd/probe/stlink/usb.py)、[命令层](https://github.com/pyocd/pyOCD/blob/main/pyocd/probe/stlink/stlink.py)及[常量](https://github.com/pyocd/pyOCD/blob/main/pyocd/probe/stlink/constants.py)，未复制成项目运行代码。

维护入口：Options for Target → Debug → Use ST-Link Debugger → Settings → Port SW。只查看SW Device列表，不通过启动调试自动装载课程工程；F429工程不用于本F407板。

历史临时目录删除曾被策略拒绝。后续环境上板验证复用并补齐pyOCD0.46.0，工具目录已移至C:/Users/%USER%/Documents/locker-hardware-tools/pyocd-0.46.0正式保留；不在产品仓库或全局Python依赖中。
