# S1-03 UART 字节端口与独立模拟设备 — 实施报告

日期 2026-10-25。范围：仅 S1-03（`host/device/`、`host/simulator/`、`tests/unit/device/`、`tests/contract/device/`、根 `CMakeLists.txt` 四行最小接入）。
本卡不是阶段 1 正式门禁；本报告不把阶段 1 设为 PASS，不把任何硬件能力（真实 UART、GPIO 脉冲、littlefs/MCU 掉电）标为已验证。

状态：**DONE**（round-1～round-8 八轮审计 FAIL 已全部整改；round-9 独立审计 **PASS**，0 阻塞 + 4 非阻塞，`audit-round9.md`；4 项非阻塞已按 §7.5 整改）。实现、构建/CTest、ASan+UBSan、TSan、daemon 进程级冒烟在 VMware Ubuntu 干净 guest 上对同一份最终快照跑完（canonical run_id=`20261026-095548`）；mutation 非空洞性验证在同一源码批次的独立批次树上另跑（见 §5.4）。按用户 m18104/m19922 指令，本卡**不再使用 WSL2 作为任何证据来源**，全部 Linux 证据只来自 VMware guest。

## 1. card / contract

| 项 | 值 |
|---|---|
| 卡 ID | S1-03 UART 字节端口与独立模拟设备 |
| 契约基线（用户指定） | 0.2.1 |
| 契约实际版本（执行前从 contract-source 读取） | `contracts/contract-source.json` → `contract_version` = 0.2.1，`status` = REVIEW_DRAFT；与基线一致，未使用旧副本 |
| 生成头契约版本 | `contracts/include/locker_contracts.h` → `#define LOCKER_CONTRACT_VERSION "0.2.1"` |
| 契约源指纹 | `contracts/contract-source.json` sha256=14e938a6502806e1518ac9160569e99717e9062e622da60c3aee8b4fd369898f（66553 B） |
| 生成头指纹 | `contracts/include/locker_contracts.h` sha256=34306a869f92d2caece69c0c92a0eb84f2377ef0ff51af657c60ee7ad629340a（39925 B） |
| UART 布局指纹 | `contracts/uart-layouts.json` sha256=28fb958dc8905a55912be16fefeb5f13d1f8c0104008cba1e6adffee26cac0bd（15245 B） |
| 黄金帧指纹 | `contracts/examples/uart-golden.json` sha256=46438cc38eeb052af3709d0585c5c802fb2fd2482f321d87f6968399151b80f6（969 B） |
| 线协议规格指纹 | `docs/architecture/uart-v1.md` sha256=5bb2204f291b259f7b28cc7a2a1473352d5f1d0d501032168924d664c5e3ed8f（26022 B） |
| 模块所有权指纹 | `docs/architecture/module-ownership.md` sha256=b182fe6e424b605fd4cad1073c85178588ee474ca8ab6f38be84e3129f72bb3f（10135 B）；`host/device/` 归设备协议负责人、`host/simulator/` 归模拟器负责人 |
| 任务卡指纹 | `docs/stages/stage-01-task-cards.md` sha256=b8f5da9fcfeef3227569921cc4db6fd3fc81cc956308813475b16da38a4ab2e0（16485 B）；本卡正文位于该文件 :58-65，逐条对照实现，未放宽验收 |
| stage-plan 指纹 | `config/development/stage-plan.json` sha256=0be1a6c7e9c139221afb11a76df5a2d6b824550bf0f9ed764beeae9a72ef8b67（24779 B） |
| 源码清单 | 本目录 `source-manifest.txt`：**25 个文件**（根 `CMakeLists.txt` + `host/device/` + `host/simulator/` + `tests/unit/device/` + `tests/contract/device/`）逐文件 sha256+字节数；清单自身 sha256=7af887a219586547f7798dd20602070f94230aec3c99d0919a4e38a2fd56860e（2794 B），VM 端逐文件校验 **25/25 OK** |
| 版本门禁 | 根 `CMakeLists.txt:41-61` 配置期用正则同时读生成头与 contract-source，不一致直接 `FATAL_ERROR "Contract version drift..."`，防止静默使用旧副本（S1-01 已建立，本卡未改） |

仓库不是 Git 仓库：`git status --short` 实测 **exit=128**（`fatal: not a git repository`）。无法用 Git 证明变更全集，故全部指纹改用 SHA-256 逐文件记录在 `source-manifest.txt`。

## 2. prerequisites

前置卡 S1-01（DONE）与 S1-02（DONE）交付物被直接复用，本卡未重建任何框架：

- `locker_common`（S1-01）：`locker_uuid_*`（`host/common/include/locker/common/uuid.h`）、`locker_clock_port_*`（`clock.h`）、`locker_uuid_equal/is_nil` 供 `host/device`、`host/simulator` 与全部测试使用；未复制第二套实现。
- 根构建体系（S1-01）：`LOCKER_BUILD_HOST` / `LOCKER_BUILD_TESTS` / `LOCKER_WARNINGS_AS_ERRORS` 选项、`locker_apply_warnings()`、契约版本漂移门禁、`find_package(Threads REQUIRED)`。本卡只在 `CMakeLists.txt:88-89` 与 `:95-96` 各加两行 `add_subdirectory`。
- vendored SQLite 3.54.1（S1-02，`host/storage/third_party/sqlite/`）：`host/simulator` 通过 `PRIVATE` 链接 `locker_storage_thirdparty` 复用它，**没有**新下载、没有引入系统 `libsqlite3-dev`、没有第二份 SQLite。
- `tests/unit/common/test_support.h`（零第三方断言宏）被 `tests/unit/device/` 与 `tests/contract/device/` 通过相对 include 复用，未复制第二套 harness。

执行前核对：

- `config/development/stage-plan.json` 开工时：`active_task="S1-03"`、`recommended_next="S1-03"`、S1-03 `status="IN_PROGRESS"`、`depends_on=["S1-01"]`（机器索引只有 S1-01；S1-02 是本卡实际复用的既有代码，不是索引里的依赖边）、`implementation_cards_done=2`。
- 工作区开工前不存在 `host/device/`、`host/simulator/`、`tests/unit/device/`、`tests/contract/device/`，无本卡残留，无失败测试可删。
- `list_agents` 仅有 lead，无并发写本卡目录或共享文件的 AI；本卡未修改 `contracts/`、`scripts/`、`host/storage/`、`host/common/`。
- 本卡新增对外依赖为 **0**：只用 POSIX（`AF_UNIX`/`SOCK_STREAM`/`poll`/`pthread`）与已 vendored 的 SQLite。

## 3. scope

### 修改文件（25 个，均在授权目录内）

| 路径 | 行/字节 | 说明 |
|---|---|---|
| `CMakeLists.txt` | 97 / 4090 | **仅加四行**：`:88 add_subdirectory(host/device)`、`:89 add_subdirectory(host/simulator)`、`:95 add_subdirectory(tests/unit/device)`、`:96 add_subdirectory(tests/contract/device)`。其余未动 |
| `host/device/CMakeLists.txt` | 31 / 797 | 目标 `locker_device`（STATIC）；PUBLIC `locker_common`+contracts，PRIVATE `Threads::Threads` |
| `host/device/README.md` | 98 / 9763 | 模块说明：目录、构建接入、关键实现约定、线程/生命周期、已知边界 |
| `host/device/include/locker/device/wire.h` | 108 / 4303 | 帧常量、`locker_wire_frame_t`、流式解析器（模块私有头，别的模块不得包含） |
| `host/device/include/locker/device/codec.h` | 152 / 7048 | wire_result 枚举、flag/cap 位、11 对请求/响应 union、编解码 API（模块私有头） |
| `host/device/include/locker/device/transport.h` | 60 / 2553 | `locker_device_transport_t` 与 init/connect/shutdown/close/read/write（模块私有头） |
| `host/device/src/wire.c` | 243 / 7980 | CRC-16/CCITT-FALSE、组帧、流式拆帧（重同步、长度上限、CRC 校验；响应类型必须是 `request_type\|RESPONSE_FLAG`，0xc0 被拒） |
| `host/device/src/codec.c` | 1266 / 48259 | 11 类请求/响应 + DEVICE_EVENT 的显式字节编解码与字段校验 |
| `host/device/src/transport.c` | 267 / 8866 | AF_UNIX SOCK_STREAM 连接/读写/半关闭；`_POSIX_C_SOURCE 200809L` |
| `host/device/src/port.c` | 992 / 39388 | 契约 `locker_device_*` 十四个符号实现（13 个返回 `locker_status_t` + `destroy` 返回 void；单 reader 线程 + pending 表） |
| `host/simulator/CMakeLists.txt` | 45 / 1520 | 目标 `locker_simulator`（STATIC）与 `locker-device-sim`（EXECUTABLE） |
| `host/simulator/README.md` | 71 / 8821 | 明确「不是固件」、启动参数、控制命令表、独立 DDL、已知边界 |
| `host/simulator/include/locker/simulator/sim.h` | 119 / 5604 | 模拟引擎公共 API（生命周期/帧/控制/观测） |
| `host/simulator/src/sim.c` | 2642 / 99545 | 设备状态机 + 独立 SQLite 设备日志（sim_meta/sim_events/sim_commands/sim_commands_v2） |
| `host/simulator/src/sim_main.c` | 1065 / 36946 | daemon：两个 0600 AF_UNIX socket、帧切分、私有控制面 |
| `tests/unit/device/CMakeLists.txt` | 26 / 984 | 四个 C 测试，CTest 名 `device.*`，TIMEOUT 120 |
| `tests/unit/device/device_test_support.h` | 311 / 10664 | 注入测试时钟、帧捕获、sim fixture、`LOCKER_TEST_PAGE_LIMIT`（不复制 harness） |
| `tests/unit/device/test_wire.c` | 353 / 14631 | 组帧/拆帧/重同步/最大帧/handler 中止（130 断言，含 `DEVICE_EVENT\|RESPONSE_FLAG` 负例） |
| `tests/unit/device/test_codec.c` | 777 / 39632 | 11 类请求响应往返、字段校验、event96、错误响应形状（179 断言） |
| `tests/unit/device/test_sim_journal.c` | 883 / 42809 | 独立日志：boot/epoch/水位/重开/缺口/提交失败（461 断言，含 `COMMAND_ACCEPTED` detail=action_no 回归） |
| `tests/unit/device/test_sim_protocol.c` | 1436 / 69822 | 模拟器状态机全链路：会话/分页/ACK/RESUME/EXECUTE/RELEASE/事件队列（507 断言，含拒绝响应回显请求 session） |
| `tests/contract/device/CMakeLists.txt` | 44 / 2180 | 两个 C 契约测试 + Python daemon smoke（无 Python 则配置期 FATAL_ERROR，不静默少测） |
| `tests/contract/device/test_golden_frames.c` | 355 / 14849 | 黄金帧逐字节比对（168 断言） |
| `tests/contract/device/test_device_port.c` | 2070 / 87535 | 进程内 AF_UNIX server 驱动真 `locker_sim_t`，覆盖 port.c→transport.c→socket→sim.c（389 断言，含真实时钟烟测与 accept 队列满时的连接截止回归） |
| `tests/contract/device/daemon_smoke.py` | 1075 / 52611（1075 个 LF，末行无换行；`wc -l` 报 1075） | 驱动真 `locker-device-sim` 进程（391 条检查，含旧 session 拒绝回显请求 session、journal 路径长度与撞名守卫），因为 C 测试故意不链接 daemon |

合计 25 个文件 621200 B。

### 实现函数（对外只有契约里的十四个 `locker_device_*` 符号）

`host/device/src/port.c`：`locker_device_create`(:546)、`bind_sink`(:574)、`open`(:587)、`get_snapshot`(:677)、`execute_once`(:695)、`query_command`(:738)、`fetch_events`(:776)、`ack_persisted`(:806)、`resume_ready`(:832)、`release_context`(:867)、`send_health`(:898)、`time_anchor`(:924)、`close`(:949)、`destroy`(:981)。

模块私有（不在契约里、别的模块不得包含）：

- `wire.h`/`codec.h`/`transport.h` 的 API；`sim.h` 的模拟引擎 API 与观测 accessor。
- `sim_main.c` 的控制面命令（`status`/`door`/`item`/`card`/`reject_next`/`drop_ack`/`suppress_pulse`/`fail_journal`/`inject_gap`/`drop_link`/`restart`/`cursor`/`quit`）。

### 越界项（明确未做）

- 未创建任何公共 HTTP 模拟接口；控制入口只有 0600 的 AF_UNIX 私有 socket。
- 未接串口实物、未用真实 HAL、未做 GPIO 脉冲；`PULSE_STARTED` 只写计划毫秒数（`SIM_PULSE_MS=150`），**硬件脉冲只模拟计数**。
- 未把进程内 `restart` 或 SQLite 重开称为 MCU/littlefs 掉电验收。
- 未访问业务数据库；模拟器只写自己的 SQLite 文件。
- 未改 `contracts/`、`scripts/`、`host/common/`、`host/storage/` 一个字节。

## 4. reuse

### 第三方依赖

| 依赖 | 来源 | 许可 | 本卡用法 |
|---|---|---|---|
| SQLite 3.54.1 amalgamation | S1-02 已 vendor 到 `host/storage/third_party/sqlite/` | Public Domain | `host/simulator` PRIVATE 链接 `locker_storage_thirdparty`；**未新增下载** |
| POSIX sockets / pthread / poll | 系统 libc | — | AF_UNIX SOCK_STREAM、单 reader 线程、daemon 主循环 |

本卡**没有**引入任何新的第三方库，也没有把「先搭一个临时框架」作为过渡：`host/device` 与 `host/simulator` 直接用契约 DTO 与 S1-01/S1-02 的既有能力。

### 项目内复用

- `locker_common`：UUID 比较/格式化、注入时钟端口、`locker_status_t` 语义。
- `locker_storage_thirdparty`：SQLite 与 cJSON 编译单元。
- `tests/unit/common/test_support.h`：`TEST_CHECK`/`TEST_CHECK_STATUS`/`TEST_CHECK_STRING`/`TEST_CHECK_U64`/`TEST_REPORT`。
- `contracts/examples/uart-golden.json`：4 个向量（2 个合法帧、CRC 错误、超长 header）被 `test_golden_frames.c` 逐字节照抄成常量。

## 5. validation

### 5.1 命令与结果

**VMware 干净 guest（唯一 Linux 验收环境）**，run_id=`20261026-095548`，源码来自快照 `s103_snapshot.tar.gz` sha256=`07d7ab6e83b034512df21ca4cff4af70b111251a57dce6c832f8757ca2987f38`（84512317 B），解包到**独立批次目录**（活跃树 `~/locker-workspace/S1-03/src` 未被删除、未被当作解包目标；脚本在解包前与全部测试之后各取一次活跃树指纹，见 `vm-s103-extract.log` 的 `live_tree_fingerprint_before=`/`_after=` 与 `live_tree_unchanged=yes`），VM 端逐文件校验 25/25 OK、`SRC_STRAY_ARTIFACTS=0`：

```text
SRC=$HOME/locker-workspace/S1-03/batches/20261026-095548/src
BUILD=$HOME/locker-workspace/build/S1-03/20261026-095548
LOGS=$HOME/locker-workspace/logs/S1-03/20261026-095548
cmake -S "$SRC" -B "$BUILD" -DCMAKE_BUILD_TYPE=Debug -DLOCKER_BUILD_HOST=ON \
      -DLOCKER_BUILD_TESTS=ON -DLOCKER_WARNINGS_AS_ERRORS=ON
cmake --build "$BUILD" -j2
ctest --test-dir "$BUILD" --output-on-failure
```

| 步骤 | 退出码 | 关键输出 | 证据 |
|---|---|---|---|
| 环境 | — | Ubuntu 26.04.1 LTS / Linux 7.3.0-rc5 x86_64 / ext4 / nproc=2 / cmake 4.2.3 / gcc 15.2.0 / make 4.4.1 / ctest 4.2.3 / Python 3.14.4 | `vm-s103-environment.log` |
| 解包 | 0 | `EXTRACT_EXIT=0`；活跃树 `live_tree_files_before=1159`/`_after=1159`、`live_tree_fingerprint_before=9638dec8…9aeb`/`_after=9638dec8…9aeb`、`live_tree_unchanged=yes` | `vm-s103-extract.log` |
| 快照校验 | 0 | `MANIFEST_LINES=25`、`SHACHECK_OK=25`、`SHACHECK_FAILED=0`、`SHACHECK_EXIT=0`、`SRC_STRAY_ARTIFACTS=0`、`SRC_FILES=1174` | `vm-s103-source-verify.log`、`vm-s103-manifest.sha256` |
| configure | **0** | `CONFIGURE_WARNING_LINES=0` | `vm-s103-configure.log` |
| build | **0** | `BUILD_WARNING_LINES=0`、`BUILD_ERROR_LINES=0`（`-Werror` 下零警告） | `vm-s103-build.log` |
| ctest | **0** | `100% tests passed, 0 tests failed out of 19`，Total 9.84 s | `vm-s103-ctest.log` |
| 逐测试 | 全 0 | `TOTAL_ASSERTIONS=3551` | `vm-s103-unit-tests.log` |
| daemon smoke | 0 | `DAEMON_CHECKS=391`、`DAEMON_FAILURES=0` | `vm-s103-daemon-smoke.log` |
| ASan+UBSan | 全 0 | 19/19 Passed（11.84 s）、`ASAN_REPORTS=0` | `vm-s103-asan-{configure,build,ctest}.log` |
| TSan | 全 0 | 19/19 Passed（12.09 s）、`TSAN_HITS=0` | `vm-s103-tsan-{configure,build,ctest}.log` |
| 四项设计检查 | 全 0 | 见 5.3 | `vm-s103-design-checks.log` |

**快照与文档的时序**：快照在文档最终定稿前打（与前几轮同一做法）——`summary.md`/`handoff.md` 里的 `run_id`、快照 sha256、耗时等数字是跑完后回填的，因此快照内的这两份文档是上一版。可核性不依赖这一点：25 个受管**源码**文件的 `source-manifest.txt` 在快照内校验 `SHACHECK_OK=25`、`SHACHECK_FAILED=0`，而项目内这 25 个文件对同一清单复算 `N=25 BAD=[]`，所以「日志对应的源码 == 交付的源码」是逐字节可验证的；文档本身不是受管源码文件，也不在清单内。

脚本自带门禁：`evidence-run-vm.sh` 结尾逐项校验 **17 个门禁**（configure/build/ctest/asan×4/tsan×4/daemon×2/manifest/stray-artifacts/live-tree-unchanged/design-checks）。其中 15 项经 `need_zero()` 读日志里的 `KEY=0`，`live-tree-unchanged` 单独比较运行前后的活跃树指纹（文件数 + 内容 sha256 双条件），`design-checks` 单独校验四项设计检查退出码；每项写 `GATE_OK <label> …` 或 `GATE_FAILED`。逐项结果**只写 `vm-s103-gates.log`**，`vm-s103-run-summary.log` 用 `cat "$GATES"` 内联同一份结果后再追加 `VM_RUN_VERDICT`，任一非零即 `VM_RUN_VERDICT=FAIL` 并 `exit 5`。本次 `vm-s103-run-summary.log` 末尾是 `VM_RUN_VERDICT=PASS`；`SSH_EXIT=0` 只出现在启动器终端，**未归档**，归档证据以 `VM_RUN_VERDICT` 为准。

**断言数（VMware guest 实测，逐测试来自 `vm-s103-unit-tests.log`）**

| 测试 | 断言数 |
|---|---|
| `common.test_bounded_queue` | 97 |
| `common.test_build_info` | 5 |
| `common.test_clock` | 53 |
| `common.test_log` | 164 |
| `common.test_numeric` | 133 |
| `common.test_uuid` | 53 |
| `storage.test_event_key` | 80 |
| `storage.test_regex` | 46 |
| `storage.test_repository` | 865 |
| `storage.test_schema` | 131 |
| `storage.test_u64` | 90 |
| `device.test_codec` | **179** |
| `device.test_wire` | **130** |
| `device.test_sim_journal` | **461** |
| `device.test_sim_protocol` | **507** |
| `contract.device.test_golden_frames` | **168** |
| `contract.device.test_device_port` | **389** |
| **合计** | **3551** |

S1-03 新增 **1834** 条（3551 − 1717）；另有 `contract.device.daemon_smoke` 的 391 条进程级检查（Python 用例，不计入 C 断言总数）与 `storage.schema_source_drift`（Python 漂移门禁）。

### 5.2 验收项逐条映射

| 卡原文验收 | 实现 | 测试 |
|---|---|---|
| 黄金帧逐字节一致 | `codec.c:588` `locker_wire_encode_request` + `wire.c:70` `locker_wire_encode_frame`；CRC `wire.c:13` | `test_golden_frames.c:83`（HELLO 61 B 全等）、`:134`（SNAPSHOT 28 B 全等）、`:74`（`123456789`→0x29b1）、`:346` 主入口 |
| 半帧、多帧、CRC/长度错误不越界 | `wire.c:157` `locker_wire_stream_feed`：magic 不符/超长/CRC 错均丢弃 1 字节重同步，永不按对端长度分配 | `test_wire.c:98/124/149/188/211/232/258`；`test_sim_protocol.c:291` 半帧/双帧/CRC/超长 header/未知 type 全 INVALID 且事件数不变 |
| 旧 session 不重复动作 | `sim.c:1935` `session_scoped` 判定：非 HELLO 且 session 不符 → `sim.c:1939` BAD_SESSION（错误响应用**请求携带的 session** 回显，`sim.c:2282-2283`）；`port.c:361-366` DEVICE_EVENT 非当前 session/epoch 丢弃 | `test_sim_protocol.c:353` `test_session_rules`（无会话拒、HELLO 换会话后旧会话拒）；`test_device_port.c:1253` `test_foreign_event_dropped` |
| 重复 action_no 不重复动作 | `sim.c:1639` 已有记录且全部不可变参数一致 → 回放原状态；不一致 → `sim.c:1649` REPLAY_BLOCKED；`sim.c:1666` command_id 全局查重 → `:1667` REPLAY_BLOCKED | `test_sim_protocol.c:654` 同 command 重放得 OK+PULSE_STARTED；不同 command 复用 action_no=1 得 REPLAY_BLOCKED 且水位不变 |
| 跳号不重复动作 | `sim.c:1683-1685` `action_no != watermark+1` → `<=watermark ? REPLAY_BLOCKED : ACTION_GAP` | `test_sim_protocol.c:654` action 5 跳号 → ACTION_GAP；action 0 → BAD_PAYLOAD |
| 模拟器重启后动作查询及事件键仍可核对 | `sim.c:2118` `locker_sim_open`（`sim.c:2175` boot_id+1、写 BOOT 事件、重算 first/last）、`sim.c:2208` `locker_sim_restart` | `test_sim_journal.c:266`（boot+1、epoch 不变、旧 session BAD_SESSION、fetch 得 2 条 BOOT）、`:349`（水位与 QUERY 结果跨重启保留）、`test_device_port.c` 与 `daemon_smoke.py` 的 restart 段 |
| ACK 与 PULSE_STARTED 可独立控制 | `sim.c:2426` `control_drop_ack`（应用但不回帧）、`sim.c:2435` `control_drop_execute_ack`（只丢 EXECUTE 的 ACK）、`sim.c:2453` `control_suppress_pulse`（跳过脉冲段） | `test_sim_protocol.c:1101` `test_drop_ack_suppression`；`:919` `test_pulse_activity_rules`；`:1307` `test_execute_ack_loss_is_independent_of_pulse_suppression`；`test_device_port.c:1298` `test_drop_ack_no_reply` |
| 日志提交失败时拒绝新动作 | `sim.c:1686` `fail_journal || journal_state != HEALTHY` → LOG_UNAVAILABLE；`sim.c:410` `sim_journal_commit` 在 `fail_journal` 时先 ROLLBACK | `test_sim_journal.c:427` `test_journal_failure_refuses_new_action`（FAULT → LOG_UNAVAILABLE、水位 0、无活动上下文；恢复后成功）、`:782` `test_real_sql_fault_cannot_be_cleared_by_injection_switch` |
| 控制入口 0600 且注入门/物品/读卡/断链/拒绝/丢 ACK/事件缺口/模拟重启 | `sim_main.c:278` `listen_unix`（`umask(0077)` → bind → `sim_main.c:302` `chmod(0600)`）；控制命令见 §7.3 | `daemon_smoke.py`（实测 `socket_mode_uart=0600`、`socket_mode_control=0600`，逐条驱动全部控制命令，含 `drop_link`） |
| 独立 SQLite 设备日志、不共享业务库 | `sim.c:266` `SIM_SCHEMA_SQL`（sim_meta/sim_events/sim_commands/sim_commands_v2），`sim.c:2138` WAL + `synchronous=FULL` | `test_sim_journal.c:231` 校验文件头 `SQLite format 3` 与三表内容；`daemon_smoke.py` 校验库文件在独立目录 |

### 5.3 四项设计检查（只证明设计一致性，不代替构建/硬件）

`python3 -B scripts/check_contracts.py`、`check_stage_plan.py`、`check_vision_contracts.py`、`check_feasibility.py` 四个退出码全 0（VMware canonical run `20261026-095548`，见 `vm-s103-design-checks.log`；结构化输出为同目录 4 个 `check_*.json`）：

- `check_contracts`：PASS，contract_version 0.2.1、public_routes 24、internal_ports 82、private_rpc_methods 28、resolved_schema_refs 453、uart_vectors 4、uart_requests 11、max_wire_payload 469、`c_declaration_syntax` = NOT_RUN（VM 的 Python 3.14 未装可选 pycparser）、`hardware_profile` = UNVERIFIED、`business_or_hardware_tested` = false。
- `check_stage_plan`：PASS，stages 5、task_cards 45、active_task S1-03、implementation_cards_done 2、`runtime_or_hardware_tested` = false。
- `check_vision_contracts`：PASS，vision_schema_refs 58、`camera_owned` = false、`rknn_available` = false、`model_inference_tested` = false。
- `check_feasibility`：PASS，async_request_contexts 3、exposure_negative_cases 3、`two_wire_break_detection` = NOT_CLAIMED、`runtime_or_hardware_tested` = false。

`c_declaration_syntax` 如实记录：VM 的 Python 3.14 未安装可选依赖 pycparser，证据日志是 `NOT_RUN (optional pycparser unavailable)`；本机 Windows Python 有 pycparser，交付前重跑得到 `PASS (parse only, not compile/link)`。这是**同一脚本在不同环境的输出**，不是两次不同结论：本次未在 Linux 环境装依赖去「凑」这条输出，也**不把 parse 当 C 编译链接**。

这四个脚本**不**证明 UART 时序、CRC 抗篡改、GPIO 脉冲或 littlefs 掉电。

### 5.4 测试非空洞性（mutation）

`mutation-batch.py` 定义 **56 个点位**（每个点位在原始树中 `old` 恰好出现 1 次；`--check` 实测 `CHECK_SUMMARY points=56 bad=0`）。full 模式逐点位复制源码树、改一处、configure+build+ctest，日志 `mutation.log`（**208276 B**，sha256=`d4fa3c310b4bf667de070ccf759c3c9b66ed899fda8b8e70d60ccea77ace3376`，**1649 行**（按 LF 计：1649 个换行且文件以换行结尾）；在 round-9 非阻塞整改后的 canonical 批次树 `~/locker-workspace/S1-03/batches/20261026-095548/src`（与 mutation.log 第 2 行 `# source tree` 逐字一致；该批次树就是 canonical 快照 `07d7ab6e…7f38` 解包后的活跃树，`source-manifest.txt` sha256=`7af887a219586547f7798dd20602070f94230aec3c99d0919a4e38a2fd56860e`；跑期间未编辑任何受管文件，日志头部 7 条 `# SOURCE_FINGERPRINT` 与 manifest 逐条一致）、`S103_RUN=20261026-100500` 重跑，每点位另存未过滤的完整 CTest stdout 于 `logs/S1-03/mutation/20261026-100500/raw/<point>.log`，**56 份**（每点位一份）打包为 `mutation-raw-ctest.tgz`（**23286 B**，sha256=`e1d4986a4d5bc6a08e0537d4fbae8c38d6805d80204639b21cfe2c209ecfa8ca`，**57 个 tar 条目** = `raw/` + 56 份点位日志））。

**CAUGHT 51 / 56，CAUGHT_TIMEOUT 2（m49_connect_nonblock、m51_journal_collision，均靠杀进程观测），合计 53 / 56 被捕获；NOT_CAUGHT 3，BUILD_FAILED 0**。下表逐点位（`位置` 是该点位锚点在当前源码树中的 `文件:行`；`捕获` 列的分子是该次变异下打印出的 `FAIL <file>:<line>:` 断言行总数，`分母` 只累加**打印出完整 `(n of m)` 汇总行**的那些测试二进制的 `m`，`daemon 红` 是 `contract.device.daemon_smoke` 变红的进程级检查条数）。两者口径不同：没有打印汇总行的二进制，其失败断言计入分子却不计入分母（`mutation.log` 只筛出 `FAIL`/汇总行，但**每点位未过滤的完整 CTest stdout 已归档在 `mutation-raw-ctest.tgz` 的 `raw/<point>.log`**，可逐点位复核；例如 m49 的 `raw/m49_connect_nonblock.log` 第 43 行是 `18/19 Test #18: contract.device.test_device_port .....***Timeout 120.11 sec`），因此 `分母` 是**可核对的子集**，不是分子对应的全集：

| 点位 | 位置 | 结果 |
|---|---|---|
| m1_crc_poly | `host/device/src/wire.c:26` | 捕获 34 断言 / 分母 298 / daemon 红 12 |
| m2_magic | `host/device/src/wire.c:196` | 捕获 5 断言 / 分母 298 / daemon 红 3 |
| m3_oversize_len | `host/device/src/wire.c:205` | 捕获 2 断言 / 分母 298 |
| m4_crc_mismatch | `host/device/src/wire.c:219` | 捕获 8 断言 / 分母 805 / daemon 红 3 |
| m5_reqno_zero | `host/device/src/wire.c:98` | 捕获 1 断言 / 分母 130 |
| m6_event_reqno | `host/device/src/wire.c:95` | 捕获 1 断言 / 分母 130 |
| m7_pending_type | `host/device/src/port.c:134` | 捕获 2 断言 / 分母 389 |
| m8_event_session | `host/device/src/port.c:362` | 捕获 1 断言 / 分母 389 |
| m9_reply_flag | `host/device/src/port.c:429` | 捕获 93 断言 / 分母 389 |
| m10_simulated_gate | `host/device/src/port.c:612` | 捕获 1 断言 / 分母 389 |
| m11_slot_id | `host/device/src/port.c:703` | 捕获 1 断言 / 分母 389 |
| m12_fetch_limit | `host/device/src/port.c:784` | 捕获 2 断言 / 分母 389 |
| m13_anchor_reqno | `host/device/src/port.c:924` | 捕获 3 断言 / 分母 389 |
| m14_cursor_halfzero | `host/device/src/codec.c:209` | 捕获 3 断言 / 分母 309 / daemon 红 1 |
| m15_event_type_range | `host/device/src/codec.c:294` | 捕获 2 断言 / 分母 179 |
| m16_card_type | `host/device/src/codec.c:312` | 捕获 1 断言 / 分母 179 |
| m17_reserved | `host/device/src/codec.c:324` | 捕获 2 断言 / 分母 179 |
| m18_supply_unknown | `host/device/src/codec.c:327` | 捕获 1 断言 / 分母 179 |
| m19_decode_version | `host/device/src/codec.c:1169` | 捕获 1 断言 / 分母 179 |
| m20_failed_len | `host/device/src/codec.c:1191` | 捕获 1 断言 / 分母 179 |
| m21_fetch_len | `host/device/src/codec.c:1221` | 捕获 1 断言 / 分母 179 |
| m22_queue_full | `host/simulator/src/sim.c:851` | 捕获 1 断言 / 分母 507 |
| m23_queue_no_session | `host/simulator/src/sim.c:847` | 捕获 1 断言 / 分母 461 |
| m24_hello_version | `host/simulator/src/sim.c:1114` | **未捕获** |
| m25_has_more | `host/simulator/src/sim.c:1276` | 捕获 11 断言 / 分母 498 |
| m26_fetch_after_bound | `host/simulator/src/sim.c:1297` | 捕获 1 断言 / 分母 507 |
| m27_ack_regression | `host/simulator/src/sim.c:1347` | 捕获 3 断言 / 分母 507 |
| m28_ack_upper | `host/simulator/src/sim.c:1350` | **未捕获** |
| m29_resume_mode | `host/simulator/src/sim.c:1412` | 捕获 2 断言 / 分母 507 |
| m30_execute_horizon | `host/simulator/src/sim.c:1679` | 捕获 18 断言 / 分母 507 |
| m31_execute_replay | `host/simulator/src/sim.c:1687` | **未捕获** |
| m32_execute_journal | `host/simulator/src/sim.c:1690` | 捕获 7 断言 / 分母 461 |
| m33_execute_door | `host/simulator/src/sim.c:1696` | 捕获 14 断言 / 分母 507 |
| m34_borrow_item | `host/simulator/src/sim.c:1698` | 捕获 16 断言 / 分母 896 |
| m35_health_timeout | `host/simulator/src/sim.c:1374` | 捕获 4 断言 / 分母 507 |
| m36_pulse_flag | `host/simulator/src/sim.c:1771` | 捕获 17 断言 / 分母 967 / daemon 红 2 |
| m37_release_pulse | `host/simulator/src/sim.c:1878` | 捕获 10 断言 / 分母 507 |
| m38_release_idempotent | `host/simulator/src/sim.c:1885` | 捕获 5 断言 / 分母 896 |
| m39_dispatch_decode | `host/simulator/src/sim.c:1934` | 捕获 4 断言 / 分母 507 / daemon 红 2 |
| m40_dispatch_session | `host/simulator/src/sim.c:1940` | 捕获 3 断言 / 分母 968 / daemon 红 3 |
| m41_continuity_flag | `host/simulator/src/sim.c:945` | 捕获 1 断言 / 分母 461 |
| m42_inject_latch | `host/simulator/src/sim.c:2488` | 捕获 6 断言 / 分母 968 / daemon 红 1 |
| m43_boot_increment | `host/simulator/src/sim.c:2179` | 捕获 647 断言 / 分母 1253 / daemon 红 5（同时有一个二进制 TIMEOUT，见下注） |
| m44_socket_mode | `host/simulator/src/sim_main.c:302` | 捕获 0 断言 / 分母 0 / daemon 红 2 |
| m45_status_token | `host/simulator/src/sim_main.c:475` | 捕获 0 断言 / 分母 0 / daemon 红 2 |
| m46_drop_link | `host/simulator/src/sim_main.c:704` | 捕获 0 断言 / 分母 0 / daemon 红 1 |
| m47_frame_resync | `host/simulator/src/sim_main.c:429` | 捕获 0 断言 / 分母 0 / daemon 红 24 |
| m48_event_flush | `host/simulator/src/sim_main.c:400` | 捕获 0 断言 / 分母 0 / daemon 红 1 |
| m49_connect_nonblock | `host/device/src/transport.c:109` | **CAUGHT_TIMEOUT**：0 断言 / 0 分母 / 0 daemon 红，靠 ctest 自身 TIMEOUT 120 杀进程观测 |
| m50_journal_default_len | `host/simulator/src/sim_main.c:910` | 捕获 0 断言 / 分母 0 / daemon 红 7 |
| m51_journal_collision | `host/simulator/src/sim_main.c:924` | **CAUGHT_TIMEOUT**：0 断言 / 0 分母 / 0 daemon 红，靠 ctest 自身 TIMEOUT 120 杀进程观测 |
| m52_canonical_entry_raw | `host/simulator/src/sim_main.c:110` | 捕获 0 断言 / 分母 0 / daemon 红 16 |
| m53_wal_shm_off | `host/simulator/src/sim_main.c:267` | 捕获 0 断言 / 分母 0 / daemon 红 24 |
| m54_dangling_link_ignored | `host/simulator/src/sim_main.c:205` | 捕获 0 断言 / 分母 0 / daemon 红 28 |
| m55_link_spelling_sidecars | `host/simulator/src/sim_main.c:257` | 捕获 0 断言 / 分母 0 / daemon 红 8 |
| m56_link_hop_limit | `host/simulator/src/sim_main.c:202` | 捕获 0 断言 / 分母 0 / daemon 红 4 |

m43 的口径说明（round-4 R4-N-3）：该次变异共打印 **647** 条断言失败行，但只有 3 个测试二进制打印了完整汇总行（`test_sim_journal.c` 210/461、`test_sim_protocol.c` 354/624、`test_golden_frames.c` 2/168，合计 566/1253），另有 **81 条来自 `test_device_port.c`**，它的完整 `(n of m)` 汇总行未进入捕获输出，因此分母 1253 只覆盖那 3 个二进制，**不是** 647 的全部分母。`mutation.log` 该段的 `assertion_failures` 行已按「`complete_summaries` 个二进制、分母仅含其汇总」表述（`mutation-batch.py` 措辞同步）；647 的按文件分布可由该段 `FAIL` 行复算（210 + 354 + 2 + 81 = 647）。

m43 同时被 `hung=1` 标记：`test_device_port` 在该变异体下也会触发 ctest 的 `TIMEOUT 120`。脚本的分类顺序是「红色证据优先于杀进程」——只要有 `FAIL` 行或 daemon 红就记 `CAUGHT`，`hung` 只作为独立列记录，避免把一个既红又超时的点位误记成 `CAUGHT_TIMEOUT`（round-5 首次重跑就是这样把 m43 记成 `CAUGHT_TIMEOUT` 的，已修正并重跑）。

m44–m48 与 m50–m56 是 `sim_main.c` 的 12 个点位：daemon 未被 C 测试链接（见 §6），它们只能由 `contract.device.daemon_smoke` 的进程级检查暴露（C 断言分母为 0）。其中 11 个以 `daemon 红` 非 0 捕获，m51 为 0 红超时（见下），两者都证明进程级控制面、拆帧器与路径校验不是只靠 `ctest` 恰好通过。其中 m50_journal_default_len（`sim_main.c:910` 把 `written` 检查换成 `(void)written;`）与 m51_journal_collision（round-6 时锚在 `sim_main.c:713` 的原撞名守卫；round-7 R7-B-1 整改后该守卫迁到 `paths_are_distinct`，现锚 `sim_main.c:924`）是 R6-B-1 整改时新增的回归点位：m50 使 daemon 的 7 条进程级检查变红；m51 在 round-8 版跑次里曾使 32 条变红，round-9 前自查补了 `link_spelling_wal`/`link_spelling_shm`/`link_hops` 三例、daemon 检查从 366 增至 391 后，`daemon_smoke` 在该变异体下先卡死（`raw/m51_journal_collision.log:45` 的 `19/19 Test #19: contract.device.daemon_smoke ...***Timeout 120.11 sec`，0 条 FAIL 行），因此本轮被记为 **CAUGHT_TIMEOUT**（daemon 红 0、hung=1）。daemon 检查数：round-6 为 286、round-7 为 318、round-8 为 366，round-9 前自查后为 391。round-7 又新增 m52_canonical_entry_raw（round-8 整改后锚 `sim_main.c:110`，把 ENOENT 回退的 `snprintf("%s/%s", parent_real, name)` 换成直接返回原串，使 `D/./x` 别名不再被识别）与 m53_wal_shm_off（round-8 整改后锚 `sim_main.c:267`，给 `clashes_with_sockets(sidecar, uart, control)` 条件加 `&& 0`，使 `-wal`/`-shm` 与 socket 的碰撞不再被拒），用于证明路径身份守卫不是死代码。round-8 再新增 m54_dangling_link_ignored（`sim_main.c:205`，把「跟随末级悬空链接」改回直接 `canonical_missing`）与 m56_link_hop_limit（`sim_main.c:202`，给 8 跳上限加 `&& 0`）；另有一个点位 m55 在 round-9 前自查中被重定向：它原本锚 `sim_main.c:248` 的 `clashes_with_sockets(journal_spelling, uart, control)`，但 `canonical_entry` 先跑 `realpath`，而 `realpath` 永不返回末级是符号链接的路径，所以 socket 的规范路径不可能等于 journal 的字面拼写，该子句是**不可达死代码**（新增 `link_spelling_socket`/`link_spelling_uart` 用例后变异体仍 0 红，实证其不可观测）。整改删掉该子句、保留 sidecar 的双形态循环，m55 改为 `m55_link_spelling_sidecars`（`sim_main.c:257` 把 `bases[1] = journal_spelling;` 换成 `bases[1] = journal;`），并新增 `link_spelling_wal`/`link_spelling_shm` 用例后 8 条 daemon 检查变红；`link_hops`（9 跳链）使 m56 变红 4 条。四个点位都针对 R8-B-1 修复的守卫本身。

m49_connect_nonblock 是本轮（round-5 整改）新增的第 49 个点位，锚在 `host/device/src/transport.c:109` 的 `F_SETFL flags|O_NONBLOCK`。变异体把非阻塞设置整段替换为 `flags = fcntl(fd, F_GETFL, 0); (void)flags;`，恢复阻塞 `connect()`：此时 `test_connect_deadline_when_accept_queue_full` 会停在 accept 队列满的内核里，没有任何断言变红，只能靠杀进程观测。实测两条杀进程路径并存——`tests/contract/device/CMakeLists.txt:25` 的 `TIMEOUT 120` 属性先触发（ctest 打印 `***Timeout` 并以 exit 8 结束），外层 `timeout 300` 只在 ctest 自身也被卡住时才给 exit 124。脚本的 `hung = (rc_ctest == 124) or ('***Timeout' in 输出)` 覆盖两条路径，并把该点位单列为 `CAUGHT_TIMEOUT`，**不**混进 `CAUGHT` 的断言计数，也不允许被记成 `NOT_CAUGHT`。

**NOT_CAUGHT 3 / 56**（如实登记；每个都给出可证的等价性论证，不用「均等价」概括）：

| 点位 | 位置 | 为什么不可观测 |
|---|---|---|
| m24_hello_version | `sim.c:1114` 去掉 payload `requested_version` 检查 | 同一校验先在 `codec.c:836` 执行（`decode_request_body` case 1 → BAD_VERSION），`sim_dispatch`（`sim.c:1931-1937`）解码失败即回原码，`sim_handle_hello` 根本不被调用；删掉第二处后线上行为逐字节相同 |
| m28_ack_upper | `sim.c:1350` 去掉 `through > last_committed` 早退 | 不变量 `fetched_through <= last_committed` 恒成立（`fetched_through` 只在 `sim.c:1324-1326` 从 `page.next_after` 推进，而 `page.next_after <= last_committed`；HELLO 时置为 `ack_persisted`，ack 本身受 `sim.c:1350` 约束）。故 `through > last_committed ⟹ through > fetched_through`，紧随其后的 `sim.c:1356` 返回同一个 CURSOR_GAP |
| m31_execute_replay | `sim.c:1687` 把 `action_no <= watermark` 改成 `<` | watermark=W>0 时，W 要么来自 HELLO 的会话水位（该会话必有 `action_no=W` 的行），要么来自 `sim.c:1735`（本会话刚插入的行）；两种情况 `sim_journal_load_command(W)` 都 found，走 `sim.c:1643`/`sim.c:1670` 的 existing 分支返回 REPLAY_BLOCKED，永远到不了 `sim.c:1687` 的相等分支。相等语义由 `test_sim_protocol.c:763-766`（action 1 == watermark、换 command → REPLAY_BLOCKED）直接覆盖 |

本轮为消除「测试有洞」新增 2 处正式断言（不是放宽）：

- **m7_pending_type**：新增 `test_device_port.c` 的 `test_pending_entry_matches_response_type` —— hold 服务端回复后发 `get_snapshot(704)`，注入「同 request_no、不同响应类型（FETCH 0x83）、同 session」的合法帧，断言 sink 不增加；再注入真帧，断言收到 `message_type=0x82`、`request_no=704`。证明 `pending_take` 的 `request_type` 条件不是死代码。
- **m22_queue_full**：`test_event_queue_saturation` 补 pop 断言 —— 队列满后 `locker_sim_pop_event` 恰好成功 `LOCKER_SIM_EVENT_QUEUE`（32）次，第 33 次返回 `LOCKER_STATUS_NOT_READY`，同时 `dropped_events=0`、`event_count=34`。

另修 3 处测试夹具的无界分页循环（`test_sim_protocol.c` `sim_prepare_ready`、`test_sim_journal.c` `sim_prepare_ready`、`test_device_port.c` `port_env_reconcile`），改为 `LOCKER_TEST_PAGE_LIMIT` 上界 + 越界断言。此前 m43_boot_increment 变异使 `has_more` 永真，循环打印 1100 万行、日志膨胀到 1.7 GB；加上界后该点位输出受控。历史版本（均已作废，仅作审计追溯）：round-4 版 647 条失败断言 / `mutation.log` 185389 B / 1418 行；round-5 版 646 条 / 186270 B / 1426 行；round-6 版 51 点位 646 条失败断言、分母 1253、`mutation.log` 197711 B / 1527 行；round-7/8 版 56 点位 646 条（`test_device_port.c` 80 条）/ 208118 B / 1648 行 / sha256 `02dde78c…16e8c`。**当前值**见 §5.4（56 点位、647 条 / 208276 B / 1649 行 / `d4fa3c31…3376`）；本节其余段落一律以 §5.4 为准。

### 5.5 NOT_RUN（环境/硬件不具备，不伪造）

| 项 | 状态 | 原因 |
|---|---|---|
| 真实 UART 电气/时序（115200 8N1、字节间隔、线路噪声、断线） | **NOT_RUN** | 无串口实物，stage 1 只用 AF_UNIX 传输字节 |
| STM32 HAL / 交叉编译 / Keil | **NOT_RUN** | 本卡是主机侧协议层与模拟器，未触碰 `firmware/` |
| GPIO 限时脉冲硬截止 | **NOT_RUN** | 模拟器只写计划毫秒数，`pulse_active` 是状态位；无 GPIO、无定时器 |
| MCU / littlefs 掉电可靠性 | **NOT_RUN** | `restart` 是进程内 close+open；SQLite 与 littlefs 不是同一介质 |
| CRC 抗篡改 / 认证 | **NOT_RUN** | `uart-v1.md:21` 明确 CRC 不承担身份认证；本卡未加任何加密 |
| `c_declaration_syntax`（pycparser） | **NOT_RUN** | 检查器在 VM 的 Python 3.14 上无可选依赖 pycparser，输出 `NOT_RUN (optional pycparser unavailable)`；不为凑输出装依赖，也不把 parse 当编译链接 |
| 硬件 profile | **UNVERIFIED** | 沿用契约状态，本卡未验证任何硬件 |

## 6. 自审与已知缺口

- **`reply.message_type` 带响应标志（待契约负责人裁定）**：`port.c:429` 写 `(uint8_t)(request_type | LOCKER_WIRE_RESPONSE_FLAG)`。契约没有明文规定 `locker_device_reply_t.message_type` 是否含 0x80；本卡裁定为「sink 收到的是被解码的线上响应类型，必须带 0x80」，否则 `type=1`（HELLO 请求）与 `0x81`（HELLO 响应）无法区分。round-2 审计 R2-N-4 指出这只是本卡裁定、生成头只声明 `uint8_t`，因此**交契约负责人裁定，本卡不改 `contracts/`**；已记入 `host/device/README.md` §4 与 handoff §12.4。
- **`pending_take` 类型/会话不匹配时不消费、继续扫描**（`port.c:127-142` 是单层 `for` 循环，命中三条件才 `*out = *entry; entry->used = false`，不匹配只是不进入分支、自然进入下一轮迭代）：陈旧条目复用了同一请求号时不能遮蔽活请求；`uart-v1.md:23` 要求 session+request_no+response_type 三者都匹配才应用响应。**在途请求号唯一性由 `pending_add`（`port.c:145` 起，同一把锁内查重）保证，不靠 `pending_take` 的扫描顺序**；`pending_take` 里没有字面 `continue`。回归 `test_device_port.c` 的 `test_pending_entry_matches_response_type`。
- **`restart`/重开后门与物品状态为 `UNKNOWN`**（`sim.c:2165-2166`）：只有全新构造函数场景才是 `CLOSED`/`PRESENT`；重开日志没有物理观测，故报 `UNKNOWN` 并要求主机重新 `RESUME_READY`。真实 MCU 应由传感器重新采样，本卡不声称这一点。
- **脉冲结束是模拟定时转换**：`sim_finish_pulse`（`sim.c:1571`）在注入时钟推进到 `SIM_PULSE_MS`（150 ms，`sim.c:32`）后写 `PULSE_FINISHED` 并清 `pulse_active`；写失败则 `sim_latch_pulse_uncertainty`（`sim.c:1547`）锁存未知结果与持久缺口。它是确定性模拟，**不是** GPIO 硬截止，硬截止证据仍属阶段 2。
- **健康心跳窗口**：`sim_health_fresh`（`sim.c:1371`）要求 `health_seen`、`core_progress != 0`、`core_observed_age_ms <= 5000`，且接收时间在 5000 ms 内；超时返回 NOT_READY。
- **`suppress_pulse` 只影响后续脉冲**，不清已置位的 `pulse_active`；README §4 已注明。
- **daemon 未链接进 C 测试**：`test_device_port.c` 自己开进程内 AF_UNIX server 驱动真 `locker_sim_t`，因此 `sim_main.c` 用独立 Python 用例覆盖（391 条检查）。`test_device_port.c` 另有 `test_real_clock_smoke` 用真实单调时钟走完 open→GET_SNAPSHOT→close（`stage-01-task-cards.md:123` 要求保留一项真实时钟烟测）。`tests/contract/device/CMakeLists.txt` 在缺少 Python3 时 `FATAL_ERROR`，不会靠少注册测试变绿。
- **`LOCKER_SIM_PATH_SIZE`/`sun_path` 都是 108 字节**：深层构建目录放不下 socket/journal，`daemon_smoke.py` 在超长时回退到 `tempfile.mkdtemp`（测试夹具健壮性，不是产品放宽）。


## 7. behavior

### 7.1 帧与解析

帧格式（`uart-v1.md:9-17`）：`4c4b` | version=1 | type | payload_len u16 LE | request_no u32 LE | payload | CRC u16 LE。总长 `12 + payload_len`，最大 524。CRC-16/CCITT-FALSE（poly 0x1021、init 0xffff、refin/refout false、xorout 0），覆盖 offset 2 到 payload 末字节，不含 magic 与 CRC 自身。

流式解析器（`wire.c:157`）单遍扫描：magic 不符 → 丢 1 字节 + `resyncs`；`payload_len > 512` → 丢 1 字节 + `length_errors` + `resyncs`；CRC 不符 → 丢 1 字节 + `crc_errors` + `resyncs`；handler 返回非 OK → `handler_errors` 并中止本次 feed。解析器不分配内存、不按对端声明长度预分配。

### 7.2 设备端口状态机

`locker_device_open` 顺序：参数校验 → 已打开返回 CONFLICT → 未 bind sink 返回 NOT_READY → 时钟无效 INVALID → path/expected_device_id 为空 INVALID → `simulated == false` 返回 NOT_READY（stage 1 全部模拟）→ `transport_connect(5000 ms, 10 ms 重试)` → 生成 `pending_session_id` → 起 reader 线程 → 发 HELLO（request_no 由内部自增，跳过 0）。

单 reader 线程独占 `read`（200 ms 超时）；写方各自持 `write_lock` 串行。pending 表上限 `LOCKER_DEVICE_PENDING_MAX = 16`；`get_snapshot`/`time_anchor` 用调用方给的 `request_no`（契约签名如此），其余用内部自增号。`close` 顺序：`opened=false` → `reader_stop=true` → `transport_shutdown(SHUT_RDWR)` 让阻塞 read 立即返回 → `join` → `transport_close` → `stream_reset`。**没有任何 EXECUTE 重发循环**。

### 7.3 模拟器控制面（0600 AF_UNIX，一行一命令）

`status`（16 个 key）、`quit`、`door unknown|closed|open`、`item unknown|absent|present`、`card HEX`、`reject_next on|off`、`drop_ack on|off`、`suppress_pulse on|off`、`fail_journal on|off`、`inject_gap N`、`drop_link`、`restart`、`cursor B:S`。回复 `ok` 或 `error <detail>`，detail 分语法类（bad_argument/missing_argument/unknown_command/line_too_long）与引擎状态类（invalid/not_ready/conflict/busy/no_space/io_error/unknown/replay_blocked/failed）。单行上限 256 字节。`status`/`quit`/`restart`/`drop_link` 不需要参数。

### 7.4 独立持久化

`sim_meta`（key/value）、`sim_events`（PRIMARY KEY(boot_id,event_seq)，u64 一律 20 位补零十进制文本）、`sim_commands`（旧表，action_no 文本主键，只用于一次性迁移）、`sim_commands_v2`（`sim.c:282-289`，PRIMARY KEY(session_id,action_no)，动作水位按会话分隔；`INSERT OR IGNORE INTO sim_commands_v2 SELECT * FROM sim_commands` 迁移旧行）。WAL + `synchronous=FULL`，事务 `BEGIN IMMEDIATE`。`boot_id` 每次 open 递增并写 BOOT 事件（seq=1）。事件通知队列上限 32（`sim.c:863`）：满了只丢**实时通知**，**日志行不丢**，且**不**锁存 `continuity_ok`——它是发送背压不是连续性故障（`uart-v1.md:356`），缺的通知由 FETCH 补齐。动作水位与 `sim_commands_v2` 同事务写入。

### 7.5 round-9 审计结论与非阻塞整改

round-9 独立审计（`audit-round9.md`，33107 B）结论 **PASS**：0 阻塞、4 非阻塞（R9-N-1～R9-N-4）。四项均已处置，处置后重算了 manifest、重跑了 canonical 与 56 点位 mutation（数字见 §5.4 与 §5.1）：

| 项 | 等级 | 内容 | 处置 |
|---|---|---|---|
| R9-N-1 | P3 | `locker_contracts.h:181` 的 reply `message_type` 方向位（`request_type \| 0x80`，`port.c:429`）在共享契约中未定义 | 交契约负责人裁定（`docs/architecture/module-ownership.md:11`）；本卡不改生成物，已在 `host/device/README.md` 登记 |
| R9-N-2 | P2 | `sim_main.c` 的 `paths_are_distinct` 注释、`host/simulator/README.md:22`、`daemon_smoke.py:562` 把「SQLite 在字面拼写上追加 -wal/-shm」当 sidecar 双形态检查的理由 | 改为「解析目标是数据库文件实际成为的条目；字面拼写是本模块自己的保守限制，不是 SQLite 要求」，依据审计在 guest 实测的 `j -> real` 只产生 `real-wal`/`real-shm`（vendored `host/storage/third_party/sqlite/sqlite3.c:47604-47633` 解析符号链接） |
| R9-N-3 | P3 | `mutation-batch.py` 8 个点位的 `ref` 是旧行号；`summary.md` 三处逐字引用/口径不符；`handoff.md` 一处把 round-8 写成 PASS | 8 处 ref 全部重算（m44 `sim_main.c:302`、m45 `:475`、m46 `:704`、m47 `:429`、m48 `:400`、m50 `:910`、m51 `:924`、m53 `:267`）；m49 的 CTest 超时原文按本轮重跑改为 120.11 秒；m51 明确为 0 红超时；`handoff.md` 该处改为 FAIL |
| R9-N-4 | P3 | `tests/contract/device/__pycache__/daemon_smoke.cpython-314.pyc` 残留；`evidence-make-manifest.sh:21` 的 `find` 未排除 pyc | 删除残留；生成器加 `! -name '*.pyc' ! -path '*/__pycache__/*'` |

## 8. docs / residue

本轮同步的文档：

- `host/device/README.md`（新建）、`host/simulator/README.md`（新建）：模块边界、构建接入、控制命令、已知边界与验证状态。
- 本目录：`summary.md`、`handoff.md`、`audit.md`（round-1 审计，历史记录不改）、`audit-round2.md`（round-2 审计，6 阻塞 + 4 非阻塞）、`audit-round3.md`（round-3 审计，3 阻塞 + 4 非阻塞）、`audit-round4.md`（round-4 审计，1 阻塞 + 4 非阻塞）、`audit-round5.md`（round-5 审计，1 阻塞 + 3 非阻塞）、`audit-round6.md`（round-6 审计，1 阻塞 + 3 非阻塞）、`audit-round6-prompt.txt` / `audit-round6-runner.ps1` / `audit-round6-run.log` / `audit-round6-codex-output.txt`（round-6 审计输入、启动器、运行记录与原始转录，随本卡归档以证明审计范围可复现）、`source-manifest.txt`（25 条，2794 B，sha256=7af887a219586547f7798dd20602070f94230aec3c99d0919a4e38a2fd56860e）、`mutation.log`、4 个设计检查 JSON、3 个证据脚本（`evidence-make-manifest.sh`、`evidence-make-snapshot.sh`、`evidence-run-vm.sh`）、`mutation-batch.py`、`mutation-raw-ctest.tgz`（R6-N-2 整改：**每个点位**的未过滤完整 CTest stdout，字节数与 sha256 见 §5.4）、`audit-round7.md`（round-7 审计，2 阻塞 + 2 非阻塞）、`audit-round7-prompt.txt` + `audit-round7-runner.ps1` + `audit-round7-run.log` + `audit-round7-codex-output.txt`（round-7 审计输入、启动器、运行记录与原始转录，随本卡归档以证明审计范围可复现）、`audit-round8.md`（round-8 审计，1 阻塞 + 2 非阻塞）、`audit-round8-prompt.txt` + `audit-round8-runner.ps1` + `audit-round8-run.log` + `audit-round8-codex-output.txt`（round-8 审计输入、启动器、运行记录与原始转录）、`audit-round9.md`（round-9 审计，结论 **PASS**，0 阻塞 + 4 非阻塞）、`audit-round9-prompt.txt` + `audit-round9-runner.ps1` + `audit-round9-run.log` + `audit-round9-codex-output.txt`（round-9 审计的输入、启动器、运行记录与原始转录）、17 个 `vm-s103-*.log`（含 `vm-s103-gates.log`）+ `vm-s103-manifest.sha256`（共 18 个 `vm-s103-*` 文件），当前共 **56 个文件**（round-9 的 report / run.log / codex-output 三件已归档；`mutation-raw-ctest.tgz` 与 `mutation.log` 已换成 round-9 非阻塞整改后重跑的 56 点位版本）。
- 收尾同步的入口文档（集成负责人职责）：`README.md`（当前发卡改为 S1-04、状态列表加 S1-03 DONE、`active_task=null` / `recommended_next=S1-04`）、`AGENTS.md`（阅读顺序第 5 条与状态行）、`docs/stages/README.md`（状态行与单卡交接段落补 S1-03 报告/交接链接）、`docs/stages/stage-01-task-cards.md`（状态行）、`docs/stages/parallel-execution-plan.md`（「当前落地状态」段）、`config/development/stage-plan.json`（S1-03 `status=DONE`、`report` 指向本报告、`active_task=null`、`recommended_next=S1-04`；阶段 1 仍 `implementation_status=IN_PROGRESS` / `gate_result=NOT_RUN`）。

本轮临时残留已清空：`C:/Users/Public/` 下 105 个 `s103*` 条目（脚本、`*_in.txt`/`*_out.txt` 审计转录、`s103_snapshot*.tar.gz`、`s103_r5_logs` 等）与 `C:/Users/%USER%/AppData/Local/Temp/` 下 126 个 `s103*` 条目（`s103_canon*.ps1`、`s103_vm_mut.py`、六份 `s103_snapshot*.tar.gz`、`s103_stage*` 等）在两个目录下实测剩余 0。WSL2 专用脚本与日志（`evidence-run-wsl.sh`、16 个 `wsl-*.log`、`wsl-run-id.txt`）以及已取消的联合修复记录 `joint-fix-round1.md` 按用户指令已于更早轮次删除。仓库内 `scripts/__pycache__/` 是 2026-10-20 遗留的**空目录**（无文件），不影响任何检查，未在本轮删除；`STM32F4xx_HAL_Code/.../Objects/*.o` 属用户 Keil 工程既有产物，按边界不触碰。

## 9. decision

S1-03 的实现与验收证据已齐备：19 个 CTest 在 VMware Ubuntu 干净 guest 上全绿（`run_id=20261026-095548`，3551 条 C 断言 + 391 条 daemon 进程级检查），ASan/UBSan 与 TSan 零报告，56 点位 mutation 捕获 53 个（51 个 CAUGHT + 2 个以超时被杀方式捕获的点位：连接截止 `m49_connect_nonblock` 与 daemon 冒烟卡死 `m51_journal_collision`）、3 个未捕获点位各自给出可证的等价性论证（§5.4），四项设计检查全绿。按用户 m18104/m19922 指令，本卡不再使用 WSL2 作为任何证据来源。round-9 独立审计结论 **PASS**（0 阻塞 / 4 非阻塞），4 项非阻塞已整改并重新取证，故本卡判定 DONE；其中 R9-N-1（`reply.message_type` 的方向位）依 `docs/architecture/module-ownership.md:11` 交契约负责人裁定，不属本卡可改范围。

本卡已收尾 **DONE**（round-9 独立审计 PASS + 4 项非阻塞整改完成）；不 commit、不 push。阶段 1 仍为 IN_PROGRESS，`gate_result` 仍为 NOT_RUN（门禁卡 S1-07），本卡 DONE ≠ 阶段 PASS。
