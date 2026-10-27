# S1-01 构建与基础设施 — 实施报告

日期 2026-10-23。范围：仅 S1-01（根构建、host/common 基础库、正式单元测试、deployment/host 依赖锁定）。本报告已包含两轮审计整改（B1/B2 → B1-R/B3）、VMware 干净 guest 第二轮复验，以及收尾轮对当前快照的 VM 复核（`vm-close-*`）。
本卡不是阶段 1 正式门禁；本报告不把阶段 1 设为 PASS，不把任何硬件能力标为已验证。

状态：**DONE**（2026-10-23 收尾）。实现、WSL2 与干净 VMware Linux 构建/CTest、两轮审计整改 B1/B2/B1-R/B3/N1/N3/N4 均已完成；最新独立审计结论 PASS、无阻塞项（`audit.md:5`），仅余 N5/P3 文档勘误，本轮已勘误并同步状态。本卡 DONE 不等于阶段 1 PASS。

## 1. card / contract

| 项 | 值 |
|---|---|
| 卡 ID | S1-01 构建与基础设施 |
| 契约基线（用户指定） | 0.2.1 |
| 契约实际版本（执行前从 contract-source 读取） | `contracts/contract-source.json` → `contract_version` = 0.2.1，`status` = REVIEW_DRAFT；与基线一致，未使用旧副本 |
| 生成头契约版本 | `contracts/include/locker_contracts.h` → `#define LOCKER_CONTRACT_VERSION "0.2.1"` |
| 契约源指纹 | `contracts/contract-source.json` sha256=14e938a6502806e1518ac9160569e99717e9062e622da60c3aee8b4fd369898f（66553 B） |
| 生成头指纹 | `contracts/include/locker_contracts.h` sha256=34306a869f92d2caece69c0c92a0eb84f2377ef0ff51af657c60ee7ad629340a（39925 B） |
| 任务卡指纹（当前） | `docs/stages/stage-01-task-cards.md` sha256=7f7561e83f9f614fd4ed226cefe3ce78ba367859a55ee92cd82212b25273fc93（16460 B）。三次指纹变化均可追溯：**开工时** sha256=7b922727…b05e（16083 B）；收尾前正文被同步更新（VM 默认环境/目录规则）后为 4c71d6c8…b136（16444 B，即 `audit.md:19` 记录的值）；收尾时本卡把第 3 行状态行改为「S1-01 已完成（DONE），下一卡 S1-02」后为当前值（16460 B）。三次都重读了正文，确认本卡产出、验收和禁止越界条款未放宽，未静默沿用旧副本 |
| 源码清单 | 本目录 `source-manifest.txt`：29 个文件（CMakeLists.txt、host/common/、tests/unit/common/、deployment/host/）逐文件 sha256+字节数，第二轮整改后已重算（2026-10-23T21:59:51+08:00）；清单文件自身 sha256=ad0ebe41cd8fba5d9a025a6633b227d4c3300f3e50aae20d8f44cd15ad295176 |
| 版本门禁 | 根 `CMakeLists.txt:44-61` 在配置期用正则同时读头与源，版本不一致直接 `FATAL_ERROR "Contract version drift..."`，防止静默使用旧副本 |

仓库不是 Git 仓库：`git status --short` 实测 **exit=128**（`fatal: not a git repository`），无法用 Git 证明历史变更全集，故所有指纹改用 SHA-256 并逐文件记录在 `source-manifest.txt`。

## 2. prerequisites

无前置实施卡。执行前已核对：

- `config/development/stage-plan.json` 开工前：`active_task="S1-01"`、`recommended_next="S1-01"`、S1-01 `status="NOT_STARTED"`、`depends_on=[]`。
- 工作区开工前不存在 `host/`、`tests/`、根 `CMakeLists.txt`，无本卡残留，无失败测试可删。
- `list_agents` 仅有 lead，无并发写本卡目录或共享文件的 AI。
- 本卡不新增对外接口；消费既有契约（`locker_contracts.h` 中的 `locker_status_t`、`locker_uuid_t`、`locker_clock_port_t`、`LOCKER_CONTRACT_VERSION`），未补造公共入口。

## 3. scope

### 修改文件（29 个，均在授权目录内）

| 路径 | 说明 |
|---|---|
| `CMakeLists.txt` | 根构建入口（新增，91 行）：选项、C11、契约版本漂移门禁、警告函数、子目录 |
| `host/common/CMakeLists.txt` | `locker_common` 静态库目标（新增） |
| `host/common/include/locker/common/bounded_queue.h` | 有界队列公共头（新增，81 行） |
| `host/common/include/locker/common/clock.h` | 单调/墙钟与可注入端口（新增，64 行） |
| `host/common/include/locker/common/uuid.h` | UUID 严格文本与 v4 判定（新增） |
| `host/common/include/locker/common/numeric.h` | u64 规范十进制（新增） |
| `host/common/include/locker/common/log.h` | 受限日志（新增，104 行；两轮整改后重写脱敏说明与线程约束） |
| `host/common/include/locker/common/build_info.h` | 契约版本查询（新增） |
| `host/common/src/bounded_queue.c` | 132 行实现 |
| `host/common/src/clock.c` | 191 行实现 |
| `host/common/src/uuid.c` | 111 行实现 |
| `host/common/src/numeric.c` | 87 行实现 |
| `host/common/src/log.c` | **623 行**实现（初版 486 行；两轮审计修复见第 6 节） |
| `host/common/src/build_info.c` | 7 行实现 |
| `tests/unit/common/CMakeLists.txt` | 6 个测试目标 + CTest 注册 |
| `tests/unit/common/test_support.h` | 零第三方依赖断言宏 |
| `tests/unit/common/test_bounded_queue.c` | 211 行 |
| `tests/unit/common/test_clock.c` | 207 行 |
| `tests/unit/common/test_log.c` | **560 行**（初版 329 行；新增 5 个回归测试 + 7 条键名变体） |
| `tests/unit/common/test_numeric.c` | 164 行 |
| `tests/unit/common/test_uuid.c` | 147 行 |
| `tests/unit/common/test_build_info.c` | 18 行 |
| `deployment/host/dependencies.lock.json` | 依赖锁定清单（全量重写，9704 B） |
| `deployment/host/README.md` | 依赖说明（重写，4962 B） |
| `deployment/host/licenses/civetweb-LICENSE.md` | 许可原文落盘 |
| `deployment/host/licenses/cjson-LICENSE.txt` | 许可原文落盘 |
| `deployment/host/licenses/libsodium-LICENSE.txt` | 许可原文落盘 |
| `deployment/host/licenses/openssl-LICENSE.txt` | 许可原文落盘 |
| `deployment/host/licenses/sqlite-PUBLIC-DOMAIN.txt` | 公有领域声明摘录（含出处） |

### 实现函数（公共 API，均声明于 host/common/include/locker/common/）

- 队列：`locker_bounded_queue_init / _destroy / _try_push / _try_pop / _peek / _get_stats / _size / _capacity / _element_size / _is_initialized`。
- 时钟：`locker_clock_real_monotonic_ms`、`locker_clock_real_wall_utc_ms`、`locker_clock_real_new_uuid`、`locker_clock_port_real`、`locker_clock_port_is_valid`、`locker_clock_port_monotonic_ms / _wall_utc_ms / _new_uuid`、`locker_deadline_after / _expired / _remaining_ms`。
- UUID：`locker_uuid_is_nil / _equal / _clear / _parse / _format / _is_rfc4122_v4`。
- u64：`locker_u64_is_canonical_decimal / _parse_decimal / _format_decimal / _text_compare / _add_checked`。
- 日志：`locker_logger_init / _deinit / _enabled / _log`、`locker_log_level_name / _backend_name / _key_is_sensitive / _scrub`。
- 构建信息：`locker_common_contract_version`。

### 越界项

无。未建业务状态、未编造 REST 路由、未实现锁驱动、未搭与正式类型不同的临时框架；未改 contracts/、scripts/、config/、firmware/、docs/stages/；未改 `deployment/firmware/dependencies.lock.json`。

## 4. reuse

本卡基础库**零第三方依赖**：仅 C11 + POSIX threads + `clock_gettime` + `getrandom`/`/dev/urandom`。因此没有引入、也没有 vendor 任何库；测试不引第三方测试框架（`tests/unit/common/CMakeLists.txt:4-5` 说明离线构建不得联网抓取），使用自写断言宏 `tests/unit/common/test_support.h`。

依赖锁定清单 `deployment/host/dependencies.lock.json` 为后续卡（S1-02 SQLite、S1-05 civetweb/cJSON/libsodium/TLS）预先锁定来源与许可，本轮**只核验元数据、未编译未链接未 vendor**：

| 组件 | 消费卡 | 锁定版本 | 不可变标识 | 许可 | 许可原文 |
|---|---|---|---|---|---|
| SQLite | S1-02 | 3.54.1 | 无 git tag，锁定 amalgamation 归档 sha256=0a9c8e34…d1f9（2990966 B） | Public Domain | `licenses/sqlite-PUBLIC-DOMAIN.txt` |
| CivetWeb | S1-05 | v1.16 | commit d7ba35bbb649209c66e582d5a0244ba988a15159 | MIT | `licenses/civetweb-LICENSE.md` |
| cJSON | S1-05 | v1.7.19 | commit c859b25da02955fef659d658b8f324b5cde87be3 | MIT | `licenses/cjson-LICENSE.txt` |
| libsodium | S1-05 | 1.0.22-RELEASE | commit 77e1ce5d6dee871c49ef211222ba18ef0c486bda | ISC | `licenses/libsodium-LICENSE.txt` |
| OpenSSL（候选，非选型决策） | S1-05 | openssl-4.0.3 | commit af1775b60dfa141a4ad762585052cabeb9f37e9e | Apache-2.0 | `licenses/openssl-LICENSE.txt` |

许可原文已复制进仓库并记录 SHA-256。CivetWeb 的 GitHub license API 返回 NOASSERTION，故 SPDX 以 `LICENSE.md` 正文（MIT + 内嵌 Lua 5.3 MIT 段）为准。

**关键限制**：`verified=true` 仅表示"元数据与许可已核对"，不表示这些库可被构建/链接。`unresolved` 明写：依赖尚未 vendor；TLS 选型属 S1-05；`verified=false` 的依赖不得链接（硬停止）；本文件不证明第三方构建可用。

## 5. validation

### 环境（两套，均为真实 Linux，非模拟）

| 环境 | 详情 |
|---|---|
| WSL2（工作副本） | Ubuntu 26.04 LTS / Linux 6.18.33.2-microsoft-standard-WSL2 x86_64 / cmake 4.2.3 / gcc 15.2.0-16ubuntu1 / GNU Make 4.4.1 / ctest 4.2.3 |
| VMware 干净 Linux（**验收主环境**） | Ubuntu 26.04.1 LTS / Linux 7.3.0-rc5 x86_64 / cmake 4.2.3 / gcc 15.2.0-16ubuntu1 / GNU Make 4.4.1 / ctest 4.2.3 / Python 3.14.4 / 2 vCPU / ext4（`/dev/sda2`）本地盘 |

干净环境做法（按 `docs/development/vm-ubuntu-workflow.md` 与 `docs/stages/S1-01-vm-final-review.md`）：Windows 主副本打成固定指纹快照（549 文件 / 26717272 B；`s101_snapshot2.tar.gz` sha256=`8f6525c9cbf9b1919ce32e7ad01b48ce8c6afd26a2a1b2a1a4254371e18c36cf`，逐文件清单 sha256=`6727b61c04bfbad59453b220a59aae5a9507fe2226752478f9a6e3623d5fe873`），scp 到 VMware guest 的 `$HOME/locker-workspace/S1-01/src`（ext4 本地盘，非 9p/drvfs 共享盘），`sha256sum -c` 549/549 OK 后才构建；build/logs/artifacts 是 src 的**兄弟目录**，不在 src 内产生任何构建产物（`SRC_STRAY_ARTIFACTS=0`）。

### 命令与结果

统一入口与 `docs/stages/stage-01-task-cards.md:125-135` 一致：

```sh
cmake -S . -B build/host -DLOCKER_BUILD_HOST=ON -DLOCKER_BUILD_TESTS=ON -DLOCKER_WARNINGS_AS_ERRORS=ON
cmake --build build/host -j2
ctest --test-dir build/host --output-on-failure
```

| 环境 | 步骤 | 退出码 | 结果 | 正式日志 |
|---|---|---|---|---|
| **VMware 干净 Linux（收尾轮复核当前快照，验收主证据）** | configure | 0 | `-- locker host build: contract 0.2.1`、`Found Threads: TRUE`、`CONFIGURE_WARNING_LINES=0` | `vm-close-configure.log` |
| 同上（收尾轮） | build（`-Werror`） | 0 | 6 个 .o + `liblocker_common.a` + 6 个测试可执行，`BUILD_WARNING_LINES=0` | `vm-close-build.log` |
| 同上（收尾轮） | ctest | 0 | `100% tests passed, 0 tests failed out of 6`，0.10 s | `vm-close-ctest.log` |
| 同上（收尾轮） | 逐测试 | 全 0 | 6 个 PASS，97/5/53/**164**/133/53 = **505** 断言 | `vm-close-unit-tests.log` |
| 同上（收尾轮） | 源码指纹 | — | `SRC_FILES=549`、`SRC_STRAY_ARTIFACTS=0`，契约/生成头/log.c/test_log.c 四个 sha256 与审计记录一致 | `vm-close-source-fingerprint.log` |
| 同上（收尾轮） | 四项设计检查（VM 内 `python3`） | 全 0 | 4 个 PASS（VM 缺 pycparser，`c_declaration_syntax` 为 NOT_RUN） | `vm-close-design-checks.log` |
| **VMware 干净 Linux（第二轮整改后）** | configure | 0 | `-- locker host build: contract 0.2.1`、`Found Threads: TRUE` | `vm-final2-configure.log` |
| 同上 | build（`-Werror` 打开） | 0 | 6 个 .o + `liblocker_common.a` + 6 个测试可执行，`BUILD_WARNING_LINES=0` | `vm-final2-build.log` |
| 同上 | ctest | 0 | `100% tests passed, 0 tests failed out of 6` | `vm-final2-ctest.log` |
| 同上 | 逐测试 | 全 0 | 6 个 PASS，共 **505** 项断言 | `vm-final2-unit-tests.log` |
| VMware（**第一轮整改前**，仅历史证据） | configure/build/ctest/逐测试 | 全 0 | 6 个 PASS、435 断言 | `vm-*.log`（4 个，未覆盖） |
| VMware（第一轮整改后） | configure/build/ctest/逐测试/ASan/TSan/fuzz/mutation | 全 0 | 6 个 PASS、474 断言 | `vm-final-*.log`（14 个，未覆盖） |

逐测试断言数（第二轮整改后，VMware 干净 guest 实测）：`common.test_bounded_queue` 97、`common.test_build_info` 5、`common.test_clock` 53、`common.test_log` **164**、`common.test_numeric` 133、`common.test_uuid` 53，合计 **505**（初版 435 → 第一轮 474 → 第二轮 505）。

四层 VM 证据严格分开、互不替代：`vm-*.log` 是**第一轮整改前**（435 断言），`vm-final-*.log` 是**第一轮整改后**（474 断言），`vm-final2-*.log` 是**第二轮整改后**（505 断言，含 B1-R/B3 修复），`vm-close-*.log` 是**收尾轮对当前快照的复核**（505 断言，8 份）。旧日志不覆盖，详见 `vm-migration-review.md`。

### 验收项 ↔ 测试映射

| 卡验收条款 | 证据 |
|---|---|
| 干净 Linux 环境可配置并构建基础库 | 在 VMware 干净 guest（`$HOME/locker-workspace/S1-01/src`，ext4，549/549 SHA 校验通过）configure/build/ctest exit=0：**收尾轮**`vm-close-configure/build/ctest.log`，第二轮 `vm-final2-*.log`；配置期即校验契约版本 |
| 队列满时明确失败 | `test_bounded_queue.c` `test_full_queue_fails_explicitly`：满后 `try_push` 返回 `LOCKER_STATUS_NO_SPACE`、`size` 不变、`stats.rejected_total==1`、且**不覆盖最老元素** |
| 消息生命周期不依赖调用方栈 | `test_bounded_queue.c`：`push_from_temporary_frame` 只在被调帧内构造消息，随后 `clobber_stack` 用 1024 B `0xA5` 覆盖该栈区，仍能取回完整副本（队列按值 memcpy 进调用方提供的 storage） |
| u64 零、最大值、越界、非法文本 | `test_numeric.c`：`test_zero`、`test_maximum`（UINT64_MAX 与 20 位缓冲不足）、`test_out_of_range`（18446744073709551616）、`test_illegal_text` |
| 日志不输出 token/密码 | 两轮整改后：`test_log.c` `test_secrets_never_reach_sink`、`test_key_deny_list`（含 X-Token/authToken/reader_card_uid/tls.private_key/clientSecret/raw_uid/set-cookie 7 条变体）、`test_scrub_free_text`、`test_scrub_credential_syntax`、`test_scrub_single_quoted_values`（B1-R）、`test_scrub_no_span_budget`（B2）、`test_long_key_suffix_is_sensitive`（B3）、`test_key_spelling_variants`；探针 `vm-final2-probe-b1-b2.log`（B1/B2）与 `vm-final2-probe-boundary.log`（B1-R/B3，`LEAK_CASES=0`）均归零；非空洞性见 `vm-final2-nonvacuity.log` 与 `vm-final2-nonvacuity-r2.log` |
| 可注入测试时钟，且测试替身不能通过公共 API 控制 | `clock.h` 只导出 `locker_clock_port_real()`，库不导出 setter；`test_clock.c` 在测试内部自建 fake 端口注入，生产路径恒为真实时钟 |
| 日志标明 backend | 行格式含 `backend=<simulator|hardware>`，`test_log.c` `test_line_shape` 校验 |

### 加固验证（整改轮新增，均为真实执行）

| 验证 | 命令要点 | 退出码 | 结果 | 日志（VMware 干净 guest） |
|---|---|---|---|---|
| ASan + UBSan | `-fsanitize=address,undefined -fno-sanitize-recover=all` | 全 0 | 6/6 PASS，`ASAN_SANITIZER_REPORTS=0` | `vm-final2-asan.log` |
| ThreadSanitizer | `-fsanitize=thread` | 全 0 | 6/6 PASS，`TSAN_REPORTS=0` | `vm-final2-tsan.log` |
| 确定性 fuzz | 固定种子 xorshift64，200000 次，混合敏感键/凭据/分隔符/引号，ASan+UBSan | 0 | `FUZZ_MAX_OUT=511`（缓冲 512）、`FUZZ_FAILURES=0` | `vm-final2-fuzz.log` |
| 非空洞性 mutation（第一轮 B1/B2） | 复制树，故意回退 B1/B2 两处缺陷后重跑 `test_log` | 0 / 测试非 0 | VM `MUTATED_TEST_LOG_EXIT=8`、`MUTATED_FAILED_CHECKS=7` | `vm-final2-nonvacuity.log` |
| 非空洞性 mutation（第二轮 B1-R/B3） | 同上，回退单引号分支与完整键后缀匹配 | 0 / 测试非 0 | VM `MUTATED_TEST_LOG_EXIT=8`、`MUTATED_FAILED_CHECKS=12` | `vm-final2-nonvacuity-r2.log` |

非空洞性是整改的关键自证：**测试能抓住重新引入的缺陷**，否则"PASS"只是空转。第二轮 mutation 的 12 条失败行（`test_log.c:328/329/334/339/344/349/354/355/361/380/390/391`）正好对应 B1-R 与 B3 的断言，说明新增测试不是摆设。

### 设计一致性检查（Windows 本机 `python -B`，仅证明设计产物一致，不代表实现/硬件）

| 命令 | 退出码 | 关键结果 | 日志 |
|---|---|---|---|
| `python -B scripts/check_contracts.py` | 0 | status PASS；contract_version 0.2.1；public_routes 24；internal_ports 82；private_rpc_methods 28；resolved_schema_refs 453；resolved_internal_refs 332；uart_requests 11；max_wire_payload 469；`c_declaration_syntax` "PASS (parse only, not compile/link)"；hardware_profile UNVERIFIED；nominal_bom_cny 198.59；business_or_hardware_tested false | `check_contracts.json` |
| `python -B scripts/check_stage_plan.py` | 0 | status PASS；stages 5；task_cards 45；**active_task null**；**implementation_cards_done 1**（S1-01）；runtime_or_hardware_tested false | `check_stage_plan.json` |
| `python -B scripts/check_vision_contracts.py` | 0 | status PASS；vision_schema_refs 58；camera_owned false；rknn_available false；model_inference_tested false；hardware_tested false | `check_vision_contracts.json` |
| `python -B scripts/check_feasibility.py` | 0 | status PASS；async_request_contexts 3；two_wire_break_detection NOT_CLAIMED；runtime_or_hardware_tested false | `check_feasibility.json` |

### NOT_RUN（本卡范围外，如实记录，不伪造 PASS）

- 未在更旧发行版（Debian 12 / GCC 12 / `cmake 3.16` 精确下限）实测；两套验证环境（WSL2 与 VMware guest）都是 Ubuntu 26.04 + GCC 15.2 + cmake 4.2.3，多轮复验只是同一对环境的重复。
- 未做 32 位 / 非 x86 平台验证。
- 时钟故障注入（`clock_gettime` 返回失败的路径）**未做自动化测试**：契约的 `monotonic_ms` 回调无错误通道（见第 6 节 N3），本轮只补了文档说明，未加故障注入探针。
- 依赖归档**未重新下载**复验 SHA-256（本轮只核对已落盘元数据与许可原文）；`verified=true` 仍只表示元数据已核对，不表示第三方库可构建。
- Keil/ARMCC 编译：本卡是 Linux host 侧，不涉及；已由 firmware/env 另行验证，不计入本卡。
- Android/手机端、RK/NPU 推理、UART/相机/锁真机实测：全部 NOT_RUN，不属于本卡验收。
- 第三方库（SQLite/CivetWeb/cJSON/libsodium/OpenSSL）的实际构建与链接：NOT_RUN（未 vendor，属 S1-02/S1-05）。
- 阶段 1 门禁 `gate_result`：NOT_RUN，本卡不是门禁卡。

> 说明：上一轮曾列「整改后代码未在 VMware 干净 guest 重跑」，第二轮整改后已由 `vm-final2-*.log`（15 份，含 configure/build/ctest/逐测试/ASan/TSan/fuzz/两轮 mutation/探针/设计检查）覆盖，该项已从 NOT_RUN 移出。

## 6. 审计整改（两轮：B1/B2/N1/N3 → B1-R/B3/N4）

`audit.md`（同目录，36387 B，审计方原文，两轮**均未修改**）：
- **第一轮**（16272 B）结论 FAIL，2 个阻塞项 B1/B2 + 3 个非阻塞项 N1/N2/N3。
- **第二轮**（36387 B，2026-10-23 21:33）结论仍为 **FAIL**：核销 B1/B2/N1/N3，但新增 2 个 P1 阻塞项 **B1-R**（单引号密码值原样留下）与 **B3**（归一化截断长字段名导致后缀漏判），另新增 P2 **N4**（handoff 把 card_uid 错归为 u64）；**N2 仍开放**（README/AGENTS 状态陈旧，本卡无权限改）。

两轮整改都遵循同一顺序：**先复现审计原文现象 → 最小修复 → 加正式回归测试 → 用 mutation 证明测试非空洞 → WSL 与 VMware 双环境重跑**。

### B1（P1）敏感值扫描提前结束 —— 已修复

**缺陷**：`Bearer part1.part2.part3` 只掩码到第一个 `.`，尾部 `.part2.part3` 明文进 sink；`{"password":"alpha beta"}` 在空格处停止，尾部 ` beta"}` 泄漏。

**修复**（`host/common/src/log.c`）：
- 新增 `is_credential_char()`：空白、引号、单引号、`,`、`;`、`&`、`}`、`]`、`)`、`<`、`>`、反斜杠之外都算凭据字符，因此 `.`、`~`、`+`、`/`、`=` 都被吃掉。
- 新增 `scan_credential_token()`、`scan_quoted_value()`（处理反斜杠转义；未闭合时掩码到结尾，fail closed）、`scan_unquoted_value()`、`is_auth_scheme()`（bearer/basic/digest）。
- 掩码时**保留键名与方案名、只替换值**（`writer_put_range` + `LOCKER_LOG_REDACTED`）。

### B2（P1）16 段掩码容量耗尽后原文放行 —— 已修复

**缺陷**：`LOCKER_LOG_MASK_SPANS`=16，第 17 个 `token=LAST_SECRET` 原样进 sink，scrub 不报耗尽、logger 返回 OK。

**修复**：删除 `mask_span_t` / `collect_mask_spans` / 两遍写回机制，改为 `scrub_stream()` 单遍边扫边写，**没有 span 预算**，不存在"用完了就放行"。`locker_log_scrub` 签名与语义不变（返回掩码段数）。

### B1/B2 回归测试（`tests/unit/common/test_log.c`，329→461 行）

- `test_key_deny_list` 增 7 条键名变体：`X-Token`、`authToken`、`reader_card_uid`、`tls.private_key`、`clientSecret`、`raw_uid`、`set-cookie`。
- `test_scrub_credential_syntax`：Bearer 带点号、引号内空格、转义引号、未闭合引号、JWT 形态、Basic 全部掩码。
- `test_scrub_no_span_budget`：24 个 `token=vNN` + `token=LAST_SECRET`，断言 `masked==25`、`LAST_SECRET`/`v23` 不出现。
- `test_key_spelling_variants`：4 类键名变体全为 `AUDIT_SECRET`，断言 sink 无泄漏且 `device_id`/`operation_id` 保留可记录。

**开发过程中的真实失败**（保留记录）：首版修复丢掉了引号值的**开引号**，`test_log` 报 `FAIL test_log.c:291: expected {"password":"[redacted]"}, got {"password":[redacted]"}`；改为 `writer_put_range(input, i, cursor + 1u)` 后通过。

### N1（P2）线程安全声明缺生命周期例外 —— 已修复（文档）

- `bounded_queue.h:16-23`：明确 `try_push/try_pop/peek/get_stats/size` 可并发；`init`/`destroy` 是**所有者独占的生命周期调用**，destroy 不得与其他调用重叠、必须先 join；并发 destroy vs push/pop 是 data race，不是可恢复错误。
- `log.h:34-37`：新增 Threading 段——logger 无共享可变状态、可多线程用但 sink 须线程安全；init/deinit 不得与并发 log 重叠，deinit 在最后一次 log 返回（或 join）之后。
- 补充说明 sink 的 `line` 是回调期间**借用的栈缓冲**，异步 sink 必须自行复制。

### N3（P2）handoff 时钟示例与失败状态漂移 —— 已修复（文档）

- `handoff.md` 的 fake 端口示例按真源签名重写：`uint64_t (*monotonic_ms)(void *user)`、`bool (*wall_utc_ms)(void *user, uint64_t *out)`、`locker_status_t (*new_uuid)(void *user, locker_uuid_t *out)`（`contracts/include/locker_contracts.h:134`），不再用位置初始化把函数塞进首字段 `user`。
- `clock.h:28-39` 如实写明：`monotonic_ms` 回调**无错误通道**，平台失败只能返回 0，且与真实 0（`CLOCK_MONOTONIC` 是 uptime）不可区分，故 `locker_clock_port_monotonic_ms()` 此时返回 OK+0；需要不 fail-open 的调用方须把"已知区间内 monotonic 未推进"当异常；给端口加状态通道属**契约负责人**变更，本卡不改 `clock.c`（改了会破坏 `locker_clock_real_monotonic_ms` 的公开语义）。
- **编译验证（不空洞）**：把 handoff §3.2 的示例原样抽成 `handoff-example-compile.c`，用 `-Wall -Wextra -Wpedantic -Werror` 编译并运行，`HANDOFF_COMPILE_EXIT=0`、`HANDOFF_RUN_EXIT=0`；同一轮再编译**旧的**写法（错签名 + 位置初始化），`OLD_EXAMPLE_COMPILE_EXIT=1`，报错正是 `initialization of 'uint64_t (*)(void *)' from incompatible pointer type` 与 `near initialization for 'fake.user'`——证明修的是真缺陷，不是把示例改到能过就行。证据见 `vm-final2-handoff-example.log`。

### B1-R（P1）单引号值原样留下 —— 已修复

**缺陷**：第一轮只把双引号当作引号边界，`note=password='alpha beta'` 这类单引号密码在遇到第一个单引号时被当作普通凭据字符或提前结束，值尾段落明文进 sink。

**修复**：`scan_quoted_value(input, length, start, quote, &terminated)`（`log.c:264`）接受**任一引号**并按其配对扫描；`log.c:350-353` 跳过键结束引号、`log.c:357` 判断值起始引号。未闭合单引号同样 fail closed（掩码到结尾）。掩码示例：`note="password='alpha beta'"` → `note="password='[redacted]'"`；`note='password'='[redacted]'` 亦正确。

**不误伤散文**：`it's a plain sentence` 原样保留（引号前不是敏感键），已加断言。

### B3（P1）归一化截断导致长键后缀漏判 —— 已修复

**缺陷**：第一轮用 63 字符定长缓冲做归一化后再匹配，超长键名（如 40 个 `k` 后接 `-Private-Key`）在缓冲边界被截断，后缀匹配失败，值原样进 sink。

**修复**：后缀匹配改走 `key_ends_with_n()`（`log.c:97`），**从完整原始键的尾部**逐个非分隔字符比较，键长无上限、时间 O(suffix)、内存 O(1)；只有"归一化等值匹配"仍用 63 字符缓冲，而等值表最长项 13 字符，故不会漏判。`log.c:89-189` 为整个键判定区。

### B1-R / B3 回归测试（`test_log.c` 461→560 行）

- `test_scrub_single_quoted_values`（`test_log.c:322`）：单引号、单双引号混用、键位单引号、未闭合单引号 fail-closed、散文不误伤。
- `test_long_key_suffix_is_sensitive`（`test_log.c:374`）：40 字符前缀 + `-Private-Key` 长键命中，值被掩码。
- 第二轮 mutation 把这两处修复回退后，**12 of 164 checks failed**（`test_log.c:328/329/334/339/344/349/354/355/361/380/390/391`），证明新增测试确实盯住这两个缺陷。

### N4（P2）handoff 把 card_uid 错归为 u64 —— 已修复（文档）

卡 UID 在真源里是 `card_uid_len` + `card_uid[10]` 的字节串（4/7/10 字节，`contracts/include/locker_contracts.h:119`、`:158`；`docs/architecture/uart-v1.md:44`、`:336-337`），仓储层用 `card_uid_hex`/`uid_hex` 表示（`contracts/internal-data-source.json:1936`、`:2868`），**不是 u64**。`handoff.md` §5 已把 u64 示例改为 `boot_id`、`event_seq`、`action_no`、`physical_revision`，并加引用块说明卡 UID 的真实类型。

### N2（P2）默认阅读入口状态陈旧 —— 已关闭（由有权限的负责人同步）

开发阶段本卡无权改默认入口，曾登记移交。收尾核对时实际文件已同步：`AGENTS.md:3`、`README.md:5,19,21,37`、`docs/stages/README.md:3,47` 均已写"S1-01 已实施/复核中、其余以机器索引为准"，不再声称 45 卡全部未开始。`docs/evidence/feasibility-2026-10-22/summary.md:3` 是 2026-10-22 的历史证据快照，按历史记录保留、不回改。**N2 关闭**（见 `audit.md:42` 的核销表）。

### N5（P3）交付说明中的旧指纹与旧待办 —— 本轮收尾已勘误

审计 N5 指出的三处漂移，本轮按实际值改正，历史批次数据一律保留不改标签：

| N5 位置 | 勘误前 | 当前实际值 |
|---|---|---|
| 本文件 §1 任务卡指纹 | 7b922727…b05e（16083 B） | 7f7561e8…fc93（16460 B），并注明开工时旧值与审计时的 4c71d6c8…b136（16444 B） |
| 本文件 §1 Git 检查 | exit=1、空输出 | exit=128，`fatal: not a git repository` |
| 本文件 N2 小节、§9 | N2 尚未修 | N2 已关闭（默认入口已同步） |
| `handoff.md` 待集成负责人段 | 默认入口仍"全部未开始" | 已改为已同步，N2 关闭 |
| `vm-migration-review.md` 目录图 | 顶层 `src/` 当作当前快照 | 当前快照为 `S1-01/src`；顶层 `src/` 是旧快照，不作依据 |
| `audit.md` 记录 `stage-plan` SHA-256 | 54de2f04…2f05（审计时值） | 收尾同步状态后为 9fddb9ec…75a5；审计原文不改写，该指纹属审计时点输入 |

## 7. behavior

- **队列成功路径**：`init` 后 `try_push` 复制元素进 storage 的 `(head+count)%capacity` 槽位，FIFO；`try_pop` 从 head 复制到调用方缓冲；环绕顺序正确（6 轮半容量 push/pop 校验）。
- **关键错误**：`init` 收到 NULL/容量 0/元素大小 0 → `LOCKER_STATUS_INVALID`；重复 init → `CONFLICT`；`pthread_mutex_init` 失败 → `IO_ERROR`。满 → `NO_SPACE`（计数 `rejected_total` 递增，不丢最老元素）；空 → `NOT_READY`。
- **时钟**：单调钟与墙钟均为真实 `clock_gettime`；deadline 加法饱和（`now > UINT64_MAX-timeout` → `UINT64_MAX`）；**无效 deadline 视为已过期**（fail closed）。UUID v4 用 `getrandom(2)`（EINTR 重试），失败回退 `/dev/urandom`；置 version/variant 位。
- **文本边界**：UUID 解析只接受 36 字符小写 canonical，失败**不写** `out_uuid`（不部分覆盖调用方值）；u64 解析拒绝空串、>20 位、前导零、非数字、溢出；`format` 先算位数再校验 `out_size >= digits+1`。
- **日志**：单行上限 `LOCKER_LOG_LINE_MAX`=512，栈上定长缓冲，超长截断并补 `...`；敏感键**归一化后**等值/后缀匹配（丢弃 `_` `-` `.` 空格 `:` `/` 再小写），命中则值替换为 `[redacted]`；自由文本由 `scrub_stream` **单遍无预算**掩码三类形态（`key=value`、`key: value`、`Bearer|Basic|Digest <credential>`），引号值与未闭合引号按 fail-closed 掩码到结尾；component/event/key 经白名单化，含空格值加引号；NULL 字段跳过不致命。级别过滤命中时不调用 sink（零副作用）。
- **脱敏的真实边界**（已写入 `log.h:29-32`）：这是防御纵深，不是数据流证明。**没有敏感键、也不符合 `key=value`/空格方案形态**的自由文本秘密（例如 "the card was 04A2B3C4"）按模式无法发现，调用方必须把秘密作为敏感键字段传入。
- **线程安全**：`locker_bounded_queue_t` 的数据访问持 `pthread_mutex`，可多生产者多消费者；`init`/`destroy` 为所有者独占生命周期调用（见 N1）。`locker_logger_t` 无共享可变状态，可多线程调用但 sink 须线程安全，init/deinit 不得与并发 log 重叠。

## 8. docs / residue

### 文档同步

- 本目录（共 56 个文件）：`summary.md`（本文件）、`handoff.md`、`source-manifest.txt`（29 文件）、`audit.md`（审计原文三轮均未改）、`vm-migration-review.md`；探针/工具源码 `probe-b1-b2.c`、`probe-boundary.c`、`probe-scrub-fuzz.c`、`nonvacuity-mutate.py`、`nonvacuity-mutate-r2.py`、`handoff-example-compile.c`；日志分四组——VMware 第一轮整改前（`vm-*.log` 4 份）、VMware 第一轮整改后（`vm-final-*.log` 14 份）、VMware 第二轮整改后（`vm-final2-*.log` 15 份）、**收尾轮 VMware 复核（`vm-close-*.log` 8 份）**，另 `check_*.json` 4 份。各组互不替代。**按用户 m19922 指令，原 WSL2 一组的 12 个日志已全部删除，Linux 证据只保留 VMware 干净 guest；`vm-migration-review.md` 中标注为 WSL2 的段落已同步改写为历史说明。**
- `deployment/host/README.md` 已重写为本卡真实依赖状态（零第三方）与阶段 1 依赖表。
- `config/development/stage-plan.json`：收尾后 S1-01 `status="DONE"`、`report="docs/evidence/stage-01/S1-01/summary.md"`、`active_task=null`、`recommended_next="S1-02"`；阶段 1 `implementation_status="IN_PROGRESS"`、`gate_result="NOT_RUN"`（本卡不是阶段门禁）。
- `README.md` / `docs/stages/README.md` / `AGENTS.md` 的"S1-01 实施/复核中"表述：收尾同步为已完成本卡、下一卡 S1-02（N2 关闭、N5 勘误）。
- `docs/development/vm-ubuntu-workflow.md`、`config/development/host-environment.json`：核对后与实际 VM 布局一致（当前快照 `S1-01/src`、顶层 `src` 为旧快照），本轮未改。
- 未改 `docs/stages/`、`docs/architecture/`、`contracts/`、`scripts/`、`firmware/`。

### 临时残留

本轮临时文件均在仓库外，交付前已清理并复核为 0（清理后逐项复核，不是"计划清理"）：
- 宿主 `%TEMP%`：`s1_01_*`、`s101_*`（快照树、tar 包、sha256 清单、验证/回归/探针/检查脚本、fuzz 与 mutation 脚本、`s101_closelogs/` 日志暂存）累计 61 项；最后一次清理 8 个脚本 + 1 个目录（日志已先与证据目录逐文件比对 `DIFF_COUNT=0` 才删），复核 `TEMP_RESIDUE=0`。
- WSL2：`/var/tmp/s101_*` 构建与 sanitizer 目录（11 项）已删除，复核 `0`。
- VMware guest：`~/s101_*.sh`、`~/s101_snapshot*.tar.gz`、`~/s1_01_*`、旧 `~/work/locker/`、`~/s1_01_clean`、`~/audit_clean.*` 及 `/tmp/s101_vm_*.sh` 已删除（复核 `0`）；`~/locker-workspace/S1-01/{src,build,logs,artifacts}` 与 `WORKSPACE-INDEX.md` 保留为 VM 工作区（非仓库内容，日志已全部回传）。
- 仓库内授权目录扫描：`.o`/`.a`/`.so`/`.exe`/`.obj`/`.tmp`/`.bak`/`.orig`/`.pyc` 共 0 个（`STRAY_ARTIFACTS=0`）；构建产物只在 `.gitignore` 已忽略的 `build/` 下。

### 不确定项

- 干净 Linux 验证的各轮环境（WSL2 / VMware 第一轮 / 第二轮 / 收尾轮）都是 Ubuntu 26.04 + GCC 15；未在更旧发行版（如 Debian 12 / GCC 12、`cmake 3.16` 下限）实测，最低工具链声明按 `cmake_minimum_required(VERSION 3.16)` 与 C11 保守给出，未经验证。
- 整改后代码已在 VMware 干净 guest 复验（`vm-final2-*.log`），收尾轮又对当前快照复核一次（`vm-close-*.log`，`SRC_FILES=549`、`SRC_STRAY_ARTIFACTS=0`）；但 VMware 的 Python 3.14.4 缺 pycparser，故 VM 内 `check_contracts` 的 `c_declaration_syntax` 为 `NOT_RUN (optional pycparser unavailable)`，与 Windows/WSL 的 `PASS (parse only)` 不同；整体 status 仍 PASS，这一差异不隐藏。
- 依赖归档本轮未重新下载复验 SHA-256（只用已落盘元数据与许可原文核对）。
- `locker_logger_t` 的跨线程并发写入只做了 TSan 下的既有测试运行，未做专门压力测试。
- 时钟失败路径（`clock_gettime` 返回失败）无自动化故障注入测试，只有文档说明。

## 9. decision

**DONE（本卡收尾）**。

依据与理由：

1. 最新独立审计结论为 **PASS、无阻塞项**（`audit.md:5`，含 N5/P3 非阻塞文档勘误）；前两轮 FAIL 保留为历史附录，不作为当前结论。
2. 审计核销表逐项确认 B1/B2/B1-R/B3/N1/N3/N4 已关闭，N2 因默认入口已同步而关闭（`audit.md:33-42`）。
3. 本轮收尾**只做文档勘误与状态同步**，未改源码、契约、验收预期：29 个作用域文件与 `source-manifest.txt` 逐条比对仍 `checked=29 mismatches=0`，`log.c` sha256=`1418a51f…a8ba`、`test_log.c` sha256=`7e8cf735…0f5f`，与审计记录一致。
4. 因代码与构建输入指纹未变，按 `S1-01-current-handoff.md` 复用已审计证据（`vm-final2-*` 15 份），并按用户"补最新整改源复验"要求在本轮对当前快照做了一次全新 VM 复验：`vm-close-*` 8 份（配置/构建/CTest/逐测试/设计检查/源码指纹），结果与 `vm-final2-*` 一致（505 断言、6/6 PASS）。
5. N5 的三处漂移（任务卡 SHA、Git 退出码、N2 状态、VM 源路径）已按当前实际值勘误，历史批次日志一律保留原标签。

同时按流程同步：

- `config/development/stage-plan.json`：S1-01 `status="DONE"`、`report="docs/evidence/stage-01/S1-01/summary.md"`、`active_task=null`、`recommended_next="S1-02"`、`activity="IMPLEMENTATION"`；阶段 1 `implementation_status="IN_PROGRESS"`、`gate_result="NOT_RUN"`。
- 默认入口 `README.md`、`docs/stages/README.md`、`docs/stages/stage-01-task-cards.md`、`AGENTS.md`、`docs/AI开发交接流程.md`、`docs/AI开发执行手册.md` 同步为"S1-01 DONE、下一卡 S1-02"。
- `python -B scripts/build_ai_execution_manual.py` 重新生成（`--check` 通过），因任务卡与手册正文变化导致 180 份指令的 `manifest.json` 指纹需要刷新。

**本卡不是阶段门禁**：阶段 1 的 `gate_result` 仍为 `NOT_RUN`，不因本卡 DONE 把阶段 1 设 PASS；S1-07 才是阶段 1 门禁。

NOT_RUN 项（旧发行版/最低工具链、32 位与非 x86、时钟故障注入自动化、第三方库构建与链接、依赖归档重下载、Keil/STM32/RK/Android/NPU/相机/UART/柜锁真机）均已在上文如实列出，不伪造 PASS。

下一卡：**S1-02 SQLite 仓储**（已由 `recommended_next` 指向；本轮不自动开始）。
