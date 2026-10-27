# S1-01 VM 迁移与最终复验说明

日期 2026-10-23。本文件按 `docs/stages/S1-01-vm-final-review.md` 与
`docs/development/vm-ubuntu-workflow.md` 编写，说明主机端默认环境从 WSL2 切换到用户已有
VMware Ubuntu 之后，S1-01 的 POSIX 兼容核对、快照同步内容、实际改动与剩余 NOT_RUN。

本文件不替代 `summary.md` / `handoff.md`，不撤销 `audit.md` 的结论；本卡收尾为 DONE 的依据见
`summary.md` §9，阶段 1 仍 `IN_PROGRESS` / `gate_result=NOT_RUN`。

## 1. 四类证据必须分开看

| 证据批次 | 环境 | 覆盖的源码指纹 | 位置 | 有效性 |
|---|---|---|---|---|
| 旧 VMware 结果 | 同一台 VMware guest（Ubuntu 26.04.1） | **第一轮整改前**源码 | `vm-configure.log` `vm-build.log` `vm-ctest.log` `vm-unit-tests.log`（4 个） | 只覆盖整改前指纹，**不移植为整改版 PASS**；保留不覆盖 |
| VMware 第一轮整改后 | 同一台 VMware guest | 第一轮整改后（B1/B2/N1/N3） | `vm-final-*.log`（14 个） | 历史证据，保留不覆盖 |
| VMware 第二轮整改后 | 同一台 VMware guest | 第二轮整改后（B1-R/B3/N4，快照 sha256 见 §3） | `vm-final2-*.log`（15 个） | 第二轮历史证据，保留不覆盖 |
| **收尾轮 VM 复核（当前主证据）** | 同一台 VMware guest（`close-20261023-225902`） | 当前快照（与 `source-manifest.txt` 29/29 一致，`SRC_FILES=549`） | `vm-close-*.log`（8 个） | 本卡 DONE 时的**当前**证据；只做配置/构建/CTest/逐测试/设计检查，未重跑 sanitizer 与 mutation（沿用 `vm-final2-*`，因源码指纹未变） |

旧 `vm-*.log`、`vm-final-*.log`、`vm-final2-*.log` 未被覆盖或改名；所有 `vm-final2-*` 与 `vm-close-*` 均来自 VM 内执行，不是 WSL 结果换标签。

**2026-10-25 更新（用户 m19922 指令）**：原「WSL2 整改回归」一组的 12 个日志已从本目录删除，本卡 Linux 证据只剩 VMware 干净 guest 四组；表中原第一行（WSL2）已移除，历史结论不再引用 WSL2 产物。

## 2. POSIX 兼容核对（未重写任何已符合 POSIX 的基础代码）

按 `docs/stages/S1-01-vm-final-review.md` 要求"不重写已经符合 POSIX 的基础代码"，
本轮**没有**为了换环境改动 `host/common/` 任何一行源码、`CMakeLists.txt` 或正式测试。
逐项核对结果：

| 关注点 | 实际用法 | 结论 |
|---|---|---|
| 线程 | `pthread_mutex_t` + `pthread_mutex_init/destroy/lock/unlock`（`host/common/src/bounded_queue.c`）；CMake 用 `find_package(Threads REQUIRED)` + `Threads::Threads`（`host/common/CMakeLists.txt:20`） | 纯 POSIX，VM 配置打印 `Found Threads: TRUE` |
| 单调时钟 | `clock_gettime(CLOCK_MONOTONIC)`（`host/common/src/clock.c`） | POSIX，Linux 原生支持 |
| 墙钟 | `clock_gettime(CLOCK_REALTIME)` | 同上 |
| 随机源 | `getrandom(2)`，失败回退 `/dev/urandom` | Linux 专有 syscall + 标准设备节点，VM 同一内核家族可用 |
| 文件/路径 | 基础库不打开任何文件，无硬编码路径、无 `/mnt/e`、无 Windows 盘符 | 无 WSL 挂载路径依赖 |
| 时间格式 | 无 `strftime`/`localtime` 依赖，日志只用毫秒整数 | 无时区/区域设置依赖 |
| 编译器扩展 | C11 + `CMAKE_C_EXTENSIONS OFF`，`-Wall -Wextra -Wpedantic -Werror` 全绿 | GCC 15.2.0 下零警告 |
| 构建脚本 | `CMakeLists.txt` 无 shell 调用、无 `wsl`/`wslpath`/盘符路径 | 可直接在 Linux 文件系统构建 |

**结论**：源码本身已是 POSIX 实现，切换环境不需要改代码；本轮改动只有证据与文档。

## 3. 同步内容与固定指纹

VM 工作区布局按 `docs/development/vm-ubuntu-workflow.md` §独立目录布局：

```text
/home/%USER%/locker-workspace/
├── src/                                 旧快照（历史，不作本次依据）
├── S1-01/src/                           当前固定指纹源码快照（只放源码）
├── build/S1-01/<批次>/                  CMake 缓存、对象、库、测试程序
├── runtime/service/                     本轮未使用（基础库无 DB/socket）
├── runtime/simulator/                   本轮未使用
├── logs/S1-01/<批次>/                   环境/配置/构建/测试/探针/sanitizer 日志
├── artifacts/                           快照包与 sha256 清单、tmp
└── WORKSPACE-INDEX.md                   指明当前源为 S1-01/src、顶层 src 为旧快照
```

当前/历史批次：`build|logs/S1-01/20261023-214808`（第一轮）、`20261023-214841`（第二轮主证据）、
`asan-214858`、`tsan-214901`（sanitizer），以及收尾轮 `close-20261023-225902`。
**顶层 `src/` 不是当前源码**：`WORKSPACE-INDEX.md` 与 `config/development/host-environment.json`
（`vm_source_root=/home/%USER%/locker-workspace/S1-01/src`、`legacy_vm_source_root=/home/%USER%/locker-workspace/src`、
`canonical_source_promotion=PENDING_ACTIVE_CARD_HANDOFF`）都指向 `S1-01/src`；本卡未搬动、未覆盖任一源树。

- 目录由仓库自带脚本创建：`python3 -B scripts/prepare_vm_workspace.py --workspace "$HOME/locker-workspace"`，
  输出 `created_or_verified=true`、`sources_moved=false`、`build_or_tests_executed=false`
  （`vm-final-prepare-workspace.log`，`PREPARE_EXIT=0`）。
- `src/` 内**不含** build、DB、socket、临时日志：`src_stray_build_artifacts=0`
  （`vm-final-environment.log`）。
- 快照同步范围（Windows 主副本 → VM 本地 ext4）：`CMakeLists.txt`、`host/`、`tests/`、`contracts/`、
  `scripts/`、`config/`、`docs/`、`deployment/`、`firmware/`、`deliverables/`、`research/`、
  `.gitignore`、`AGENTS.md`、`README.md`。排除 `build/` 与用户只读的 `STM32F4xx_HAL_Code/`。
- **第二轮快照（当前）**：549 文件 / 26,717,272 B，`s101_snapshot2.tar.gz` 10,758,257 B，
  sha256=`8f6525c9cbf9b1919ce32e7ad01b48ce8c6afd26a2a1b2a1a4254371e18c36cf`；
  逐文件清单 sha256=`6727b61c04bfbad59453b220a59aae5a9507fe2226752478f9a6e3623d5fe873`（549 行）。
  VM 内 `sha256sum -c` 结果：`SHACHECK_EXIT=0`、**`SHACHECK_OK=549`**、`SHACHECK_FAILED=0`
  （`vm-final2-source-verify.log`）。
- 第一轮快照（历史）：541 文件，tar sha256=`6b75774a…6473`，清单 sha256=`c47814bc…f217`，
  `SHACHECK_OK=541`（`vm-final-source-verify.log`）。
- 文件系统：`workspace_filesystem=ext4 /dev/sda2`，即 VM 本地 Linux 文件系统，
  不是 `/mnt/hgfs` 共享目录、不是 `/mnt/e`。

**为什么快照包含 docs/scripts/config 而不只是能编 C 的最小树**：设计检查脚本会校验
`README.md` 里的本地链接（例如 `firmware/README.md`）。第一版最小快照导致 VM 内
`check_contracts.py` 报 `AssertionError: broken local link`（EXIT=1），
补全 `firmware/`、`deliverables/`、`research/` 后才具备跑全量设计检查的条件。
这是本轮真实遇到的失败，记录在案，未用"跳过该检查"掩盖。

## 4. 实际改动

### 4.1 源码 / 构建 / 测试：零改动

`host/common/`、根 `CMakeLists.txt`、`tests/unit/common/`、`deployment/host/` 本轮**没有任何修改**。
本卡作用域 29 个文件的 sha256 与 `source-manifest.txt` 逐条比对：`checked=29 mismatches=0`。
即本轮 VM 复验的是上一轮审计整改后的同一份源码，没有"为过 VM 而改代码"。

### 4.2 新增证据文件（本目录）

**第二轮（当前）**：`vm-final2-environment.log`、`vm-final2-source-verify.log`、
`vm-final2-configure.log`、`vm-final2-build.log`、`vm-final2-ctest.log`、
`vm-final2-unit-tests.log`、`vm-final2-probe-boundary.log`、`vm-final2-probe-b1-b2.log`、
`vm-final2-asan.log`、`vm-final2-tsan.log`、`vm-final2-fuzz.log`、
`vm-final2-nonvacuity.log`、`vm-final2-nonvacuity-r2.log`、`vm-final2-handoff-example.log`、
`vm-final2-design-checks.log`（15 个）。

**第一轮（历史，未覆盖）**：`vm-final-environment.log`、`vm-final-prepare-workspace.log`、
`vm-final-source-verify.log`、`vm-final-configure.log`、`vm-final-build.log`、
`vm-final-ctest.log`、`vm-final-unit-tests.log`、`vm-final-probe-b1-b2.log`、
`vm-final-asan.log`、`vm-final-tsan.log`、`vm-final-fuzz.log`、`vm-final-nonvacuity.log`、
`vm-final-handoff-example.log`、`vm-final-design-checks.log`（14 个）。

旧 `vm-*.log` 四个文件内容未变。

**收尾轮（当前，`vm-close-*` 8 个）**：`vm-close-environment.log`、`vm-close-source-fingerprint.log`、
`vm-close-configure.log`、`vm-close-build.log`、`vm-close-ctest.log`、`vm-close-unit-tests.log`、
`vm-close-design-checks.log`、`vm-close-index.log`。

### 4.3 文档

`summary.md`、`handoff.md` 增加 VM 复验章节；新增本文件；收尾轮补记 N5 勘误（任务卡 SHA、Git exit=128、
N2 已关闭、当前源路径 `S1-01/src` 与顶层旧 `src` 的身份）。三份文档都不改审计结论。

## 5. 本轮 VM 复验结果（全部来自 VM 内真实执行）

**收尾轮复核当前快照（当前主证据，`vm-close-*`，run `close-20261023-225902`）**：

| 项 | 命令要点 | 结果 | 日志 |
|---|---|---|---|
| 环境 | `date/uname/os-release/findmnt/cmake/gcc/make/ctest/python3/nproc` | Ubuntu 26.04.1 LTS、Linux 7.3.0-rc5 x86_64、ext4 `/dev/sda2`、cmake 4.2.3、gcc 15.2.0、make 4.4.1、ctest 4.2.3、Python 3.14.4、nproc=2 | `vm-close-environment.log` |
| 源码指纹 | `sha256sum` 契约源/生成头/log.c/test_log.c + 文件计数 | 四个 sha256 与审计记录一致；`SRC_FILES=549`、`SRC_STRAY_ARTIFACTS=0` | `vm-close-source-fingerprint.log` |
| 配置 | 全新目录 `cmake -S S1-01/src -B build/S1-01/close-… -DLOCKER_BUILD_HOST=ON -DLOCKER_BUILD_TESTS=ON -DLOCKER_WARNINGS_AS_ERRORS=ON` | `CONFIGURE_EXIT=0`、`CONFIGURE_WARNING_LINES=0`、`-- locker host build: contract 0.2.1`、`Found Threads: TRUE` | `vm-close-configure.log` |
| 构建 | `cmake --build … -j2` | `BUILD_EXIT=0`、`BUILD_WARNING_LINES=0` | `vm-close-build.log` |
| CTest | `ctest --test-dir … --output-on-failure` | `CTEST_EXIT=0`、`100% tests passed, 0 tests failed out of 6`，0.10 s | `vm-close-ctest.log` |
| 逐测试 | 6 个测试二进制直接运行 | 全 `EXIT=0`：97 / 5 / 53 / **164** / 133 / 53 = **505** 断言 | `vm-close-unit-tests.log` |
| 四项设计检查 | VM 内 `python3 -B scripts/check_*.py` | 4 个 `EXIT=0`、`status=PASS`（`c_declaration_syntax` 因缺 pycparser 为 NOT_RUN） | `vm-close-design-checks.log` |

收尾轮**只做**配置/构建/CTest/逐测试/设计检查：源码指纹与第二轮完全一致，故 ASan/TSan/fuzz/mutation/探针
沿用 `vm-final2-*` 结果，不重复重跑也不改名冒充新证据。

**第二轮整改后（`vm-final2-*`）**：

| 项 | 命令要点 | 结果 | 日志 |
|---|---|---|---|
| 源码指纹 | `sha256sum -c s101_snapshot2.sha256` | 549/549 OK，0 FAILED，`SRC_STRAY_ARTIFACTS=0` | `vm-final2-source-verify.log` |
| 配置 | `cmake -S src -B build/S1-01/... -DLOCKER_BUILD_HOST=ON -DLOCKER_BUILD_TESTS=ON -DLOCKER_WARNINGS_AS_ERRORS=ON` | `CONFIGURE_EXIT=0`，`-- locker host build: contract 0.2.1`，`Found Threads: TRUE` | `vm-final2-configure.log` |
| 构建 | `cmake --build ... -j2` | `BUILD_EXIT=0`，`BUILD_WARNING_LINES=0`（`-Werror`） | `vm-final2-build.log` |
| CTest | `ctest --test-dir ... --output-on-failure` | `CTEST_EXIT=0`，`100% tests passed, 0 tests failed out of 6` | `vm-final2-ctest.log` |
| 逐测试 | 6 个测试二进制直接运行 | 全 `PER_TEST_EXIT=0`：bounded_queue 97 / build_info 5 / clock 53 / **log 164** / numeric 133 / uuid 53 = **505** 断言 | `vm-final2-unit-tests.log` |
| B1-R/B3 探针 | `probe-boundary.c` 编译并运行 | `PROBE_BUILD_EXIT=0`、`LONG_KEY_SENSITIVE=1`、`LEAK_CASES=0`、`PROBE_EXIT=0` | `vm-final2-probe-boundary.log` |
| B1/B2 探针 | `probe-b1-b2.c` 编译并运行 | `PROBE_EXIT=0`、`B1_VARIANTS_LEAK=0`、`B2_17TH_LEAK=0` | `vm-final2-probe-b1-b2.log` |
| ASan+UBSan | 全量单测带 `-fsanitize=address,undefined -fno-sanitize-recover=all` | 全 0，6/6 通过，`ASAN_SANITIZER_REPORTS=0` | `vm-final2-asan.log` |
| TSan | 全量单测带 `-fsanitize=thread` | 全 0，6/6 通过，`TSAN_REPORTS=0` | `vm-final2-tsan.log` |
| 脱敏 fuzz | 固定种子 xorshift64，200000 次 | `FUZZ_FAILURES=0`、`FUZZ_MAX_OUT=511`、`FUZZ_SANITIZER_REPORTS=0` | `vm-final2-fuzz.log` |
| 非空洞性 mutation（第一轮 B1/B2） | 副本里重新破坏 B1/B2 | `MUTATED_TEST_LOG_EXIT=8`、`MUTATED_FAILED_CHECKS=7` | `vm-final2-nonvacuity.log` |
| 非空洞性 mutation（第二轮 B1-R/B3） | 副本里重新破坏单引号分支与完整键后缀匹配 | `MUTATED_TEST_LOG_EXIT=8`、`MUTATED_FAILED_CHECKS=12` | `vm-final2-nonvacuity-r2.log` |
| handoff 示例 | §3.2 fake 时钟示例编译并运行 | `HANDOFF_COMPILE_EXIT=0`、`HANDOFF_RUN_EXIT=0` | `vm-final2-handoff-example.log` |
| 四项设计检查 | `python3 -B scripts/check_{contracts,stage_plan,vision_contracts,feasibility}.py` | 全部 `EXIT=0`、`status=PASS` | `vm-final2-design-checks.log` |

**第一轮整改后（历史，`vm-final-*`）**：同结构，`PREPARE_EXIT=0`、541/541 OK、`CONFIGURE/BUILD/CTEST_EXIT=0`、474 断言（log 133）、ASan/TSan/fuzz 全 0、mutation 7 of 133 failed、handoff 示例 0/0、设计检查全 PASS。

环境（`vm-final-environment.log`）：`guest_hostname=ubuntu`、`Linux 7.3.0-rc5 x86_64`、
`Ubuntu 26.04.1 LTS`、`cmake 4.2.3`、`gcc 15.2.0-16ubuntu1`、`GNU Make 4.4.1`、`ctest 4.2.3`、
`Python 3.14.4`、`nproc=2`。

**注意 VM 与 WSL 的一处差异**：VM 内 `check_contracts.py` 输出
`"c_declaration_syntax": "NOT_RUN (optional pycparser unavailable)"`，而 Windows/WSL 上是
`"PASS (parse only, not compile/link)"`，因为 VM 的 Python 3.14 环境没装可选依赖 pycparser。
这不影响 `status=PASS`（其余断言全部通过），但说明"VM 上 C 声明语法解析"这一子项在 VM 内
**NOT_RUN**，不能声称它在 VM 上通过。

## 6. 剩余 NOT_RUN / BLOCKED（不因 VM 复验而消失）

| 项 | 状态 | 原因 |
|---|---|---|
| 更旧发行版（Debian 12 / GCC 12） | NOT_RUN | 只验证了 Ubuntu 26.04 + GCC 15.2.0；`cmake_minimum_required(VERSION 3.16)` 是声明下限，未实测 |
| CMake 3.16 真实下限 | NOT_RUN | 两台环境都是 CMake 4.2.3 |
| 32 位 / 交叉编译 | NOT_RUN | 只验证 x86_64 |
| 时钟失败路径故障注入 | NOT_RUN | 无自动化 `clock_gettime` 失败注入测试；契约端口无错误通道（见 `handoff.md` §3.5） |
| VM 内 C 声明语法解析 | NOT_RUN | VM Python 缺可选 pycparser（见 §5 注） |
| 第三方库构建（SQLite/CivetWeb/cJSON/libsodium/OpenSSL） | NOT_RUN | 只核验元数据与许可，未 vendor、未编译、未链接 |
| TLS 选型 | 未决 | 属 S1-05，`dependencies.lock.json` 中 OpenSSL 仅为候选 |
| Keil/ARMCC、Android、RK/NPU、UART/相机/柜锁真机 | NOT_RUN | 不属本卡，VM x86 结果不能代替 |
| `audit.md` 复核结论 | PASS（无阻塞项，N5/P3 文档勘误） | 三轮审计原文均未改写；本文件不代审计下结论，只登记其现状 |

## 7. 复现方法

在 Windows 主副本生成固定指纹快照（不含 `build/` 与用户只读 HAL 目录），传到 VM 后
（当前快照固定在 `S1-01/src`，顶层 `src/` 是旧快照，不要覆盖它）：

```bash
set -u
WS="$HOME/locker-workspace"
SRC="$WS/S1-01/src"          # 当前源；WORKSPACE-INDEX.md 已指明
RUN="$(date +%Y%m%d-%H%M%S)"
BUILD="$WS/build/S1-01/$RUN"; LOGS="$WS/logs/S1-01/$RUN"; mkdir -p "$BUILD" "$LOGS"
cd "$SRC"
sha256sum -c "$WS/artifacts/s101_snapshot2.sha256"   # 必须 549/549 OK
cmake -S "$SRC" -B "$BUILD" -DLOCKER_BUILD_HOST=ON -DLOCKER_BUILD_TESTS=ON \
      -DLOCKER_WARNINGS_AS_ERRORS=ON 2>&1 | tee "$LOGS/configure.log"
cmake --build "$BUILD" -j"$(nproc)" 2>&1 | tee "$LOGS/build.log"
ctest --test-dir "$BUILD" --output-on-failure 2>&1 | tee "$LOGS/ctest.log"
```

收尾轮的实际做法与此一致，见 `vm-close-*.log`（`RUN=close-20261023-225902`）。

ASan/TSan/fuzz/mutation/探针/设计检查的完整命令行见 `vm-final2-*.log` 各文件头部注释；
探针与 fuzz 源码、mutation 脚本都在本目录（`probe-b1-b2.c`、`probe-boundary.c`、`probe-scrub-fuzz.c`、`nonvacuity-mutate.py`、`nonvacuity-mutate-r2.py`、`handoff-example-compile.c`）。

## 8. 未做与边界

- 未改 VM 网络配置、未升级系统、未安装新软件包（VM 无外网，本轮也不需要联网）。
- 未把任何日志改名冒充另一种环境的结果；按用户 m19922 指令，WSL2 产物已整体删除，不再作为本卡证据。
- 未移动或删除旧源码副本、旧共享目录内容。
- 未 commit/push，未生成 `audit.md`；本卡已按证据标 DONE（见 `summary.md` §9），但未把阶段 1 标 PASS——阶段 1 的 `gate_result` 仍 `NOT_RUN`，其门禁卡是 S1-07。
- 本轮结束后清理了 VM 内临时脚本与快照包（`~/s101_*.sh`、`~/s101_snapshot*.tar.gz`、旧 `~/work/locker`、`~/s1_01_clean`、`~/audit_clean.*`）；`~/locker-workspace/S1-01/{src,build,logs,artifacts}` 保留为本轮 VM 工作区（不属仓库内容），日志已全部回传到 `docs/evidence/stage-01/S1-01/`。
