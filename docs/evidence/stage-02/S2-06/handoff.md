# S2-06 交接

| 字段 | 内容 |
|---|---|
| 卡号 | S2-06 RK 真实串口、ABI 与首次局域网准备 |
| 契约版本 | 0.2.1 |
| 状态 | DONE（ABI／部署／UART／TLS／配对可用；业务闭环属 S2-07） |
| 日期 | 2026-10-30 |

## 入口与命令

| 用途 | 命令 | 结果 |
|---|---|---|
| 交叉构建 | `cmake -S . -B build/rk3568 -DCMAKE_TOOLCHAIN_FILE=$PWD/deployment/target/aarch64-linux-gnu.cmake -DLOCKER_BUILD_HOST=ON -DLOCKER_BUILD_NETWORK=ON -DLOCKER_BUILD_TESTS=OFF` | exit 0，产出 aarch64 静态二进制 |
| 主机回归 | 去掉 toolchain，`-DLOCKER_BUILD_TESTS=ON`，`ctest` | **36/36** |
| 纯库回归 | `-DLOCKER_BUILD_HOST=ON -DLOCKER_BUILD_TESTS=ON`（无网络依赖） | 27/27 |
| 设计检查 | `check_contracts` / `check_stage_plan` / `check_vision_contracts` / `check_feasibility` | 四项 PASS |
| 固件 | `python -B scripts/build_firmware_gnu.py --target s205 --clock hse8_168 --toolchain-root <xpack>` | exit 0，56012 B |
| 烧录 | `python -B docs/evidence/stage-02/S2-06/flash-product.py --program --expected-current-sha256 <sha>` | 回读一致 |
| TLS | `python docs/evidence/stage-02/S2-06/tls-smoke.py --host 192.0.2.20 --ca build/http-deploy/tls/server.crt --password <pw>` | 三项 checks 全 true |
| 配对 | `python deployment/target/export-pairing.py --cert … --device-id 000000000000000000000000 --host 192.0.2.20 --out …` | 1329 B，5 字段 |
| 断接 | `python docs/evidence/stage-02/S2-06/disconnect-check.py --host … --ca … --password …` | 不重放 PASS；重连握手未验 |

## 关键结果

- aarch64 静态 `locker-service` 在板运行，`DEVICE_SERIAL listening`，HTTPS 8443 可用。
- `/device` 返回真实设备身份与物理状态（`device_id`、`journal_epoch`、`boot_id=8`、`door=OPEN`、`observed_age_ms=15`）。
- 未信任客户端被拒绝；配对文件只含 5 个公开字段。
- 修复 HELLO 身份字段与能力位两处缺陷；新增 `CR-S206-1`（主机暂不要求 SUPPLY_ADC）。

## 失败路径与保留记录

| 事件 | 处理 |
|---|---|
| 部署的二进制是改动前产物：tar 保留 Windows mtime，比构建产物旧，增量编译跳过 | 对改动文件 `touch` 后重编，并在二进制内确认含 `DEVICE_SERIAL` 字符串 |
| 交叉编译 OpenSSL 报 `crypto/cryptlib.h` 缺失 | 实为并行操作把同一 src 目录 `rm -rf` 后重解压，构建中途失源；非源码问题，重跑即过 |
| 服务报 `state directory unsafe` | `/data` 是 `→ userdata` 软链，`realpath` 与配置不等；`state_dir` 改 `/userdata/locker/state` |
| 再次启动报同错 | 旧进程未退、`instance.lock` 被占；`pkill` 后等待再起 |
| `kill -9` 后报 `IPC start failed` | 残留 `service.sock`；`rm -f` 后正常 |
| 首次 TLS 烟测 404 | 路由前缀是 `/api/v1/`，脚本补前缀 |
| 断接脚本 429 | 登录限流（5 次/分），改为登录一次复用 token |

## 未实现项（随卡交接）

- **物理断线重连未验**：需拔 UART 线复测“重连先握手、不重放”。
- 业务借还闭环、真实开锁、No-Charge 属 S2-07/S2-08。
- 设备 `mode=DEGRADED`、`continuity_ok=false` 未解除：MCU Journal 需补账，未格式化掩盖。
- RK 的 MAC 随机、IP 由 DHCP 分配；证书 SAN 绑定当前 IP，换址需重签。
- PC0 供电标定仍缺，`supply_mv=null`。

## 改动文件清单

`host/device/src/transport.c`、`host/device/src/port.c`、`host/device/include/locker/device/transport.h`、
`host/runtime/config.c`、`host/runtime/main.c`、`host/cli/main.c`、
`host/http/third_party/CMakeLists.txt`、`tests/contract/device/test_device_port.c`、
`tests/integration/runtime/test_lifecycle.py`、`tests/integration/runtime/test_stage_gate.py`、
`firmware/tasks/assembly.c`、`deployment/target/{aarch64-linux-gnu.cmake,locker-service.device.example.json,export-pairing.py}`、
`deployment/host/{README.md,simulator.example.json}`、`host/runtime/README.md`、
`docs/evidence/stage-02/S2-06/*`、`docs/hardware/RK3568板型与UART核对.md`。

## 度量

| 项 | 值 |
|---|---|
| 实际耗时 | 约 3 小时（12:25 起，含两轮缺陷定位与三次重编重烧） |
| 验证轮数 | 主机回归 3 轮；上板重烧 3 次；TLS 烟测 3 次；断接 2 次 |
| 整改回合 | 2（HELLO 身份字段、能力位不一致） |

## 下一卡

S2-07 一个柜格的完整实物闭环（依赖 S2-06）。前置建议：先做一次物理断线复测，再把真锁、
MOS、12V 按图册接入并做保护核对；`CR-S206-1` 需在 PC0 标定落地时同步改回 `0x1f`。
