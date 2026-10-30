# S2-06 RK 真实串口、ABI 与首次局域网准备

日期：2026-10-30｜契约：0.2.1｜结论：**本卡完成**。
aarch64 静态二进制在板运行，DevicePort 走真实 `/dev/ttyS4`，HTTPS 局域网与证书 SAN 通过，
配对导出工具可用；业务借还闭环、物理断线重连属 S2-07／未验项。

## 目标机与产物

| 项 | 值 |
|---|---|
| 板卡 | RK3568（粤嵌），`aarch64`，Buildroot 2018.02-rc3-dirty，无 systemd |
| 串口节点 | `/dev/ttyS4` = `serial@fe680000`（UART4），`root:dialout 0660`，内核未配 DMA 按中断模式工作 |
| 内核控制台 | `console=ttyFIQ0`、earlycon `fe660000`（UART2），**不占用 ttyS4** |
| 网络 | DHCP `192.0.2.20/23`，网关 `172.100.1.1`；MAC 为运行时随机的本地管理地址（见未覆盖） |
| 二进制 | `locker-service` aarch64 静态，9639056 B，`bbaa20f72db9611920a3c…` |
| 管理工具 | `locker-admin` aarch64 静态，2696528 B |
| 固件 | `locker_s205.hex` 56012 B，`9b6032832c92636118d0c…`，第 0–3 扇区 |

## 入口与命令

| 用途 | 命令 |
|---|---|
| 交叉构建 | `cmake -S . -B build/rk3568 -DCMAKE_TOOLCHAIN_FILE=deployment/target/aarch64-linux-gnu.cmake -DLOCKER_BUILD_HOST=ON -DLOCKER_BUILD_NETWORK=ON -DLOCKER_BUILD_TESTS=OFF` |
| 主机回归 | 同配置去 toolchain，`-DLOCKER_BUILD_TESTS=ON`，`ctest` → **36/36**（含 runtime 集成）；纯库配置 27/27 |
| 部署 | 板侧 `wget -O /tmp/deploy.sh http://<PC>:8000/deploy.sh && sh /tmp/deploy.sh`（HTTP 通道，本卡新增） |
| 初始化 | `printf '***\n' \| /data/locker/locker-admin init --config … --username admin --tool-name A1 --password-stdin` |
| 启动 | `cd /data/locker && ./locker-service --config ./locker-service.json` |
| TLS 烟测 | `python docs/evidence/stage-02/S2-06/tls-smoke.py --host 192.0.2.20 --ca … --password …` |
| 配对导出 | `python deployment/target/export-pairing.py --cert … --device-id … --host … --out …` |
| 断接核对 | `python docs/evidence/stage-02/S2-06/disconnect-check.py --host … --ca … --password …` |

部署约定：`/data` 是 `→ userdata` 的软链接，`state_dir` 必须写真实路径 `/userdata/locker/state`
（`runtime_lock` 要求 `realpath` 与配置逐字符相等）。终止服务用 `TERM`；`kill -9` 会残留
`service.sock`，重启前需 `rm -f`。

## 实现改动

| 位置 | 内容 |
|---|---|
| `host/device/src/transport.c` | 新增真实串口后端：`/dev/` 路径走 termios 115200 8N1、raw、**关闭 CRTSCTS**（ADuM 不传流控线，开着会让写端永久等待）、无回显、VMIN/VTIME=0；读用 `poll+read`，写用 `write`；`shutdown` 对 tty 无操作（读线程靠超时唤醒）；打开失败不重试 |
| `host/device/src/port.c` | 取消 `simulated==false → NOT_READY`；改为“真实配置必须给 `/dev/` 设备，否则 INVALID”；`REQUIRED_CAPS` 见 CR-S206-1 |
| `host/runtime/config.c` | 两个模式 `SIMULATOR_ONLY` / `DEVICE_SERIAL`；字段 `sim_socket` → `device_path`；真实模式强制 `/dev/` 前缀 |
| `host/runtime/main.c` | `cc.device_simulated` 取自配置（原来硬编码 true，会把真实设备记成模拟）；启动/失败文案按模式输出 |
| `host/cli/main.c` | 初始化完成提示按模式输出 |
| `host/http/third_party/CMakeLists.txt` | 允许 aarch64，OpenSSL 目标按架构选 `linux-aarch64` + `--cross-compile-prefix`，libsodium 加 `--host` |
| `deployment/target/*` | 交叉工具链文件、设备模式配置样例、配对导出工具 |
| `firmware/tasks/assembly.c` | HELLO 能力位由 `0x03` 改为 `0x17`（补 CARD_READER、BOUNDED_PULSE） |

## 验证结果

| 验收项 | 结果 | 依据 |
|---|---|---|
| 产物 ABI 可执行 | PASS | `file` 显示 aarch64 static；板上启动并监听 8443 |
| HTTP→Core→真设备可追踪 | PASS | HTTPS 登录后 `/device` 返回 `device_id=000000000000000000000000`、`simulated=false`、`door=OPEN`、`item=ABSENT`、`observed_age_ms=15`、`journal_epoch=9628099c-…`、`boot_id=8` |
| 证书 SAN 匹配 | PASS | 部署证书 SAN = `DNS:locker.local, IP:192.0.2.20, IP:127.0.0.1`；信任该证书时登录 200 |
| 未信任被拒绝 | PASS | 系统信任库直连报 `CERTIFICATE_VERIFY_FAILED: self-signed certificate` |
| 配对文件无敏感字段 | PASS | 仅 5 字段，1329 B，DER 824 B；工具显式拒绝 password/token/private_key 字段名 |
| 串口断接不重放旧动作 | PASS | 物理拔线 5 s（轨迹见 `uart-trace-unplug.json`）：120 s 内 `execute_unlock` 恒为 0、`tick` 连续；关闭状态下所有动作被 `opened` 门禁判 `NOT_READY`（`test_device_port` 覆盖） |

## 本轮修复的缺陷

1. **HELLO 应答缺身份字段**：Control 构造的 HELLO 只填业务字段，`device_uid`/`capabilities`
   由 `assembly_transmit` 补；主机 `device_identity_matches()` 任一缺失即判 `BAD_PRECONDITION`，
   表现为 `device_id` 全零、模式永久 `RECONCILING`。
2. **能力位与必需位不一致**：固件广播 `0x03`、主机要求 `0x1f`。固件改为广播真实能力 `0x17`；
   主机要求同步改为 `0x17`，**故意不含 SUPPLY_ADC**（PC0 未标定，`supply_valid=false`），
   记为 **CR-S206-1**，PC0 标定落地时在同一处改回 `0x1f`。
3. 过程性错误（已撤）：一度在传输层用 `t->capabilities`(=0) 覆盖 assembly 填好的能力位。

## 未覆盖 / 风险

- **拔 UART 信号线对 tty 不可见**：实测 120 s 内 `hello` 恒为 2、无 `EIO/POLLHUP`，主机不知道线断过，
  靠请求超时才能发现；只有 USB 串口拔出才会产生链路错误。
- **设备关闭后无自动重开路径**（已补，见 `../recovery-preflight/summary.md`）：原实现只在启动时
  `locker_device_open` 一次，适配器因超时关闭后需重启服务。现已加入守护线程：连续超时判失效 →
  关闭 → 退避重开 → 3 s 内未建链则继续重试；物理拔线 40 s 实测恢复，`execute_unlock` 全程为 0。
- 业务借还闭环、真实开锁动作、No-Charge 等属 S2-07/S2-08。
- 设备当前 `mode=DEGRADED`、`continuity_ok=false`：MCU 侧 Journal 需补账，属真实状态，未用格式化掩盖。
- RK 的 MAC 为运行时随机本地地址，DHCP 续租可能变址；证书 SAN 绑定了当前 IP，换址需重签。
- 供电 `supply_mv=null`：PC0 未标定，与 S2-02 debt 一致。
