# S1-02 SQLite 单写仓储与事务 — 实施报告

日期 2026-10-24。范围：仅 S1-02（`host/storage/` 唯一 schema-v1.sql、迁移元数据、六个 `locker_repo_*` 端口实现、`tests/unit/storage/` 正式测试、根 CMakeLists.txt 两行最小接入）。
本卡不是阶段 1 正式门禁；本报告不把阶段 1 设为 PASS，不把任何硬件能力（含 RK 存储介质掉电可靠性）标为已验证。

状态：**IN_PROGRESS**（等待最终独立收尾复核）。九轮独立审计中前七轮结论为 FAIL、第八轮（`audit-round8.md`）与第九轮（`audit-round9.md`，超轮联合确认轮，codex gpt-6.1-sol/high）结论均为 **PASS**；第九轮未发现尚未修复的阻塞项 B-x，仅保留 2 项非阻塞收尾问题 N-1/P3、N-2/P3（均已在收尾时逐条订正，见 §5.14）。两轮 PASS 的 C 端 configure/build/CTest/ASan/TSan/C mutation 均为 NOT_RUN（审计侧环境受限），不作为本卡 C 端证据。前七轮：`audit.md` 首轮 B1–B5 + N1–N5、`audit-round2.md` 复审 R2-B1/B2/B3 + R2-N1–R2-N4、`audit-round3.md` 三审 B-1/B-2 + N-1–N-3、`audit-round4.md` 四审 B-1/B-2 + N-1–N-3、`audit-round5.md` 五审 B-1 + N-1/N-2、`audit-round6.md` 六审 B-1 未知触发器 / B-2 隐藏生成列 + N-1 归因 / N-2 文档、`audit-round7.md` 七审 B-1 表内 CHECK/FOREIGN KEY 绕过 schema 接纳 + N-1 mutation 归因/顺序/计数；九轮均已逐条整改并补正式回归。实现、VMware 干净 guest 的构建/CTest、ASan+UBSan、TSan、mutation 非空洞性验证均已对**整改后的最终快照**重跑通过（VM run_id=`20261024-213500`）；按用户 m19922 指令，原 WSL2 一组的 18 个日志与脚本已删除，本卡 Linux 证据只有 VMware 一组。按用户 m12834 的超轮规则，round-8 起开发 AI 与审计 AI 共同核对结论；本卡在最终独立收尾复核前仍保持 IN_PROGRESS。

## 1. card / contract

| 项 | 值 |
|---|---|
| 卡 ID | S1-02 SQLite 单写仓储与事务 |
| 契约基线（用户指定） | 0.2.1 |
| 契约实际版本（执行前从 contract-source 读取） | `contracts/contract-source.json` → `contract_version` = 0.2.1，`status` = REVIEW_DRAFT；与基线一致，未使用旧副本 |
| 生成头契约版本 | `contracts/include/locker_contracts.h` → `#define LOCKER_CONTRACT_VERSION "0.2.1"` |
| 契约源指纹 | `contracts/contract-source.json` sha256=14e938a6502806e1518ac9160569e99717e9062e622da60c3aee8b4fd369898f（66553 B） |
| 生成头指纹 | `contracts/include/locker_contracts.h` sha256=34306a869f92d2caece69c0c92a0eb84f2377ef0ff51af657c60ee7ad629340a（39925 B） |
| 内部数据 schema 指纹 | `contracts/internal-data.schema.json` sha256=e7b8695c49115e2994cf126dc5ab629e5c82460e70d4c292b19576f55c60e386（129569 B）；本卡实现与测试直接消费它，未改一个字节 |
| 任务卡指纹 | `docs/stages/stage-01-task-cards.md` sha256=7f7561e83f9f614fd4ed226cefe3ce78ba367859a55ee92cd82212b25273fc93（16460 B）；本卡正文位于该文件 :48-56，逐条对照实现，未放宽验收 |
| stage-plan 指纹 | `config/development/stage-plan.json` sha256=1ed89d9f5695c40f965f43d331907295eccca21eaf52d67bee92e5dc4fdf1b47（24749 B） |
| 源码清单 | 本目录 `source-manifest.txt`：**32 个文件**（根 `CMakeLists.txt` + `host/storage/`（含 `README.md` 与 `tools/extract_schema.py`）+ `tests/unit/storage/`）逐文件 sha256+字节数，2026-10-24T20:59:57+08:00 生成；清单文件自身 sha256=817ddb073ba2a35b3b69e83e5fc3d8f72429ad27d3f22e076a967edecfa6dd0a（3563 B），`sha256sum -c` 32/32 OK（VM 与 WSL2 两处均复核）。其中 26 个为本卡手写源码，6 个为 vendored 第三方（清单末尾单独标注） |
| 版本门禁 | 根 `CMakeLists.txt:41-61` 在配置期用正则同时读生成头与 contract-source，版本不一致直接 `FATAL_ERROR "Contract version drift..."`，防止静默使用旧副本 |
| schema 子集门禁 | `tests/unit/storage/CMakeLists.txt` 末尾注册 CTest `storage.schema_source_drift`：`python3 -B host/storage/tools/extract_schema.py --check`，比对 `src/schema_records.json` 与契约真源，不一致即测试失败（审计 N5 要求） |

仓库不是 Git 仓库：`git status --short` 实测 **exit=128**（`fatal: not a git repository`）。无法用 Git 证明变更全集，故全部指纹改用 SHA-256 逐文件记录在 `source-manifest.txt`。

## 2. prerequisites

前置卡 S1-01 已完成（DONE）：

- `docs/evidence/stage-01/S1-01/summary.md`（状态 DONE）、`handoff.md`、`audit.md`（独立审计 PASS，无阻塞项，仅 N5/P3 文档勘误已修）。
- S1-01 交付的 `locker_common` 基础库本卡直接复用：`locker_uuid_parse/format/equal`（`host/common/include/locker/common/uuid.h`）用于 UUID 文本校验；`locker_contracts.h` 提供六个端口原型与 DTO。
- S1-01 已建立并已验证的构建入口（根 `CMakeLists.txt` 的 `LOCKER_BUILD_HOST/LOCKER_BUILD_TESTS/LOCKER_WARNINGS_AS_ERRORS` 选项、`locker_apply_warnings()`、契约版本漂移门禁、`Threads::Threads`）本卡只加两行接入，未重建构建体系。
- `tests/unit/common/test_support.h`（零第三方依赖断言宏）被 `tests/unit/storage/` 通过相对 include 复用，未复制第二套 harness。

执行前核对：

- `config/development/stage-plan.json` 开工时：`active_task="S1-02"`、`recommended_next="S1-02"`、S1-02 `status="IN_PROGRESS"`、`depends_on=["S1-01"]`、`primary_paths=["host/storage/","tests/unit/storage/"]`；`implementation_cards_done=1`。
- 工作区开工前不存在 `host/storage/` 与 `tests/unit/storage/`，无本卡残留，无失败测试可删。
- `list_agents` 仅有 lead，无并发写本卡目录或共享文件的 AI。
- 依赖归档已在本机（`%TEMP%/s102_deps/`），SHA-256 与 `deployment/host/dependencies.lock.json` 一致，VM 无外网也能构建。

## 3. scope

### 修改文件（32 个，均在授权目录内）

| 路径 | 行/字节 | 说明 |
|---|---|---|
| `CMakeLists.txt` | 3943 B | **仅加两行**：`:87 add_subdirectory(host/storage)`、`:92 add_subdirectory(tests/unit/storage)`。其余 91 行未动 |
| `host/storage/CMakeLists.txt` | 102 / 3832 | 三个目标：thirdparty / generated / locker_storage |
| `host/storage/README.md` | 125 / 7850 | 模块说明（构建接入、线程/生命周期、schema 子集再生成、已知边界） |
| `host/storage/tools/extract_schema.py` | 134 / 5355 | 契约 schema 子集的机械抽取器 + `--check` 漂移比对（CTest 调用） |
| `host/storage/schema-v1.sql` | 43 / 2350 | 唯一 DDL 真源（meta / migrations / documents） |
| `host/storage/cmake/embed_file.cmake` | 45 / 1891 | 构建期（`add_custom_command`）把真源文件逐字节嵌成 C 数组片段 |
| `host/storage/include/locker/storage/schema.h` | 43 / 2197 | schema 校验器公共入口（纯文本 API，不泄 cJSON） |
| `host/storage/include/locker/storage/event_key.h` | 35 / 1660 | 事件键文档 key 编解码 |
| `host/storage/include/locker/storage/u64.h` | 20 / 835 | u64 规范十进制 |
| `host/storage/include/locker/storage/regex.h` | 26 / 1130 | 受限正则（fail closed） |
| `host/storage/include/locker/storage/embedded.h` | 23 / 922 | 嵌入资源的外部声明 |
| `host/storage/src/repository.c` | 2469 / 95832 | 六个端口 + 事务/查询/分页/导入实现（含 round-2 的哨兵结构校验、round-3 的对象类型判定与列/唯一约束/BINARY 结构核验、round-6 的官方对象清单核验与 `table_xinfo` 隐藏列拒绝、写入效果 `sqlite3_changes` 守卫、round-7 的官方 `CREATE TABLE` 文本逐字节比对 `verify_official_ddl`） |
| `host/storage/src/repository_internal.h` | 74 / 3344 | 模块内部共享声明（不对外） |
| `host/storage/src/schema.c` | 759 / 26069 | JSON Schema 子集校验器（含嵌入 NUL 守卫） |
| `host/storage/src/schema_internal.h` | 23 / 1022 | cJSON 层内部入口 + 嵌入 NUL 守卫声明 |
| `host/storage/src/schema_records.json` | 1 / 27525 | 契约 schema 冻结子集（47 defs + 18 collections） |
| `host/storage/src/schema_embedded.c` | 22 / 809 | 手写外壳，`#include` 生成的 `.inc` |
| `host/storage/src/event_key.c` | 198 / 7165 | 事件键实现 |
| `host/storage/src/u64.c` | 58 / 2180 | u64 薄适配（复用 `locker_common` 的 numeric） |
| `host/storage/src/regex.c` | 768 / 25127 | 受限正则实现 |
| `host/storage/third_party/sqlite/sqlite3.c` | 9582521 | **vendored** SQLite 3.54.1 amalgamation（未改一字节） |
| `host/storage/third_party/sqlite/sqlite3.h` | 695729 | 同上 |
| `host/storage/third_party/sqlite/sqlite3ext.h` | 39547 | 同上 |
| `host/storage/third_party/cjson/cJSON.c` | 80399 | **vendored** cJSON 1.7.19（未改一字节） |
| `host/storage/third_party/cjson/cJSON.h` | 16394 | 同上 |
| `host/storage/third_party/cjson/LICENSE` | 1084 | MIT 许可原文 |
| `tests/unit/storage/CMakeLists.txt` | 48 / 2354 | 5 个测试目标 + CTest 注册 + `storage.schema_source_drift` 门禁 |
| `tests/unit/storage/test_repository.c` | 2983 / 138538 | 26 个测试函数 / 865 断言（含 round-6 的静默写入检测与 round-7 的 18 个坏结构夹具、官方 DDL 正对照 `test_official_ddl_positive_control`） |
| `tests/unit/storage/test_schema.c` | 505 / 31269 | 11 个测试函数 / 131 断言 |
| `tests/unit/storage/test_event_key.c` | 205 / 7913 | 7 个测试函数 / 80 断言 |
| `tests/unit/storage/test_u64.c` | 133 / 6040 | 6 个测试函数 / 90 断言 |
| `tests/unit/storage/test_regex.c` | 120 / 5128 | 6 个测试函数 / 46 断言 |

### 实现函数（对外只有契约里的六个端口）

契约 `contracts/include/locker_contracts.h`：

- `:190 typedef struct locker_repo locker_repo_t;`
- `:273 locker_status_t locker_repo_open(const locker_repo_config_t *config, locker_repo_t **out);`
- `:275 locker_status_t locker_repo_query(locker_repo_t *repo, const locker_repo_query_t *query, locker_repo_result_t *result);`
- `:277 void locker_repo_close(locker_repo_t *repo);`（NULL 安全）
- `:211 locker_status_t locker_repo_commit_change(locker_repo_t *repo, const locker_change_t *change);`
- `:213 locker_status_t locker_repo_import_events(locker_repo_t *repo, const locker_event_batch_t *batch, const locker_change_t *projection_change, locker_event_key_t *committed_cursor);`
- `:215 locker_status_t locker_repo_load_recovery(locker_repo_t *repo, const locker_repo_query_t *query, locker_recovery_t *result);`

模块内部（**不在契约里，也不对外承诺**）：`host/storage/include/locker/storage/{u64,event_key,regex,schema,embedded}.h` 的工具函数与 `src/repository_internal.h` 的事务/文档辅助函数。它们是仓储实现的私有组成，其他模块不得直接包含。

### 越界项

无。未自行决定借还成功（只存 Core 给的投影）、未发送设备动作（仓储无 socket/serial 依赖）、未改公共 schema 或状态枚举（`contracts/` 零改动）、未建 HTTP/UI 直连仓储的入口、未改 `scripts/`、`config/`、`firmware/`、`docs/stages/`。

`deployment/host/` 的改动**不是本卡越权**：审计 N4 指出该处的 vendor 状态与事实矛盾（lock 里仍写「尚未 vendor」、cJSON 消费卡仍是 S1-05），而 S1-02 是实际 vendor SQLite/cJSON 的卡，按「谁改动谁同步」由本卡更新 `deployment/host/dependencies.lock.json` 与 `deployment/host/README.md` 的事实描述；只改陈述、未改任何锁定版本号/摘要/许可条目，见 §8。

## 4. reuse

### 第三方依赖（先查官方/许可/版本/适配，再 vendor）

| 依赖 | 版本 | 许可 | 许可原文 | 归档 SHA-256 | 用途 |
|---|---|---|---|---|---|
| SQLite amalgamation | 3.54.1（`SQLITE_VERSION_NUMBER 3054001`，tag `version-3.54.1`） | Public Domain | `deployment/host/licenses/sqlite-PUBLIC-DOMAIN.txt` | `0a9c8e34…ad1f9`（2990966 B） | 单写仓储、事务、WAL |
| cJSON | 1.7.19（tag `v1.7.19`，commit `c859b25da02955fef659d658b8f324b5cde87be3`） | MIT | `deployment/host/licenses/cjson-LICENSE.txt` | `7fa616e3…454562`（356247 B） | 解析/序列化 RepositoryChange 与查询结果 |

两者都已记录在 `deployment/host/dependencies.lock.json`（S1-01 建立 lock shape；本卡按审计 N4 更新了 vendor 状态，见 §8）。本卡实测核验（脚本 `%TEMP%/s102_verify_vendor.py`）：

- 归档 SHA-256 与 lock 记录一致（sqlite match=true、cjson match=true）；
- 6 个 vendored 文件与归档内对应成员**逐字节相同**（`all_vendored_match=true`）；
- 许可原文已在仓库 `deployment/host/licenses/` 内，本卡未复制第二份。

选择 vendor 而非链接系统库的理由：VM 内无 `sqlite3.h` 且无外网、无 apt 源；`SQLITE_THREADSAFE=1` 等编译形态需要固定，系统包版本不可控。vendor 后 `locker_storage_thirdparty` 目标**故意不加** `locker_apply_warnings()`——第三方代码不在此修改，不应因项目 `-Werror` 而失败。

### 项目内复用

- `locker_common`（S1-01）：`locker_uuid_parse` / `locker_uuid_format` / `locker_uuid_equal`。
- `tests/unit/common/test_support.h`：`TEST_CHECK` / `TEST_CHECK_STATUS` / `TEST_CHECK_STRING` / `TEST_CHECK_U64` / `TEST_REPORT`。
- 根构建入口的选项、警告门禁、契约版本漂移门禁：全部沿用，未另建第二套。

### 没有新建的东西（避免临时框架）

- 没有引入 JSON 库之外的第二个 schema 真源：`src/schema_records.json` 是 `contracts/internal-data.schema.json` 的**机器抽取子集**（47 defs + 18 collections），抽取后已去 `description` 并排序，闭包完整（`MISSING_REFS=[]`）。
- 没有用 SQLite JSON1 做业务判断（JSON1 只被 cJSON 之外的校验路径间接使用，业务语义全在 C 侧）。
- 没有另造 DTO：所有出入参都是契约里的 `locker_change_t` / `locker_repo_query_t` / `locker_repo_result_t` / `locker_recovery_t` / `locker_event_batch_t`。

## 5. validation

### 5.1 命令与结果

**VMware 干净 guest（验收主环境）**，run_id=`20261024-213500`（round-8 审计整改后的最终快照；早前 run `20261024-055243` 因快照滞后于 `test_repository.c` 的 NUL 用例强化、`20261024-063018` 早于 round-2 整改、`20261024-114250` 早于 round-3 整改、`20261024-125905` 早于 round-4 整改、`20261024-135713` 早于 round-5 整改、`20261024-152903` 早于 round-6 整改、`20261024-174830` 早于 round-7 整改（缺 `verify_official_ddl` 的 `CREATE TABLE` 文本比对与 CHECK/FK/等长 DDL 夹具）、`20261024-193500` 早于 round-8 整改（缺 N-3 的 `build_audit_record_with_id` 正对照保真度），均作废，不作证据）：

```
SRC=$HOME/locker-workspace/S1-02/src
BUILD=$HOME/locker-workspace/build/S1-02/20261024-213500
LOGS=$HOME/locker-workspace/logs/S1-02/20261024-213500
ARTIFACTS=$HOME/locker-workspace/artifacts/S1-02
cmake -S "$SRC" -B "$BUILD" -DLOCKER_BUILD_HOST=ON -DLOCKER_BUILD_TESTS=ON -DLOCKER_WARNINGS_AS_ERRORS=ON
cmake --build "$BUILD" -j2
ctest --test-dir "$BUILD" --output-on-failure
```

VM 上卡源码目录是 `$HOME/locker-workspace/S1-02/src`（该 guest 的用户是 `china`，`vm-s102-run-summary.log` 记录的是 `/home/%USER%/...` 展开后的绝对路径）；顶层 `$HOME/locker-workspace/src` 只放跨卡共享的早期内容，**不含 `host/storage`**，故不能用它作源码根。快照 `s102_snapshot_r9.tar.gz` sha256=`f4279ba59ad7b013bdc9811a8e3f6eafc093e214ea3ea92a79ad92784b47c69f`（81796615 B，1113 个 tar 条目）；VM 端从 `source-manifest.txt` 生成 `manifest.sha256` 后 `sha256sum -c` 32/32 全 OK。**打包 → scp → VM 端 sha256 三处一致**，故 VM 跑的就是本地最终源码。

| 步骤 | 退出码 | 关键输出 | 证据 |
|---|---|---|---|
| 环境 | — | Ubuntu 26.04.1 LTS / Linux 7.3.0-rc5 x86_64 / ext4 /dev/sda2 / nproc=2 / cmake 4.2.3 / gcc 15.2.0 / make 4.4.1 / ctest 4.2.3 / Python 3.14.4 | `vm-s102-environment.log` |
| 快照指纹 | 0 | `MANIFEST_LINES=32`、`SHACHECK_OK=32`、`SHACHECK_FAILED=0`、`SRC_STRAY_ARTIFACTS=0`、`SRC_FILES=1016`、`TAR_ENTRIES=1113`、`EXTRACT_EXIT=0` | `vm-s102-source-verify.log`、`vm-s102-manifest.sha256`、`vm-s102-run-summary.log` |
| configure | **0** | `-- locker host build: contract 0.2.1`、`Found Threads: TRUE`、`CONFIGURE_WARNING_LINES=0` | `vm-s102-configure.log` |
| build | **0** | `BUILD_WARNING_LINES=0`（`-Werror` 下零警告） | `vm-s102-build.log` |
| ctest | **0** | `100% tests passed, 0 tests failed out of 12`，Total 0.51s | `vm-s102-ctest.log` |
| 逐测试 | 全 0 | 11 个测试二进制断言数见下（第 12 个 CTest 是 Python 漂移门禁，无断言计数） | `vm-s102-unit-tests.log` |
| ASan+UBSan | 全 0 | 12/12 Passed（1.37s）、`ASAN_REPORTS=0` | `vm-s102-asan-{configure,build,ctest}.log` |
| TSan | 全 0 | 12/12 Passed（1.92s）、`TSAN_HITS=0` | `vm-s102-tsan-{configure,build,ctest}.log` |
| 四项设计检查 | 全 0 | 见 5.3 | `vm-s102-design-checks.log` |

> **2026-10-25 更新（用户 m19922 指令）**：本卡原先另有一组 WSL2 快速迭代日志（`configure.log`、`build.log`、`ctest.log`、`unit-tests.log`、`asan-*`、`tsan-*`、`design-checks.log`、`environment.log`、`run-id.txt`、`source-verify.log`、`mutation-batch.log`、`mutation-joint-m26c.log`、`manifest-check.log`、`evidence-run-wsl.sh` 共 18 个），已按指令全部删除。本卡 Linux 证据只有 VMware 干净 guest 一组（`vm-s102-*`）；WSL2 产物不再作为任何结论的依据，删除不影响上面的 VM 结果。

**断言数（VMware 干净 guest 实测）**

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
| `storage.test_repository` | **865** |
| `storage.test_schema` | 131 |
| `storage.test_u64` | 90 |
| **合计** | **1717** |

### 5.2 验收项逐条映射

| 卡原文验收 | 实现 | 测试 |
|---|---|---|
| 同一事件重复导入不重复推进 | `repository.c:1635` 先 `db_document_exists` 去重；`new_count==0`（`:1653`）时 `rollback` 并返回已存游标，**不改投影、不推进游标**；若该页 `gap_before_page=true` 则先锁存 `continuity_ok=false` 再返回（round-2 B1） | `test_import_events_dedup`（重复页第二次导入返回同一游标、投影 revision 不变、documents 数不变） |
| 失败事务不留下半个占位/outbox | `commit_change` 单事务：parse → `BEGIN IMMEDIATE` → `apply_change`（checks+mutations+revision）→ commit，任一步失败整单 rollback | `test_failed_transaction_is_atomic`（注入非法 mutation 后 revision 与 documents 均不变） |
| 两个竞争申请只能一个成功 | `slot_guards` 的 `ABSENT` check + `expected_snapshot_revision`，在 `BEGIN IMMEDIATE` 内校验 | `test_competing_slot_guard`（同进程顺序竞争）+ `test_concurrent_slot_guard`（**两个 fork 进程**打开同一库文件竞争：恰好 1 个 OK、1 个 CONFLICT，revision==1、slot_guards 仅 1 条） |
| 数据库重开仍保留有效借用/未决操作/幂等结果 | 全部落 `documents` 表，`snapshot_revision` 在 `meta` | `test_persistence_across_reopen`（一次提交 6 类记录 → close → reopen → 逐集合查询断言值；重开后 revision 不倒退） |
| 游标不能越过缺口/倒退/把 10 排在 2 前 | `cursor_is_contiguous`（`repository.c:1436`）、`event_keys_adjacent`（`repository.c:1416`）+ `locker_storage_event_key_compare` 按 u64 数值比较；文档 key 用 20 位补零 | `test_import_events_gap_and_regression`（跳号 CONFLICT 且不落库、页内乱序 INVALID、boot 倒退 CONFLICT、越过 `last_committed` INVALID）+ `test_cursor_order_and_pagination`（10 排在 2 之后） |
| WAL、`synchronous=FULL` | `locker_repo_open` 里 `PRAGMA journal_mode = WAL` / `PRAGMA synchronous = FULL`（`repository.c:2362`、`repository.c:2366`；`sqlite3_busy_timeout` 在 `:2355`、`foreign_keys` 在 `:2359`） | WAL：`test_wal_enabled`（另开连接读 `PRAGMA journal_mode` == "wal"，且 `-wal` 文件存在）。**`synchronous=FULL` 无运行时断言**——它是连接级 pragma，无法从另一连接观测，见第 6 节 |
| 首次初始化与损坏/迁移失败区分，失败不能自动清库 | `locker_repo_open`（`repository.c:2319`，初始化判定块从 `:2376` 起）：`db_table_exists(documents)`（`:2380`）+ `db_object_exists`（`:248`）+ `db_user_object_count`（`:279`，`NOT GLOB 'sqlite_*'`）区分「真正空目标」「同名非表对象」「有痕迹但结构不全」；首次 DDL+元数据在 `BEGIN IMMEDIATE` 事务里做，`COMMIT` 前 `verify_schema_tables`，失败 `ROLLBACK` 且不动旧库；已初始化走 `verify_schema_tables`（`repository.c:2037`：先 `verify_table_columns`（`:2061`）逐列 `pragma_table_xinfo`（含 `hidden`），再 `verify_key_index`（`:2065`）用 `pragma_index_list`/`pragma_index_xinfo` 核验唯一索引与 BINARY 升序，再 `verify_official_objects`（`:2086`）核验用户对象恰好是三张受控表，最后 `verify_official_ddl`（`:2244`）逐字节比对三张表的 `CREATE TABLE` 文本）+ `verify_schema_metadata`（`repository.c:1765`）；`run_quick_check`（`repository.c:2296`） | `test_open_failure_paths`（损坏文件原样保留、`initialize_empty=true` 也不覆盖；schema_version 改 2 → CONFLICT；删 migrations 行 → CONFLICT；只有 meta 表的半初始化库 → CONFLICT 且用户表仍原样；无 documents 表且不初始化 → NOT_READY） |
| u64 按已校验十进制文本存储并按数值比较 | `locker_storage_u64_parse/format/compare_text`（`src/u64.c`，薄适配 `locker_common` 的 `numeric`）；schema 校验器对 `#/$defs/U64` 走 `locker_u64_parse_decimal` 判 OK（含溢出校验） | `test_u64`（零、最大值、越界 `18446744073709551616`、21 位、前导零、符号、空白、空串、NULL；`"2" < "10"`）+ `test_schema`（U64 用 JSON 数字类型、"03"、超 UINT64_MAX 全部拒绝） |
| 按 internal-data.schema.json 实现受控集合 | `src/schema_records.json` 由契约机器抽取，18 collections + 47 defs 闭包完整；`schema.c` 逐关键字校验、未知关键字 fail closed | `test_schema`（131 断言：required/additionalProperties/enum/const/pattern/format/minLength/maxLength/minItems/maxItems/anyOf/allOf/if-then/集合与 value 同型） |

### 5.3 四项设计检查（只证明设计一致性，不代替构建/硬件）

| 命令 | 退出码 | 关键输出 |
|---|---|---|
| `python -B scripts/check_contracts.py` | **0** | status PASS、contract_version 0.2.1、public_routes 24、internal_ports 82、private_rpc_methods 28、resolved_schema_refs 453、resolved_internal_refs 332、uart_requests 11、max_wire_payload 469、`c_declaration_syntax "NOT_RUN (optional pycparser unavailable)"`、hardware_profile UNVERIFIED、nominal_bom_cny 198.59、business_or_hardware_tested false |
| `python -B scripts/check_stage_plan.py` | **0** | status PASS、stages 5、task_cards 45、`active_task null`、`implementation_cards_done 2`（S1-01 + 本卡）、runtime_or_hardware_tested false |
| `python -B scripts/check_vision_contracts.py` | **0** | status PASS、vision_schema_refs 58、camera_owned false、rknn_available false |
| `python -B scripts/check_feasibility.py` | **0** | status PASS、async_request_contexts 3、two_wire_break_detection NOT_CLAIMED |

VM 内四项检查同样全 0（`vm-s102-design-checks.log`），VM 的 Python 3.14 未装可选依赖 pycparser，因此 `c_declaration_syntax` 输出 `"NOT_RUN (optional pycparser unavailable)"`，status 仍 PASS。**这不是本卡的失败项，但也意味着契约 C 声明的语法检查本次未执行**，如实登记在 §5.5。

设计检查的时点要说清，不能混作同一次：
- VM canonical 批次的 `vm-s102-design-checks.log`（run_id `20261024-213500`）在收尾改状态**之前**，因此 `check_stage_plan` 报 `active_task "S1-02"`、`implementation_cards_done 1`，属历史快照，不能拿来证明收尾后的状态。
- `2026-10-24T22:25:39+08:00` 这次是收尾把 `stage-plan.json` 改成 S1-02 `DONE` / `active_task=null` / `recommended_next=S1-03` **之后**的重跑，四项仍全 0，`check_stage_plan` 输出 `active_task null`、`implementation_cards_done 2`，证明状态迁移没有破坏索引一致性。证据目录里的 `check_*.json` / `check_*.log` 以这次为准；按用户 m19922 指令，原 WSL2 那一组日志已删除，这次重跑的命令与结果由 `check_*.log` 与本节表格保存。

### 5.4 测试非空洞性（mutation）

脚本（round-8 整改后最终版，原运行路径 `C:/Users/Public/s102_r9mut.py`，**已作为正式证据保留为 `mutation-batch.py`**，批次完成于 2026-10-24T21:20+08:00；与 round-7 那批的唯一差别是重新绑定 N-3 改过的 `test_repository.c`，变异定义与点位未变）：把 `CMakeLists.txt + contracts + host + tests` 复制到 `$HOME/s102_r9mut/<name>`，对被测源码做**单个定向变异**（m15b、m26b 与 8 个 `_plus_ddl_off` 是显式标注的多点联合），独立构建目录 configure+build+ctest，再从**同一次 `ctest --output-on-failure` 的 stdout** 抽取断言级 FAIL 行（`mutation-batch.py:162-177`，没有第二次单独执行二进制；round-8 N-1 ④ 改正旧表述）。变异前用 Python 校验替换串在文件中**恰好出现 1 次**（`count != 1` 即 `PATCH_PATTERN_COUNT` 中止，防静默改错位置）。**42 个点位的 configure 与 build 退出码全部为 0**（`patch=OK configure=0 build=0`），因此红测只能归因于语义变异，不可能来自构建失败。证据 `mutation.log`（42 个 section + 1 个三点联合附录段，26243 B / 约 690 行）开头记录本批被测源文件的 `SOURCE_FINGERPRINT`（sha256 前 12 位）：`CMakeLists.txt f9e1964ee544`、`repository.c b9ab2abaa41e`、`schema.c 19bd5b4f9e24`、`u64.c 875036a0abbf`、`event_key.c 2229c561f455`、`regex.c b01a90e58f18`、`test_repository.c f1cc609cf4cf`、`test_schema.c 4952adc2876f`、`test_u64.c 6919ee375023`、`test_event_key.c 159254c9091d`、`test_regex.c 8bb903aa8b76`，证明这一批确实打在最终源码上而不是旧快照上。分母是 `storage.repository` **865**、`storage.regex` 46、`storage.u64` 90、`storage.schema` 131（m9b 因 root 为 NULL 只跑到 850）；行号均指 `tests/unit/storage/test_*.c`。表里的失败数已扣掉测试二进制尾部那条 `FAIL storage.repository (N of M checks failed)` 汇总行（批处理日志的 `assertion_failures=` 把它也算进去了，实际断言数 = 该值减 1），逐条 FAIL 行由程序从批处理日志解析，无手工转抄。

| 变异 | 位置 | 结果 |
|---|---|---|
| m1_revision：`if (current != expected)` → `if (0)` | `repository.c:713` | **5/865**（`:532 :542 :896 :986 :991`）——**首轮 B1** |
| m2_dedup：`status == OK && !exists` → `status == OK` | `repository.c:1636` | **5/865**（`:945 :947 :1474 :1478 :1487`）——**首轮 B1** |
| m3_adjacent：`== previous->event_seq + 1u` → `>=`（允许跳号） | `repository.c:1422` | **9/865**（`:1028 :1033 :1048 :1049 :1177 :1183 :1193 :1194 :1292`）——**首轮 B1** |
| m4_u64_fallback：`if (left_length != right_length)` → `if (0)` | `u64.c:51` | **2/90**（`test_u64.c:105` `"7" vs "07x"`、`:109` `"" < "0"`）——非规范文本回退分支 |
| m5_schema_additional：`if (cJSON_IsFalse(additional) != 0)` → `if (0)` | `schema.c:413` | **2/131**（`test_schema.c:69 :273`） |
| m6_sync：`PRAGMA synchronous = FULL` → `OFF` | `repository.c:2366` | **未捕获（12/12 PASS）**——连接级 pragma，另一连接不可观测；如实登记，不伪造断言 |
| m7_gap_latch：`if (batch->gap_before_page)` → `if (0)` | `repository.c:1716` | **2/865**（`:1378 :1386`，即 `!continuity`）——**首轮 B1** |
| m8_trace_guard：「有痕迹库」守卫 `!documents_is_table && user_objects != 0u` → `if (0)` | `repository.c:2393` | **3/865**（`:2866`（`bare.db` 双模式）、`:2198`（`sqlitex_` 前缀对象）、`:2595`（`struct_bare.db`））——**首轮 B3 判定**，不是 DDL 事务回滚 |
| m9_page_budget：`reserved_next_key = KEY_MAX*6u + 8u` → `8u` | `repository.c:975` | **未捕获（12/12 PASS）**——该预留是 **1160 B**（`192*6+8`）的保守余量，现有分页用例的页长离 65535 上限还有余量，少预留仍装得下；如实登记 |
| m9b_doc_bytes：`document_bytes = strlen(document_text)` → `key + value` 长度和 | `repository.c:1099` | **4/850**（`:1775 :1778 :1819 :1822`，期望 status 0 得 9=`NO_SPACE`）——**首轮 B4 核心**。分母 850 不是 865：失败后 `root == NULL`，后续依赖 root 的断言被跳过 |
| m10_nul_guard_raw：`if (json[index] == '\0')` → `if (0)` | `schema.c:42` | **6/865**（`:1881 :1886`，各 3 次）——**首轮 B5** |
| m11_parse_nul：parse_json 的 `locker_storage_json_text_has_embedded_nul(...)` 守卫 → `if (0)` | `repository.c:502` | **17/865**（`:1881 :1886` 各 8 次 + `:1890`）——**首轮 B5** |
| m12_boundary_structure：三行半零哨兵结构校验整段 → `if (0)` | `repository.c:1560-1562` | **4/865**（`:1249 :1256 :1284 :1292`）——**round-2 R2-B2** |
| m13_first_available_zero：`is_zero(&batch->first_available)` 拒绝 → `if (0)` | `repository.c:1573` | **3/865**（`:1271 :1284 :1292`）——**round-2 R2-B2** |
| m14_gap_latch_early：重复页早退 `if (!batch->gap_before_page)` → `if (1)` | `repository.c:1658` | **2/865**（`:1478 :1487`，即 `!continuity`）——**round-2 R2-B1** |
| m15b_view_double：`db_object_exists` 只认 table + 视图守卫 `if (0)` + 痕迹守卫 `if (0)` 三点联合 | `repository.c:256 + 2388 + 2393` | **4/865**（`:2866`（`bare.db` 双模式）、`:2125`（视图 `open(false)` 从 CONFLICT 变 NOT_READY）、`:2198`、`:2595`）——**round-2 R2-B3**，见边界第 5 条 |
| m16_sqlite_prefix：`NOT GLOB 'sqlite_*'` → `NOT LIKE 'sqlite_%'` | `repository.c:291` | **1/865**（`:2198`）——**round-3 B-1**（下划线是 LIKE 通配符，`sqlitex_audit` 被漏数） |
| m17_verify_columns：`verify_table_columns(db, &k_tables[index])` 调用 → `LOCKER_STATUS_OK` | `repository.c:2061` | **未捕获（12/12 PASS）**——被 round-7 的 DDL 文本比对兜底遮蔽；联合点 `m17_verify_columns_plus_ddl_off` 捕获 **32/865** |
| m18_verify_key_index：`verify_key_index(db, &k_tables[index])` 调用 → `LOCKER_STATUS_OK` | `repository.c:2065` | **未捕获（12/12 PASS）**——同上；联合点 `m18_verify_key_index_plus_ddl_off` 捕获 **24/865** |
| m20b_any_matching_index：`if (!matches) { CONFLICT; break; } ++matched_unique;` → `if (matches) { ++matched_unique; }`（任一匹配索引即放行） | `repository.c:2010` | **未捕获（12/12 PASS）**——同上；联合点 `m20b_any_matching_index_plus_ddl_off` 捕获 **16/865** |
| m21b_skip_column_collation：列排序规则检查尾部 `... \|\| !ascii_text_equal(collation, "BINARY"))` → `... \|\| 0)` | `repository.c:1897` | **未捕获（12/12 PASS）**——同上；联合点 `m21b_skip_column_collation_plus_ddl_off` 捕获 **16/865** |
| m22_skip_extra_unique_index：不匹配的唯一索引 `CONFLICT; break` → `continue` | `repository.c:2010` | **未捕获（12/12 PASS）**——同上；联合点 `m22_skip_extra_unique_index_plus_ddl_off` 捕获 **16/865**；与 m20b 是同一 if 块的两种改法，不当作两个独立缺陷 |
| m25_regex_bounded_zero：零下界分支 `produced = 1u` → `produced = 0u` | `regex.c:531` | **2/46**（`test_regex.c:80` `{0,1}` 接受 `"aa"`、`:93` `{0,19}` 多接受一位）——**round-4 N-3**，见边界第 8 条 |
| m26_partial_unique：partial UNIQUE fail-closed `CONFLICT; break;` → `continue;` | `repository.c:2000` | **未捕获（12/12 PASS）**——被对象清单与 DDL 文本两道兜底遮蔽；联合点 `m26_partial_unique_plus_ddl_off` 捕获 **12/865**，三点联合（m26+m28+m33，见边界第 12 条）捕获 **20/865** |
| m27_regex_min_seed：正下界播种 `if (min_value > 0u) { produced = min_value; }` → `if (0) {` | `regex.c:562` | **1/46**（`test_regex.c:28`：`^[0-9a-f]{8}$` 等 `{n}` 分支多生成一个 atom，9 位 hex 被错误接受）——round-4 N-3 的边界对照 |
| m28_official_objects：`status = verify_official_objects(db);` → `status = LOCKER_STATUS_OK;`（关掉官方对象清单核验） | `repository.c:2071` | **4/865**（`:2250` ×2、`:2475 :2476`）——**round-6 B-1**（预存 `RAISE(IGNORE)` 触发器与未知用户对象被放行） |
| m29_table_info：`pragma_table_xinfo` 改回 `pragma_table_info` 且 `hidden` 恒 0 | `repository.c:1856` | **未捕获（12/12 PASS）**——同上；联合点 `m29_table_info_plus_ddl_off` 捕获 **16/865**（`:2504 :2506` 即 `SQL_GENERATED_APPENDED` 夹具） |
| m30_hidden_only：`if (hidden != 0)` 隐藏列拒绝 → `if (0)` | `repository.c:1881` | **未捕获（12/12 PASS）**——同上；联合点 `m30_hidden_only_plus_ddl_off` 捕获 **16/865**（`:2490 :2491` 即 `SQL_GENERATED_COLUMN` 夹具） |
| m31_put_changes：`db_put_document` 的 `if (sqlite3_changes(db) != 1)` → `if (0)` | `repository.c:438` | **4/865**（`:2623 :2628 :2642 :2647`，即被 `RAISE(IGNORE)` 静默吞掉的 PUT 又被当成功）——**round-6 B-1** |
| m32_delete_changes：`if (status == OK && existed && sqlite3_changes(db) != 1)` → `existed && 0` | `repository.c:477` | **3/865**（`:2680 :2684 :2692`）——**round-6 B-1**（DELETE 失败时 revision 仍为 1、文档仍为 1，只有 PUT 失败时文档为 0） |
| m26b_partial_plus_objects：m26 + m28 两点联合（partial UNIQUE 放行 **且** 对象清单核验关掉） | `repository.c:2000 + 2070` | **4/865**（`:2250` ×2、`:2475 :2476`）——这 4 条**全部来自 ⑪ trigger 夹具**，不含 `:2460 :2461`，所以它证明的是「关掉对象清单后 trigger 夹具变红」，**不能**当作 partial 分支的可观测证据（round-8 N-1 ① 订正）；partial 的目标证据只有三点联合 |
| m33_ddl_skipped：`return verify_official_ddl(db);` → `return LOCKER_STATUS_OK;` | `repository.c:2077` | **12/865**（`:2250` ×6、`:2542 :2543 :2554 :2555 :2567 :2568`）——**round-7 B-1**（CHECK / FOREIGN KEY / 等长不同文本 DDL 三个夹具） |
| m34_ddl_length_only：`memcmp(stored, expected, expected_length) != 0` → `0`（只比长度） | `repository.c:2281` | **4/865**（`:2250` ×2、`:2567 :2568`）——只被 `SQL_DDL_SAME_LEN_DIFFERENT_TEXT`（等长不同内容）捕获 |
| m34b_ddl_text_not_compared：整个「长度或内容不等」条件 → `if (stored == NULL)` | `repository.c:2281` | **12/865**（与 m33 相同的 12 行）——该 edit 把长度比较与内容比较**一起**去掉，只剩 NULL 检查，因此它是比 m34 更强的形式，不能描述为「保留长度分支」 |
| m17_verify_columns_plus_ddl_off：m17 + m33 联合 | `repository.c:2061 + 2077` | **32/865**（`:2250` ×16、`:2438 :2439 :2442 :2443 :2446 :2447 :2490 :2491 :2504 :2506`、`:2542 :2543 :2554 :2555 :2567 :2568`）——按夹具拆分：**16 helper + 6 条 m33 DDL 夹具 + 10 条目标夹具**（扣掉 m33 的 12 条后为 20，与 round-8 审计口径一致） |
| m18_verify_key_index_plus_ddl_off：m18 + m33 联合 | `repository.c:2065 + 2077` | **24/865**（`:2250` ×12、`:2521 :2523 :2527 :2529 :2533 :2535`、`:2542 :2543 :2554 :2555 :2567 :2568`）——**12 helper + 6 条 m33 DDL + 6 条目标夹具**（扣 12 后为 12） |
| m20b_any_matching_index_plus_ddl_off：m20b + m33 联合 | `repository.c:2010 + 2075` | **16/865**（`:2250` ×8、`:2521 :2523`、`:2542 :2543 :2554 :2555 :2567 :2568`）——**8 helper + 6 条 m33 DDL + 2 条目标夹具**（扣 12 后为 4） |
| m21b_skip_column_collation_plus_ddl_off：m21b + m33 联合 | `repository.c:1897 + 2077` | **16/865**（`:2250` ×8、`:2442 :2443`、`:2542 :2543 :2554 :2555 :2567 :2568`）——**8 helper + 6 条 m33 DDL + 2 条目标夹具**（扣 12 后为 4） |
| m22_skip_extra_unique_index_plus_ddl_off：m22 + m33 联合 | `repository.c:2010 + 2076` | **16/865**（与 m20b 联合点相同的 16 行）——**8 helper + 6 条 m33 DDL + 2 条目标夹具**（扣 12 后为 4） |
| m26_partial_unique_plus_ddl_off：m26 + m33 联合 | `repository.c:2000 + 2076` | **12/865**（`:2250` ×6、`:2542 :2543 :2554 :2555 :2567 :2568`）——**6 helper + 6 条 m33 DDL + 0 条目标夹具**，**不含** `:2460 :2461`：两点联合下 partial 夹具仍被对象清单拒，这 12 条全部来自 m33，不能算 partial 覆盖；partial 的独立可观测需三点联合 |
| m29_table_info_plus_ddl_off：m29 + m33 联合 | `repository.c:1856 + 2077` | **16/865**（`:2250` ×8、`:2504 :2506`、`:2542 :2543 :2554 :2555 :2567 :2568`）——**8 helper + 6 条 m33 DDL + 2 条目标夹具**（扣 12 后为 4） |
| m30_hidden_only_plus_ddl_off：m30 + m33 联合 | `repository.c:1881 + 2077` | **16/865**（`:2250` ×8、`:2490 :2491`、`:2542 :2543 :2554 :2555 :2567 :2568`）——**8 helper + 6 条 m33 DDL + 2 条目标夹具**（扣 12 后为 4） |

**42 次运行中 32 次变红、10 次如实未捕获（m6、m9，以及被 round-7 DDL 文本比对遮蔽的 m17/m18/m20b/m21b/m22/m26/m29/m30 八个单点）**，说明测试不是恒真；七轮审计的每个阻塞项都有定向变异在守（首轮 B1→m3/m7、B3→m8、B4→m9b、B5→m10/m11；round-2 R2-B1→m14、R2-B2→m12/m13、R2-B3→m15b；round-3 B-1→m16、B-2→m17/m18 及其联合点；round-4 B-1→m20b/m21b/m22 及其联合点、N-3→m25/m27；round-5 B-1→m26 / m26b / 三点联合；round-6 B-1→m28/m31/m32、B-2→m29/m30 及其联合点；round-7 B-1→m33/m34/m34b）。**计数口径只有三档，不折算「独立缺陷数」**：42 次运行、32 次变红、10 次未捕获。m20b 与 m22 打在同一段代码（`repository.c:2010`）的两种改法上、失败集合完全相同；m17/m18/m20b/m21b/m22/m26/m29/m30 的单点与联合点打在同一处代码，联合点只是把兜底同时关掉，不构成新缺陷。

必须说清的边界（第 1–3 点由 round-2 R2-N3/N4 指出，第 4–6 点由 round-3 N-1 指出，第 7 点由 round-4 N-1 收尾指出，第 8 点由 round-5 N-1 指出，第 9–11 点由 round-6 N-1 指出，第 12 点由 round-7 N-1 与 B-1 整改共同要求）：

1. **m4 的锚点没换，换的是输入可达性**（round-4 N-2 勘误）。锚点一直是 `u64.c:51` 的非法文本回退分支；旧版没有任何用例喂非规范文本，合法输入根本不进这个分支，所以当时的捕获记录不能证明当前实现被覆盖。现新增 `test_compare_text_noncanonical` 专门喂非规范文本；两条断言（`"7" vs "07x"`、`"" < "0"`）就是它变红的证据。
2. **m8 原名 `init_tx` 是误称**，它打的是「有痕迹但结构不全的库必须 CONFLICT」这一判定，**不是** `BEGIN IMMEDIATE` / DDL / `ROLLBACK` 本身。初始化事务中途失败的回滚目前**没有注入式回归**（夹具需要在 DDL 执行到一半时让第二条语句失败，SQLite 没有可插桩的钩子），如实登记为未覆盖，不据 m8 声称已证。
3. **m9b 的分母是 850**。失败后 `root` 为 NULL，后续依赖 `root` 的断言被跳过，因此不能写成 4/865；也不能把成功批次的 865 当分母。
4. **m12 的第一版是无效变异**：替换串把三行条件截断成语法错误（`BUILD_EXIT=2`，根本没跑到测试）。改为整段三行替换成 `if (0) {` 后重建才捕获；这也说明「变异失败」必须区分「测试没覆盖」与「变异本身没编译」。本轮 42 个点位全部 `configure=0 build=0`，即已排除该干扰。
5. **m15b 不能证明「首次 `open(true)` 假成功」**（round-5 N-1 ① 改正）：`test_documents_view_rejected` 的首次 `open(true)` 在 `test_repository.c:2122`，没有 FAIL；唯一视图 FAIL 是 `:2125` 的 `open(false)`（CONFLICT 变 NOT_READY）。即使关掉对象类型/视图/痕迹三处，初始化前的 `verify_schema_tables`（`repository.c:2409`）仍核列，视图列 type 为空、notnull=0、pk=0，会被拒并 ROLLBACK。所以 m15b 的 4 条红行证明的是**返回分类与有痕迹保护有回归**，不是「假成功被捕获」；生产路径本身静态拒绝视图，不存在当前视图漏洞。
6. **m10/m11 的红行不用「跨用例 revision 连带」解释**（round-5 N-1 ⑤ 指出旧解释已过期）：现每个 NUL 负例都独立建库、先建立 revision=1 基线，`:1881`/`:1886` 的各次命中是同一用例内的预期后置检查（status 与 documents 数），不是前一个用例推进了 revision。各类 NUL 的独立性另由 4a 正对照（干净库 `nul_pos.db` 必须 OK）与两个带合法 mutations 的裸字节用例保证。
7. **m23/m24 已从批次移除**（round-4 N-1 登记为重复点位、round-5 整改时删除）：它们与 m17/m18 同锚点、失败行完全相同，属重复执行，保留会虚增捕获数。同理 m15_object_type 单点已移出批次（round-3 N-1），其残留目录单独重跑 0 失败，不列为捕获项。**round-6 批次里的 `m29b_xinfo_to_info_only` 也已删除**（round-7 N-1 ①）：它的 edit 与 `m29_table_info` 逐字节相同（`pragma_table_info` 没有 hidden 列，「只回退列视图、保留 hidden 读取」无法用不同 SQL 表达），是一次重复执行而不是第二个缺陷；保留会让「42 次运行」虚增。
8. **m25 的旧描述是错的，已按实际变异重写**（round-5 N-1 ④）：变异点是 `regex.c:531` 零下界分支把 `produced` 从 `1u` 改成 `0u`（不是旧日志写的「循环回退到 index=1」），变红的是 `test_regex.c:80 :93` 的 `{0,1}`/`{0,19}` 断言；`test_regex.c:28` 的正下界缺陷由 m27（`regex.c:562` 播种 `produced`）单独捕获。
9. **m26 的「未捕获」是遮蔽，不是死代码**（round-6 N-1 ③ + round-7 N-1 ②）：真实调用顺序是 `verify_table_columns`（`repository.c:2061`）→ `verify_key_index`（`:2065`）→ `verify_official_objects`（`:2071`）→ `verify_official_ddl`（`:2077`）。原版 partial 检查在 `:2000` 就拒；把它改成 `continue` 后，夹具仍被**后面**的对象清单与 DDL 文本再拒一次，所以单点全绿。**不能把它说成「对象清单在键索引之前先拒掉」**——那是错误的先后关系。**拆分口径（round-8 N-1 ①②③ 订正）**：m26b（m26+m28）变红的 4 条全是 ⑪ trigger 夹具（`:2475 :2476`）与 `:2250`×2，**没有** `:2460 :2461`；m26+DDL-off 的 12 条也全部由 m33 的 CHECK/FK/等长夹具贡献，目标夹具行数为 **0**。真正把 partial 分支变成可观测的是三点联合 m26+m28+m33：20 条红行中 **4 条**归 partial（`:2460 :2461` 与 `:2250`）、**4 条**归 trigger、**12 条**归 m33 的 DDL 夹具。因此只能说「在同时关掉对象清单与 DDL 文本两道兜底后，partial 的 fail-closed 分支确实会独立产生断言失败」，不能反过来断言它在全部防线开启时不可替代。
10. **补充/联合点位不算新的独立缺陷**：`m26b`、`m34b` 与 8 个 `_plus_ddl_off` 是为把「被遮蔽的防线」变成可观测而设计的联合形式，与 m26、m34、m17/m18/m20b/m21b/m22/m29/m30 打在同一处代码。上表把它们单独列出只为让归因透明；计数口径仍是 42 次运行 / 32 次变红 / 10 次未捕获，不因这些补充点把捕获数说高。
11. **`assertion_failures=` 与真实断言数差 1**：批处理解析的是测试二进制输出里所有以 `FAIL ` 开头的行，其中包括尾部那条 `FAIL storage.repository (N of M checks failed)` 汇总行。因此表中失败数 = 批处理 `assertion_failures` 减 1，且与逐条 FAIL 行完全对应；后续任何复核都应先做这个减 1，否则会把每个点位都虚增 1 条。
12. **round-7 的 DDL 文本比对让 8 个结构单点失去独立可观测性**（round-7 N-1 + B-1 整改要求）：新增的 `verify_official_ddl`（`repository.c:2244`）在最后把 `sqlite_master.sql` 与唯一 `schema-v1.sql` 逐字节比对，于是任何「只改坏一项结构检查」的变异都被它兜住，套件仍绿。这不是防线冗余，而是**兜底层次**：单点未捕获如实登记，联合点证明每条防线仍必要。partial UNIQUE 需要三点联合（`m26` + `m28` 关对象清单 + `m33` 关 DDL 文本）才观测到 `struct_partial.db` 的 `:2460 :2461`，实测 20/865（`:2250` ×10、`:2460 :2461`、`:2475 :2476`、`:2542 :2543 :2554 :2555 :2567 :2568`）——其中只有 4 条归 partial，另 4 条归 trigger、12 条归 m33；联合红测只证明「关掉兜底后的可观测行为」，不单凭它断言每道防线在全部防线同时开启时不可替代（round-8 N-1 ③）。


### 5.5 NOT_RUN（环境/硬件不具备，不伪造）

| 项 | 原因 |
|---|---|
| RK 存储介质掉电可靠性 | 无 RK 板、无断电测试设备；本卡只验证到「WAL + synchronous=FULL 的 SQLite 语义」与「正常 close/reopen 后数据仍在」层面（`test_persistence_across_reopen`），**不声称已验证掉电持久性** |
| 契约 C 声明的语法检查（`check_contracts.py` 的 `c_declaration_syntax`） | 需要可选依赖 pycparser；VM 的 Python 3.14 未安装，输出 `NOT_RUN (optional pycparser unavailable)`，status 仍 PASS。未安装依赖来「凑」这条输出 |
| CMake `storage.schema_source_drift` 的 Windows 侧执行 | 只在 Linux 上跑过（VM 干净 guest）；Windows 原生 CMake 未验证（本卡验收环境是干净 Linux） |
| `synchronous=FULL` 的运行时断言 | 连接级 pragma，另一连接不可观测；只在代码路径与 `handoff.md` 说明，无运行时断言 |
| SQLite 在真实 eMMC/SD 上的 fsync 时延与掉电原子性 | 同上，需真机 |
| 业务闭环（借还全流程） | 属 S1-04 及以后；本卡只提供仓储原语 |
| Keil/STM32/RK/Android/NPU/相机/UART/柜锁实测 | 属其他卡与真机；本卡不涉及 |
| 32 位 / ARM 交叉编译 | 未做；本卡只在 x86_64 Linux 验证 |
| CMake 3.16 最低版本下限 | 实测用的是 4.2.3；未在 3.16 上跑过 |

### 5.6 审计整改对照（B1–B5 / N1–N5）

首轮独立审计（`audit.md`，2026-10-24）结论 FAIL。下表逐条给出整改落点与守它的回归；**审计原文的结论不被本表修改**，是否闭环由复审判断。

| 编号 | 审计指出的缺陷 | 整改落点 | 守它的回归 |
|---|---|---|---|
| **B1**/P1 | 游标连续性把「严格递增」当「连续」：页内只比相邻、`stored` 分支直接继承 `continuity_ok`、`cursor_is_contiguous` 用 `<=` 允许跳号、同 boot 水位可倒退 | `event_keys_adjacent`（`repository.c:1416`）要求同 boot `seq == prev+1`、换 boot `boot > prev && seq == 1`（**允许跳 boot 号**，那次启动可能没落事件）；`cursor_is_contiguous`（`repository.c:1436`）改为复用同一判据；import 预扫对页内每对相邻事件调用它；`stored_exists` 分支追加 `first_new <= stored_through → CONFLICT`（水位不倒退）；`gap_before_page` 被消费并锁存 `continuity_ok = false`（`:1716`） | `test_import_page_boundaries`（页内缺号 1,3 → INVALID）、`test_import_continuity_latching`（已有游标收到 gap 页 → 落库 false 且后续页洗不白；同 boot 历史回填 → CONFLICT；boot 倒退 → CONFLICT）、`test_import_backfill_rejected`、`test_boot_jump_keeps_continuity`（boot 1 → boot 5 接受且 `continuity_ok` 保持 true） |
| **B2**/P1 | `last_committed` 上界只在非零时检查，且比较先比 device_id/epoch，零游标或另一设备可绕过 | 预扫后新增批次边界块（`repository.c:1554-1590`）：`last_committed` 为零 → INVALID；必须与首条同 device+epoch；`last_key <= last_committed`；`first_available` **必须非零**且同日志、`first_key >= first_available`；`next_after` 必须同日志且 `== last_key`。删除了旧的「非零才比较」块 | `test_import_page_boundaries` 里对同一页逐项破坏边界（全零 / 异 device / 异 epoch / 越过水位 / `next_after` 不一致 / `first_available` 越界）→ 六次 INVALID 且事件数、水位、`continuity_ok` 均不变 |
| **B3**/P1 | 只按 `documents` 表是否存在判断首次初始化；`CREATE TABLE IF NOT EXISTS` + `INSERT OR IGNORE` 会保留旧 `revision=7`；DDL 无显式事务 | `locker_repo_open`（`repository.c:2319`，判定块 `:2376`–`:2395`）改用 `db_table_exists("documents")` + `db_object_exists`（`:248`，任意对象类型）+ `db_user_object_count`（`:279`，`name NOT GLOB 'sqlite_*'`，round-3 B-1 修正）：同名非表对象或任意用户对象存在 → CONFLICT（有痕迹，**不删库**）；真正空库且 `initialize_empty` 时把 DDL + `verify_schema_tables`（`:2037`）+ `verify_schema_metadata`（`:1765`）包进 `BEGIN IMMEDIATE`/`COMMIT`，失败 `ROLLBACK`；已初始化路径同样先 `verify_schema_tables` 再 `verify_schema_metadata` | `test_open_failure_paths` 的 `bare.db` 用例（只有 `meta` 表 → false/true 两种 `initialize_empty` 都 CONFLICT，且用户对象数仍为 1、原样保留）+ `test_documents_view_rejected`（documents 是视图 → 都 CONFLICT）+ round-3 新增的缺列/丢唯一约束/NOCASE 夹具 |
| **B4**/P2 | 分页预算只累计 key+value，不含 JSON 包装/转义/页头/`next_after_key`；100 条长中文 audits 会整页 NO_SPACE 而不是缩页 | 新增 `page_documents_budget`（`repository.c:973`）：用 cJSON 序列化探针信封得固定开销，再预留 `KEY_MAX*6+8`（=1160 B）的续页键空间；`collect_page`（`:1016`）改为对每条文档 `cJSON_PrintUnformatted` 取真实 `document_bytes` 累加；装不下下一条时返回**非空页 + 续页键**，只有一条都装不下才 NO_SPACE | `test_pagination_budget`（100 条长中文 audits → 得到非空页与 `next_after_key`，翻页收齐而非 NO_SPACE）；mutation m9b 直接打这个点 |
| **B5**/P2 | cJSON 解码 escaped NUL 后 `strlen` 截断，`"0\u0000junk"` 被当 `revision=0` 且序列化丢后缀 | 新增 `locker_storage_json_text_has_embedded_nul`（`schema.c:35`，声明在 `src/schema_internal.h`）：同时拒原始 0x00 与奇偶正确的 `\u0000` 转义；`locker_storage_schema_validate_json`/`validate_document_json` 入口与 `locker_storage_parse_json`（`repository.c:497`，守卫在 `:502`）都先过该守卫 | `test_embedded_nul_rejected`（NUL 在 U64 开头/中间、属性名、文档 key、`reason`，均 INVALID 且 revision/文档不变；合法 `\\u0000` 字面反斜杠仍通过） |
| **N1**/P2 | handoff 用 `length=0` 的 projection 指针表示「无投影」，实际返回 INVALID | `handoff.md` §3 改为传 `NULL`，并给可编译的完整调用示例，明确 `OK ≠ 可 ACK` | 文档订正，无代码改动 |
| **N2**/P2 | summary/handoff 把正常 close/reopen 说成「进程崩溃后重开」 | 两处改为「正常 close/reopen」；唯一 fork 场景单独说明是竞争测试 | 文档订正 |
| **N3**/P2 | 原子性测试用非法 outbox state，在 `BEGIN` 之前就被拒，不证明半写回滚 | `test_failed_transaction_is_atomic` 改为在库里建 `BEFORE INSERT ON documents WHEN NEW.collection='outbox'` 的 `RAISE(ABORT)` trigger，让「第一条有效写已发生、第二条 SQL 失败」，再断言整单回滚（slot_guards 空、revision 0） | 该用例本身；m8 之外的 `CONFLICT` 路径 |
| **N4**/P3 | `deployment/host/dependencies.lock.json`/`README.md` 仍写「尚未 vendor」，`host/storage/README.md` 缺失 | 前两者由集成负责人更新（`vendored` 段、`consumer_cards`、`unresolved` 四条）；新建 `host/storage/README.md`（125 行 / 7850 B，round-8 后值） | 文档订正 |
| **N5**/P3 | `u64.c` 与 `schema.c` 重复实现 `host/common` 的 numeric 能力；抽取工具未保留、无自动真源比对门禁 | `u64.c` 改为 `locker_common` numeric 的薄适配；`schema.c` 的 U64 校验改走 `locker_u64_parse_decimal`（含溢出）；新增 `host/storage/tools/extract_schema.py` + CTest `storage.schema_source_drift` | `test_u64`、`test_schema`（`18446744073709551616` 仍被拒）、漂移门禁本身 |

### 5.7 round-2 审计整改对照（R2-B1 / R2-B2 / R2-B3 + R2-N1–R2-N4）

第二轮独立复审（`audit-round2.md`，2026-10-24）结论仍为 **FAIL**：首轮 B4 已闭环、B5 入口防护成立，但 B1/B2/B3 各留一个未封边界（R2-B1/B2/B3），另有 4 项非阻塞。**审计结论不被本表修改**，是否闭环由 round-3 复审判断。

| 编号 | 审计指出的缺陷 | 整改落点 | 守它的回归 |
|---|---|---|---|
| **R2-B1**/P1 | 全重复页丢弃新 gap 标志：`new_count == 0` 早退在 `gap_before_page` 锁存之前，先导 `(1,1)(1,2)` 得 `continuity=true`，再原样重发同页但 `gap=true` 会直接 rollback 返回 OK 且 `continuity` 仍 true，下一正常页继承 true | `import_events` 的 `new_count == 0` 分支拆成三路：`gap_before_page == false` 保持原样（rollback + 返回已存游标）；`gap == true` 且 `!stored_exists` → `CONFLICT`（有事件无水位，状态不一致）；`gap == true` 且已存游标 → **不推进 `through`、不应用投影**，但 `build_cursor_record(&stored_through, false)` 覆盖写游标后提交，锁存缺口并返回 `OK` | 新增 `test_duplicate_page_latches_gap`（dup_gap.db：重复页 + `gap=true` 后 `continuity` 落库为 false，后续正常页洗不白）；m14 变异直接打这个分支 |
| **R2-B2**/P1 | 半零游标未经结构校验即充当日志边界：`is_zero` 只识别两者都零，`(2,0)`/`(0,1)` 被当成合法键；非空页把 `first_available` 整体清零会被「非零才校验」跳过 | 新增 `event_key_structure_ok`（`repository.c:1384`，检查点 `:1541-1543`、`:1554`）：`boot_id` 与 `event_seq` 要么都零（哨兵）要么都非零，依据 `docs/architecture/uart-v1.md:38`「禁止仅其中一个为 0」；每条事件键 + `first_available`/`last_committed`/`next_after` 全部过该检查；`first_available` 改为**零即拒绝**（非空页必须给真值） | `test_import_page_boundaries` 扩到 10 个负例：`last_committed` 半零 `(2,0)`、`first_available` 半零 `(0,1)`、`next_after` 半零 `(1,0)`、`first_available` 只清 boot/seq 保留 device/epoch、整体清零，全部 `INVALID` 且事件数/水位/`continuity` 不变；m12/m13 变异 |
| **R2-B3**/P2 | 只有 `documents` **视图**的有痕迹库被当成空目标：`db_table_exists` 只查 `type='table'`、旧的用户表计数只数 table，`CREATE TABLE IF NOT EXISTS` 遇同名视图静默跳过，初始化假成功并写入元数据 | 新增 `db_object_exists`（`SELECT 1 FROM sqlite_master WHERE name = ?1`，不限 type）与 `db_user_object_count`（round-3 B-1 后改为 `WHERE name NOT GLOB 'sqlite_*'`，含 view/index/trigger；`LIKE` 的 `_` 是单字符通配，会误排除 `sqlitex_*` 这类合法用户对象）；`locker_repo_open` 三态判定改为「`documents` 是表 / 是同名非表对象 / 完全没有」：`documents_any_object && !documents_is_table` → `CONFLICT`，`!documents_is_table && user_objects != 0` → `CONFLICT`；初始化分支在 `COMMIT` **之前**补 `verify_schema_tables` | 新增 `test_documents_view_rejected`（夹具 `CREATE VIEW documents AS SELECT ... WHERE 0;`，两种 `initialize_empty` 都 `CONFLICT`，之后用户对象数仍为 1、未写入 meta/migrations）；m15 变异 |
| **R2-N1**/P3 | 回归覆盖不完整：B5 三个负例空洞（`mutations:[]` 先被 `minItems` 拒、key 用例 value 本就非法、属性名截断后仍是未知键）、B3 无 DDL 中途失败注入、B1 跳 boot 用例起始 `continuity` 已为 false | B5 负例全部改成「除 NUL 外完全合法」的输入（合法 PUT mutation、合法 ToolRecord、属性名 `name\u0000junk` 截断后正好是合法 `name`、新增 key 开头与原始 0x00 在属性名两段）；新增 `test_boot_jump_keeps_continuity`（起始 `continuity=true` 再跳 boot） | `test_embedded_nul_rejected`（强化后 m10/m11 均捕获）、`test_boot_jump_keeps_continuity` |
| **R2-N2**/P3 | 交接正文与整改表矛盾：`handoff.md` 写换 boot 要求 `boot+1`（实现是「严格增大、允许跳号」）、`summary.md` 写「boot 跳号 → CONFLICT」；N1 的 `OK ≠ 可 ACK` 未写充分条件 | 两处统一为「换 boot 要求 `boot` 严格增大且 `seq == 1`（允许跳号）」，并补 `test_boot_jump_keeps_continuity` 作实现证据；`handoff.md` §3.5 补 `OK` 与 `ACK_PERSISTED` 的充分条件（`continuity_ok == true` + 水位不倒退不越界，否则只能回 `HISTORY_INCOMPLETE`） | 文档订正 + 上述回归 |
| **R2-N3**/P4 | mutation 证据不能全视为最终快照复验：`m9b` 原始分母是 4/534 不是 4/549；m4 锚点（`u64.c` 前导零回退）在当前薄适配下合法输入根本走不到，不能证明当前版本被覆盖 | 全部变异**重新对最终源码跑一遍**（round-2 时 16 个点位，round-3 增至 18 个），日志开头记录被测源文件指纹；m4 改为打「非法文本回退分支」（`u64.c:51`）并新增 `test_compare_text_noncanonical` 专门覆盖该分支（`"7" vs "07x"`、`"" < "0"`、21 位超长非规范文本） | `test_u64`（90 断言）+ `mutation.log` 的 `SOURCE_FINGERPRINTS` 行 |
| **R2-N4**/P4 | `reserved_next_key` 是 1160 B（`KEY_MAX*6+8 = 192*6+8`）不是 36 B；m8 名为 `init_tx` 实际测的是「有痕迹库」判定；m6 FULL 漏检如实保留 | 文档改正为 1160 B；m8 改名 `m8_trace_guard` 并在日志里写明它覆盖的是「有痕迹但结构不全 → CONFLICT」而非 DDL 事务回滚；m6 仍如实登记为未覆盖 | 文档订正 |

### 5.8 round-3 审计整改对照（B-1 / B-2 + N-1–N-3）

第三轮独立复审（`audit-round3.md`，2026-10-24，codex gpt-6.1-sol/high）结论仍为 **FAIL**：2 项阻塞（B-1/B-2）+ 3 项非阻塞（N-1/N-2/N-3）。该轮审计侧无法执行 Linux 构建（`wsl.exe` 返回 E_ACCESSDENIED、`ssh locker-vm` 255、PATH 无 cmake/gcc），因此 C 端 configure/build/CTest/ASan/TSan/mutation 全部 NOT_RUN，只做了 Windows Python + Node 的静态/SQL 探针；本轮整改后由本卡在两个 Linux 环境重跑全部 C 端证据。**审计结论不被本表修改**，是否闭环由 round-4 复审判断。

| 编号 | 审计指出的缺陷 | 整改落点 | 守它的回归 |
|---|---|---|---|
| **B-1**/P2 | 对象计数把 `LIKE` 的下划线当字面前缀：`name NOT LIKE 'sqlite_%'` 会把用户建的 `sqlitex_audit` 当内部对象排除，只有 `sqlitex_audit` 一张表的库被判为「真正空目标」，首次初始化假成功并补建三表 | `db_user_object_count`（`repository.c:279`）的 SQL 改为 `SELECT count(*) FROM sqlite_master WHERE name NOT GLOB 'sqlite_*'`（`:291`）；`GLOB` 里 `_` 是字面下划线，内部前缀 `sqlite_` 才被排除 | `test_sqlite_prefixed_user_object_rejected`（只建 `sqlitex_audit` 表 / 同名前缀视图两种夹具，`initialize_empty` true/false 都必须 CONFLICT，且事后用户对象数与内容不变）；mutation `m16_sqlite_prefix` 捕获 1/865（`:2198`） |
| **B-2**/P2 | `verify_schema_tables` 只查三张表名，不验证列与唯一约束：`documents(x TEXT)` 或缺 `(collection,key)` 主键的库也能 open 成功，之后 query 报 `no such column: value_json`、PUT 报 `ON CONFLICT clause does not match any PRIMARY KEY or UNIQUE constraint` | `verify_schema_tables`（`repository.c:2036`）升级为「列结构 + 主键位置 + 键唯一索引 + BINARY 排序」：`verify_table_columns`（`:1849`）用 `SELECT name, type, [notnull], pk, hidden FROM pragma_table_xinfo(?1) ORDER BY cid` 逐列核验（round-6 起 `hidden != 0` 直接 CONFLICT，名/类型不敏感相等、NOT NULL 一致、主键位置精确）；`verify_key_index`（`:1973`）用 `pragma_index_list` 找 unique 非 partial 索引（round-5 起 partial 一律 fail-closed），再由 `index_covers_key`（`:1919`）用 `pragma_index_xinfo` 核验列名顺序、`desc == 0`、`coll == 'BINARY'`；首次初始化在 `COMMIT` 前也调用它 | `test_schema_structure_verified`（真库对照 + 18 个坏夹具：缺 `value_json` 列、三列齐全但丢主键、`collection` 用 NOCASE、多一列 `extra`、列级 NOCASE、类型不符、反向排序 + 额外唯一索引、partial UNIQUE、预存 `RAISE(IGNORE)` 触发器、隐藏生成列、内联额外 UNIQUE、内联 NOCASE/DESC 主键、表内 `CHECK`、表内 `FOREIGN KEY`、等长不同文本 DDL；每个夹具 `open(false)` 与 `open(true)` 都必须 CONFLICT 且 `repo == NULL`）；mutation `m17_verify_columns` 与 `m18_verify_key_index` 在 round-7 的 DDL 文本兜底加入后**单点未捕获**，各自与 m33（关掉 DDL 比对）联合后捕获 **32/865** 与 **24/865**（round-6 时单点分别为 20/829 与 12/829） |
| **N-1**/P2 | m15 变异是 SQL 语法错误（`WHERE type = (char)116` 原样进 SQL），最先红的是普通新库打开（期望 OK 得 IO_ERROR），不能证明 R2-B3 的视图路径被守住；即使改成合法 `type='table'`，`db_user_object_count` 仍会数出视图 | 重新设计为 `m15b_view_double`（`:256` + `:2388` + `:2393` 三点联合关掉视图/痕迹防线）并捕获 4/865（round-5 N-1 已改写红行归因：证明的是返回分类与有痕迹保护，不是「首次 open 假成功」）；`m15` 单点结果**如实登记为未捕获/冗余防线**，不作为闭环证据；`mutation.log` 逐条记录 configure/build/ctest 退出码与目标场景失败行 | `mutation.log` 的 m15 / m15b 两段 |
| **N-2**/P2 | NUL 属性名负例仍空洞：`na\u0000me` 截断后是 `na` 不是 `name`，ToolRecord 仍缺 `name` 且 `na` 非允许字段，INVALID 不能单独证明 NUL 防护；key 开头 NUL 截断成空 key 也会先被 `minLength` 拒；m11 后段红行是 `expected=1` 的连带效应 | 属性名负例改为 `name\u0000junk` / `name`+0x00+`junk`（截断后恰好是合法 `name`，唯一拒绝理由只能是 NUL），并加 4a 正对照（干净库 `nul_pos.db` 必须 OK）；key 开头 NUL 用例保留但**不单独宣称**为守卫证据；各类 NUL 独立性靠「除 NUL 外完全合法」的输入 + 正对照 | `test_embedded_nul_rejected`（强化后 m10 捕获 6/865、m11 捕获 17/865） |
| **N-3**/P3 | 文档与证据数字错误：`handoff.md` 把 `first_available`/`last_committed` 一律写成「本页首/末条」（契约语义是日志最早可用位置/已提交尾，可早于/晚于当前页）；`summary.md` 的 B3 行仍只写 `db_user_table_count`；`README.md:21` 写 `active_task=null` 而机器索引是 S1-02；`summary.md` 写 WSL CTest 0.21s 而日志实测为 0.26s（round-5 收尾复跑） | `handoff.md` §3.5 改写为「`first_available` = 日志最早可用位置、`last_committed` = 已提交日志尾、`next_after` = 本页末条」，并新增 `test_import_boundary_envelope`（`first_available < first_key`、`last_key < last_committed`、`has_more=true` 必须 OK 且水位只推进到本页末条）；`summary.md` B3 行重写为对象类型判定 + 提交前 `verify_schema_tables`；`README.md:21` 改为 `active_task=S1-02`；CTest 时间改为实测值 | `test_import_boundary_envelope` + 文档订正 |

### 5.9 round-4 审计整改对照（B-1 / B-2 + N-1–N-3）

第四轮独立复审（`audit-round4.md`，2026-10-24，codex gpt-6.1-sol/high）结论仍为 **FAIL**：2 项阻塞（B-1/B-2）+ 3 项非阻塞（N-1/N-2/N-3）。该轮审计侧同样无法执行 Linux 构建（进程创建失败、`wsl.exe` E_ACCESSDENIED、`ssh locker-vm` 255、PATH 无 cmake/gcc），C 端全部 NOT_RUN，只用 Windows Python 3.14.6 + Node 做 SQL 探针与指纹；本轮整改后由本卡在两个 Linux 环境重跑全部 C 端证据。**审计结论不被本表修改**，是否闭环由 round-5 复审判断。

| 编号 | 审计指出的缺陷 | 整改落点 | 守它的回归 |
|---|---|---|---|
| **B-1**/P2 | 反向排序组合可绕过键唯一性校验：列默认 BINARY、主键显式 `COLLATE NOCASE`、另建一个 BINARY 唯一索引 → 三列名/类型/NOT NULL/pk 位置全匹配、列默认排序检查通过、`index_list` 里 `binary_key` 恰好被 `index_covers_key` 认可，旧 `verify_key_index`「找到任一匹配索引即 OK」放行；实际 UPSERT 被 NOCASE 主键拒（`UNIQUE constraint failed: documents.collection, documents.key`），键集合已变 | `verify_key_index`（`repository.c:1973`）改为「每一个 unique 且非 partial 的索引都必须匹配」：`if (!matches) { status = CONFLICT; break; }`（`:2010`）否则 `++matched_unique`（`:2014`）；循环后 `matched_unique == 0`（`:2023`）也 `CONFLICT`。主键语义不能被额外 BINARY 唯一索引替代，挂在别的列上的额外唯一索引同样改变可接受键集合，一并按不兼容拒绝 | `struct_revcoll.db`、`struct_extrauniq.db` 夹具（`test_schema_structure_verified`）；定向变异 m20b 与 m22 在 round-7 的 DDL 文本兜底加入后**单点未捕获**，各自与 m33 联合后捕获 **16/865**（round-6 时单点为 4/829；二者是同一 `if` 块的两种改法，不当作两个独立缺陷） |
| **B-2**/P2 | 审计期间源码被外部改动，`repository.c`/`test_repository.c` 与 `source-manifest.txt`、VM 清单、mutation 指纹脱节（清单 30/32、mutation 6/8 匹配） | 源码与测试全部落定后重出 `source-manifest.txt`（32 文件，sha256=`817ddb073ba2a35b3b69e83e5fc3d8f72429ad27d3f22e076a967edecfa6dd0a`，3563 B，32/32 OK）；重跑 WSL2 与 VM 全套 C 证据（VM run_id=`20261024-193500`）；变异批次重跑并把 11 个源文件指纹写进 `mutation.log` 头部 | `mutation.log` 头部 `SOURCE_FINGERPRINT`、`source-manifest.txt`、§5.1 |
| **N-1**/P2 | 变异归因不准：m15b 不能据失败行证明「首次 open 假成功」；m17/m18 各 4 行失败分别来自一个夹具；m11 汇总 13 但只保留 12 条明细 | m15_object_type 单点移出批次（残留目录单独重跑 0 失败）；m17/m18 重新按直接重跑二进制登记（round-6 新增生成列/内联约束夹具后 m17 由 12 增至 20/829；round-7 的 DDL 文本兜底加入后二者单点均未捕获，联合 m33 后为 32/865 与 24/865），新增 m20b/m21b/m22 逐点位明细；**m23/m24 与 m17/m18 同锚点，登记为重复点位、不计独立覆盖，round-5 整改时已从批次移除** | `mutation.log` 逐点位段 + §5.4 第 7 条边界 |
| **N-2**/P3 | `handoff.md` 与测试注释把 `last_committed` 写成「RK 已提交的日志尾」（实际是 MCU 持久日志提交边界，RK 本端落库水位是 `committed_cursor`/`through`）；残留清理说明与 `summary.md` 矛盾且把 WSL 构建路径写成 `/var/tmp`；`summary.md` 三处勘误（`na`+NUL+`me`、`struct_bare` 行、m4 锚点） | `handoff.md` §3.5 与 `test_import_boundary_envelope` 注释改写为「MCU 已经持久提交的日志尾，不是 RK 本端落库水位」；§11 残留统一为「已清理并复核 0」并把路径改成 `$HOME/s102_*`；`summary.md` 三处按实际用例/夹具更正 | 文档 + `test_import_boundary_envelope`（`first_available < first_key < last_key < last_committed` 的成功对照） |
| **N-3**/P3 | 私有 regex 的 `{0,n}` 上界多生成一个可选 atom（`regex.c:530–535`/`:559–568`），`^a{0,1}$` 对 `"aa"` 会接受；当前仓储 U64 另有 `schema.c:492–493`/common 数值解析保护，未据此认定 u64 防护失效 | `regex.c:562` 先按 `min_value` 播种 `produced` 再循环（`if (min_value > 0u) { produced = min_value; }`，循环 `for (index = produced; ...)` 从 `produced` 起），并新增 `test_bounded_repeat` | `test_regex.c` 的 `test_bounded_repeat`；定向变异 m25（零下界 `produced` 1u→0u，2/46）与 m27（正下界播种，1/46） |

### 5.10 round-5 审计整改对照（B-1 + N-1 / N-2）

第五轮独立复审（`audit-round5.md`，2026-10-24，codex gpt-6.1-sol/high）结论仍为 **FAIL**：1 项阻塞（B-1/P2）+ 2 项非阻塞（N-1/P2、N-2/P3）。该轮审计侧同样无法执行 Linux 构建（`wsl.exe` E_ACCESSDENIED、`ssh locker-vm` 255、PATH 无 cmake/gcc），C 端全部 NOT_RUN，只用 Windows Python 3.14.6 + SQLite + Node 做 SQL 探针与指纹核对。本轮整改后由本卡在两个 Linux 环境重跑全部 C 端证据（§5.1）。**审计结论不被本表修改**，是否闭环由 round-6 复审判断。

| 编号 | 审计指出的缺陷 | 整改落点 | 守它的回归 |
|---|---|---|---|
| **B-1**/P2 | `verify_key_index` 跳过所有 partial UNIQUE：只要在合法 DDL 上执行 `CREATE UNIQUE INDEX partial_extra ON documents(collection) WHERE collection='audits'`，该索引 `unique=1 partial=1`，旧代码在非 unique 跳过之后没有 partial 分支，直接进 `index_covers_key` 匹配，主键又让 `matched_unique=1`，于是结构检查通过、`quick_check` 也通过；但此后第二条合法 `AuditRecord` 的 UPSERT 会报 `UNIQUE constraint failed: documents.collection`——损坏/不兼容库被当成可用库 | `repository.c:2000` 新增 fail-closed：`if (is_partial != 0) { status = LOCKER_STATUS_CONFLICT; break; }`。理由写进源码注释（`:1996–1999`）：partial unique 不是全局唯一约束、不能代替主键，但它确实会拒绝一部分本来合法的写入，未知部分约束一律按不兼容处理。非唯一索引仍按原样放行 | 新夹具 `SQL_PARTIAL_UNIQUE_INDEX`（`test_repository.c:2318`：合法三列 + 合法主键 + `CREATE UNIQUE INDEX partial_collection ON documents(collection) WHERE collection='audits'`）；`test_schema_structure_verified` 对 `struct_partial.db` 断言两种 `initialize_empty` 都 CONFLICT（`:2460 :2461`）、且事后对象数仍为 4（`:2466`，未被补建/清空）；真 DDL 正对照 `struct_ok.db` 仍 OK（`:2414 :2419`）；定向变异 **m26**（`repository.c:2000` 的 `CONFLICT; break;` → `continue;`）单点**未捕获**（round-6 的对象清单与 round-7 的 DDL 文本比对两道兜底先拒同一夹具），两点联合 **m26b**（+m28）捕获 4/865，三点联合（+m33）捕获 **20/865** |
| **N-1**/P2 | ① m15b 的红行不能证明「首次 `open(true)` 视图假成功」；② m17/m18 的失败行归因到错夹具、summary 写成「缺列/多列/NOCASE」；③ 25 次运行 23 红，去重后应区分「运行数」与「独立点位数」；④ m25 旧描述（循环回退到 index=1）与实际变异不符；⑤ m11 的「跨用例 revision 连带」解释已过期 | ① §5.4 第 5 条重写：m15b 证明的是返回分类与有痕迹保护，首次视图路径由初始化前的 `verify_schema_tables`（`repository.c:2153`）静态拒绝；② §5.4 表格把 m17 的 12 条精确拆到 `struct_extra`/`struct_colcoll`/`struct_wrongtype`，m18 的 12 条拆到 `struct_revcoll`/`struct_extrauniq`/`struct_partial`；③ §5.4 汇总加「25 次运行 / 23 次变红 / 按独立缺陷 24 点位 22 捕获」三档口径；④ 重写 m25 为「零下界 `produced` 1u→0u」，另加 m27 覆盖正下界播种；⑤ §5.4 第 6 条改为「每个 NUL 负例独立建库 + 先建 revision=1 基线」，删掉过期解释 | `mutation.log` 逐点位段（重写为 410 行、含新 `SOURCE_FINGERPRINT` 与程序化解析的 FAIL 行）+ §5.4 八点边界 |
| **N-2**/P3 | ① `summary.md:324–329` 与 `handoff.md:583` 的表格行被正则 `^a{0,1}` 后直接拼入文档标题，表格中断；② 合计断言数写 1583，实际两个 `unit-tests.log` 11 行求和为 1601（505 + 1096），本卡按变异二进制直接重跑实测为 **1609**；③ `host/storage/README.md:68–71` 仍用 `HOME/locker-build/s1-02`，summary 说 `$HOME/s102_ev` 符合工作区纪律，而实际 canonical 是 `locker-workspace/{build,logs,artifacts}/<卡号>/<批次>`；④ summary 把 `:2469–2485` 的 `bare.db` 叫 `struct_bare` | ① §5.9 的损坏行整段重写为完整 N-3 行（本表即修正后版本）；② 全文合计改为实测值：round-5 时 **1609**（common 505 + storage 1104），round-6 补回归后 **1681**（common 505 + storage 1176，其中 repository 829）；③ README 构建路径改为 `$HOME/locker-workspace/build/S1-02/manual`，summary §5.1 说明 WSL2 用 `$HOME/s102_r6ev2` 且只要求构建目录在源码树外、VM canonical 才是 `locker-workspace` 分类；④ m8 的失败行改为 `bare.db` 与 `struct_bare.db` 分开写（round-6 后为 `:2730 :2734 :2735 :2746` 与 `:2519 :2520`） | 文档订正 + `source-manifest.txt` 32/32 重算 + `mutation.log` 重写 |

### 5.11 round-6 审计整改对照（B-1 / B-2 + N-1 / N-2）

第六轮独立复审（`audit-round6.md`，2026-10-24，codex gpt-6.1-sol/high，43048 B）结论仍为 **FAIL**：2 项阻塞（B-1/P1 未知触发器、B-2/P2 隐藏生成列）+ 2 项非阻塞（N-1/P2 归因、N-2/P3 文档）。该轮审计侧同样无法执行 Linux 构建（`wsl.exe` E_ACCESSDENIED、`ssh locker-vm` 255），C 端全部 NOT_RUN，只用 Windows Python 3.14.6 + SQLite 3.53.2 做内存库反例探针；但它明确声明 FAIL 依据是**新增反例**而非环境受限。本轮整改后由本卡在两个 Linux 环境重跑全部 C 端证据（§5.1）。**审计结论不被本表修改**，是否闭环由 round-7 复审判断。

| 编号 | 审计指出的缺陷 | 整改落点 | 守它的回归 |
|---|---|---|---|
| **B-1**/P1 | 未知持久触发器被接纳：官方 DDL 建库 + 合法元数据后执行 `CREATE TRIGGER skip_second BEFORE INSERT ON documents WHEN NEW.key='2111…' BEGIN SELECT RAISE(IGNORE); END;`，重新 `open` 通过全部结构检查；此后命中 `WHEN` 的 PUT 被静默丢弃（`sqlite3_changes()==0` 且无 SQL 错误），`apply_change` 仍推进 `snapshot_revision` 并 `COMMIT`，端口报成功但内容缺失 | ① 新增 `verify_official_objects`（`repository.c:2086`）：`SELECT type, name FROM sqlite_master WHERE name NOT GLOB 'sqlite_*'`，要求每行 `type=='table'`、表名 ∈ `{meta,migrations,documents}`、不重复、总数恰好 3，否则 CONFLICT；由 `verify_schema_tables` 末尾调用（`:2071`），首次初始化 `COMMIT` 前（`:2409`）与已初始化分支（`:2421`）都覆盖。② `db_put_document` 在 `step_write` 成功后加 `if (sqlite3_changes(db) != 1) → IO_ERROR`（`:438`）；`db_delete_document` 先记 `existed`，仅当 `existed && sqlite3_changes(db) != 1` 才 IO_ERROR（`:477`，删除不存在的 key 仍是合法空操作） | 新夹具 `SQL_TRIGGER_IGNORE`（`test_repository.c:2328`）→ `test_schema_structure_verified` 对 `struct_trigger.db` 双 `initialize_empty` 都 CONFLICT（`:2475 :2476`）且事后对象数仍为 4（`:2481`）；新用例 `test_silently_ignored_write_detected`（`:2604`）用 `ignored.db` / `ignored_delete.db` 验证命中触发器的 PUT/DELETE 报 IO_ERROR、revision 不推进、`documents` 仍为 0（DELETE 侧 revision 仍 1、文档仍 1），`DROP TRIGGER` 后同一条操作成功；定向变异 m28（`:2071` 调用 → OK）**4/865**、m31（`:438` 守卫 → if(0)）**4/865**、m32（`:477` → `existed && 0`）**3/865** |
| **B-2**/P2 | `pragma_table_info` 隐藏生成列，额外约束绕过精确列数核验：`ALTER TABLE documents ADD COLUMN extra TEXT GENERATED ALWAYS AS (CASE WHEN collection='audits' THEN NULL ELSE key END) VIRTUAL NOT NULL;` 之后 `table_info` 仍恰好报官方 3 列、主键索引/metadata/`quick_check` 全过，只有 `table_xinfo` 才多出 `extra TEXT notnull=1 hidden=2`；之后第一条合法 `AuditRecord` 的 UPSERT 报 `NOT NULL constraint failed: documents.extra` | `verify_table_columns`（`repository.c:1849`）的 SQL 改为 `SELECT name, type, [notnull], pk, hidden FROM pragma_table_xinfo(?1) ORDER BY cid`（`:1856`），读取 `hidden`（`:1876`）并在列名/类型比较之前 `hidden != 0 → CONFLICT`（`:1881`）；三表都走同一路径，隐藏/生成列一律不得出现 | 两个生成列夹具：`SQL_GENERATED_COLUMN`（`:2337`，`value_json` 声明成 VIRTUAL 生成列，`table_info` 只报 2 列）与 `SQL_GENERATED_APPENDED`（`:2346`，审计原始反例，末尾追加生成列，`table_info` 仍报 3 列）；`test_schema_structure_verified` 对 `struct_generated.db`（`:2489`）与 `struct_generated_appended.db`（`:2503`）双模式都 CONFLICT，且对象数仍为 3（`:2498`、`:2513`）；定向变异 m29（`:1856` 改回 `table_info` 且 hidden 恒 0）单点**未捕获**、m30（`:1881` hidden 判定 → if(0)）单点**未捕获**（均被 round-7 的 DDL 文本比对兜底），各自联合 m33 后 **16/865**（m29+ddl 命中 `:2504 :2506`、m30+ddl 命中 `:2490 :2491`） |
| **N-1**/P2 | mutation 正文归因仍冲突：① `mutation.log` 仍称 m15b 三点联合「恢复旧不安全行为」，与 summary 口径矛盾；② 仍用「前一用例推进 revision」解释 m10/m11；③ 25 次运行减 m20b/m22 重合称「24 个独立缺陷、22 捕获」缺材料支撑；④ 完整原始输出与精确变异定义在收尾时被清理，无法重放 | ① m15b 归因统一为「证明返回分类与有痕迹保护，首次视图路径由初始化前的 `verify_schema_tables`（`:2409`）静态拒绝」，与 §5.4 第 5 条一致；② m10/m11 归因统一为「每个 NUL 负例独立建库 + 先建 revision=1 基线，红行是用例内后置检查」；③ 删除「独立缺陷数」折算，只保留「运行数 / 变红数 / 未捕获数」三档，m20b 与 m22 的同一 if 块、m17/m18 的共享 `:2250` 逐项注明；④ 正式变异定义（`s102_r8mutI.py` 的 POINTS 表）与完整诊断输出（`s102_r8mutI.log`，562 行）保留在 mutation.log 的逐点位段与 §5.4 表中，不再只留摘要 | `mutation.log`（26594 B / 663 行，42 个具名段 + 11 行 `SOURCE_FINGERPRINT` + 10 条未捕获归因）+ §5.4 的 12 条边界 |
| **N-2**/P3 | ① `handoff.md:544` 把 `~/locker-workspace/S1-02/{src,build,logs,artifacts}` 写成保留目录，与同文 `:386–388` 及 VM 实际布局 `build/S1-02/20261024-152903` 矛盾；② `handoff.md:593` 称 `WHERE collection='audits'` 的唯一索引会拒掉 outbox/slot_guards 等集合，实际该谓词只约束 audits 子集；③ summary 的 §5.1 与 §8 对 VM 源码目录写法不一致 | ① summary 与 handoff 统一为四条实际路径（源码 `S1-02/src`、`build/S1-02/<run>`、`logs/S1-02/<run>`、`artifacts/S1-02`），run 号统一为 `20261024-193500`；② partial UNIQUE 影响描述改为「两条 `AuditRecord` 冲突（collection 同为 audits）」，与 §5.10 及测试注释一致；③ VM 源码目录统一写 `~/locker-workspace/S1-02/src` | 文档订正 + VM `source-verify.log`（SHACHECK_OK=32、SRC_FILES=1016、SRC_STRAY_ARTIFACTS=0） |


### 5.12 round-7 审计整改对照（B-1 + N-1）

第七轮独立复审（`audit-round7.md`，2026-10-24，codex gpt-6.1-sol/high，43331 B）结论仍为 **FAIL**：1 项阻塞（B-1/P2 表内 CHECK / FOREIGN KEY 绕过 schema 接纳）+ 1 项非阻塞（N-1/P2 mutation 归因/顺序/计数）。该轮审计侧同样无法执行 Linux 构建（`wsl.exe` E_ACCESSDENIED、`ssh locker-vm` 255、PATH 无 cmake/gcc），C 端全部 NOT_RUN，只用 Windows Python 3.14.6 + SQLite 3.53.2 做内存库反例探针；但它明确声明 FAIL 依据是**新增反例**而非环境受限。本轮整改后由本卡在两个 Linux 环境重跑全部 C 端证据（§5.1）。**审计结论不被本表修改**，是否闭环由 round-8 复审判断。

| 编号 | 审计指出的缺陷 | 整改落点 | 守它的回归 |
|---|---|---|---|
| **B-1**/P2 | 表内 CHECK / FOREIGN KEY 绕过 schema 接纳：保留官方 `meta`/`migrations` 与元数据、只把 `documents` 重建为官方三列 + 官方主键 + `CHECK(key <> '2111…')`（或同构的 `FOREIGN KEY(collection) REFERENCES meta(value)`），此时 `table_xinfo` 三列、列排序规则、`index_list`/`index_xinfo`、用户对象数（仍 3 张表）、元数据与 `quick_check` 与官方 DDL **完全相同**，于是两种 `initialize_empty` 都被接纳；之后第一条命中 `CHECK`/`FK` 的合法 PUT 才报 `CHECK constraint failed` / `foreign key mismatch`，被当作可用仓储打开后才失败 | 新增 `verify_official_ddl`（`repository.c:2244`）：对三张表分别 `SELECT sql FROM sqlite_master WHERE type = 'table' AND name = ?1`（SQL 在 `:2254`），与从唯一 `schema-v1.sql`（嵌入符号 `locker_storage_schema_sql`）提取的期望 `CREATE TABLE` 文本**逐字节比对**；`stored == NULL`、长度不等、`memcmp` 不等、无行/多行都返回 CONFLICT（判定在 `:2280–:2287`）。期望文本由 `expected_table_ddl`（`:2157`）扫描 `CREATE TABLE IF NOT EXISTS` 到 `depth==0` 的 `;` 生成，跟踪 `' " \` [ ]` 引号与双写转义。调用点在 `verify_schema_tables` 末尾（`:2077`），即列检查（`:2061`）→ 键索引（`:2065`）→ 对象清单（`:2071`）→ DDL 文本（`:2077`），首次初始化 `COMMIT` 前与已初始化分支都覆盖 | 三个新夹具：`SQL_CHECK_CONSTRAINT`（`test_repository.c:2381`）、`SQL_FOREIGN_KEY`（`:2390`）、`SQL_DDL_SAME_LEN_DIFFERENT_TEXT`（`:2400`，全小写等价定义，`sqlite_master` 存 164 B 与官方 documents **等长但内容不同**，只有逐字节比较能拒）；`test_schema_structure_verified` 对 `struct_check.db`（`:2542 :2543`）、`struct_foreign_key.db`（`:2554 :2555`）、`struct_same_len.db`（`:2567 :2568`）双 `initialize_empty` 都 CONFLICT 且对象数仍为 3（`:2550 :2561 :2575`）；真 DDL 正对照新增 `test_official_ddl_positive_control`（`:2725`）——用官方 DDL 建库后提交同一份含两条 `audits` 的 change，期望 OK 且 `documents_count == 2`，证明比对没有过度收紧；round-8 N-3 后该正对照改为提交**坏库会失败的那一份输入**（`AuditRecord` 的 `key` 等于 `value.id`，第二条 `key` 正是 `UUID_KEY`，新增 `build_audit_record_with_id` 与两条 `strstr` 断言保证这一点）；定向变异 m33（`:2077` 调用 → OK）**12/865**、m34（`:2281` 只比长度）**4/865**、m34b（整个长度或内容条件 → 只判 NULL）**12/865** |
| **N-1**/P2 | ① 保留日志里 m29 与 m29b 的 edit 逐字节相同，summary 却说二者差别是「只回退列视图、保留 hidden 读取」，现有 edit 无法支持；② 对象清单被说成「在键索引检查之前先拒夹具」，实际 `verify_official_objects` 在键索引**之后**才执行；③ round-6 整改表把 `assertion_failures` 汇总行也算进断言数（m28/m31 写 5、m32 写 4、m29/m29b/m30 写 5）；④ DELETE 失败后的状态被统称「documents 仍为 0」，而正式 DELETE 用例要求 revision=1、文档仍为 1；⑤ m26b 的 8 条失败里有 4 条来自同时关闭 m28 后的 trigger 夹具，不能全算 partial 单独贡献 | ① 从批次删除 `m29b`（`pragma_table_info` 没有 hidden 列，「只回退列视图、保留 hidden 读取」无法用不同 SQL 表达，它本就是 m29 的重复执行），mutation.log 与 §5.4 都注明；② 全部改成真实顺序「列 `:2061` → 键索引 `:2065` → 对象清单 `:2071` → DDL 文本 `:2077`」，并明确否定旧说法；③ 全表失败数统一为「`assertion_failures` 减 1」，§5.4 边界第 11 条写明这条口径；④ DELETE 侧改写为「revision 仍 1、文档仍 1，只有 PUT 失败时文档为 0」；⑤ m26b 与三点联合的 FAIL 行按夹具逐条列出（m26b 4 条 = `:2250`×2 + `:2475 :2476` trigger 夹具；三点联合 20 条含 `:2460 :2461` partial 夹具） | `mutation.log`（42 个 section、`# points=42 named=34 joint=8 caught=32 not_caught=10`）+ §5.4 的 42 行表与 12 条边界；`test_repository.c` 的 ⑬–⑮ 夹具注释改为「对象清单在键索引**之后**仍会拒掉额外索引夹具，所以单点变异全绿，需要联合点位才可观测」 |

### 5.13 round-8 审计结论与 N-1～N-4 整改（PASS）

第八轮独立复审（`audit-round8.md`，2026-10-24，codex gpt-6.1-sol/high）结论 **PASS（限定为本轮代码与证据复审）**：未发现尚未修复的阻塞项 B-x，仅 4 项非阻塞 N-1～N-4。该轮审计侧同样无法执行 Linux 构建（`wsl.exe` E_ACCESSDENIED、`ssh locker-vm` 255、PATH 无 cmake），C 端全部 NOT_RUN，只用 Windows Python 3.14.6 + SQLite 3.53.2 内存探针与字节指纹核验；它明确声明「Linux PASS 只按现存交付日志的历史身份引用，未升级为本轮实跑」。

| 编号 | 审计指出的问题 | 整改落点 | 复核方式 |
|---|---|---|---|
| **N-1**/P2 | mutation 正文把 8 个联合点当作独立缺陷数、m26b 的 4 条红行被算作 partial 证据、`assertion_failures` 与真实断言数未说明差 1 | `summary.md` §5.4 开头改为「从同一次 `ctest --output-on-failure` 的 stdout 抽取 FAIL 行（`mutation-batch.py:162-177`，无第二次执行）」；m26b 行明确「4 条全部来自 ⑪ trigger 夹具、不含 `:2460 :2461`」；边界第 9 条给出完整分解（m26b 4 = trigger；m26+DDL-off 目标行 0；三点联合 20 = 4 partial + 4 trigger + 12 DDL）；第 11 条说明 `assertion_failures` 含尾部汇总行 | `mutation.log` 头部 joint 段重写为 `32 = 16 + 6 + 10` 形式；`summary.md` §5.4 八个联合点行尾逐行标注「N helper + 6 条 m33 DDL + M 条目标夹具」；`handoff.md` §7.3 同步 |
| **N-2**/P2 | 两份 `unit-tests.log` 逐项和为 1715（common 505 + storage 1210），文档写 1707（505 + 1202）属合计算术错误 | `summary.md` §5.1 断言表与 `handoff.md` 四处（`:406`、`:509`、`:519`、`:611`）改为 1717（N-3 又加 2 条断言，storage 1212、repository 865） | 逐项实测：common 97+5+53+164+133+53=505；storage 80+46+865+131+90=1212；合计 1717 |
| **N-3**/P2 | `test_official_ddl_positive_control` 的 key 前缀与 `CHECK(key <> UUID_KEY)` 谓词不命中，正对照未证明「官方 DDL 下该 CHECK 会拦」，保真度不足 | `test_repository.c` 新增 `build_audit_record_with_id`；正对照改用 `k_first_id = "11111111-2222-4333-8444-555555555555"` 与 `UUID_KEY`（= `99999999-…`，即 CHECK 谓词命中值），并加两条 `TEST_CHECK(strstr(...) != NULL)` 保证 key == value.id | 断言总数 1707 → **1717**；`test_repository.c` 2965 → 2983 行 / 137410 → 138538 B；`test_official_ddl_positive_control` 在 `:2725` |
| **N-4**/P3 | 文档把嵌入生成说成「配置期」，实际是构建期 `add_custom_command`；`host/storage/README.md` 的 VM 源码入口写成顶层 `$HOME/locker-workspace/src`，那里不含 `host/storage` | 六处源码/文档注释统一改为「构建期（`add_custom_command`）」：`schema.h`、`embedded.h`（两处）、`schema_embedded.c`、`schema-v1.sql`、`host/storage/README.md`（三处）、`host/storage/CMakeLists.txt` 头部；README 的 VM canonical 段改为卡源码 `$HOME/locker-workspace/S1-02/src` + `build/S1-02/<run>` + `logs/S1-02/<run>` + `artifacts/S1-02` | 改后指纹：`host/storage/CMakeLists.txt` 3832 B（`3d184f052d2b`）、README 7850 B（`45133f6c1483`）、`embedded.h` 922 B、`schema.h` 2197 B、`schema-v1.sql` 2350 B、`schema_embedded.c` 809 B |

round-8 之后按用户超轮规则（审计超 3 轮由开发 AI 与审计 AI 共同修复并共同确认）重新取证：`source-manifest.txt` 重出（32 条 / 3563 B / 自身 sha256=`817ddb073ba2a35b3b69e83e5fc3d8f72429ad27d3f22e076a967edecfa6dd0a`，`generated=2026-10-24T20:59:57+08:00`），WSL2 与 VM 双环境全套重跑（VM run_id=`20261024-213500`，快照 sha256=`f4279ba59ad7b013bdc9811a8e3f6eafc093e214ea3ea92a79ad92784b47c69f` / 81796615 B / 1113 条目），42 点位变异重跑（32 变红 / 10 未捕获），m26c 三点联合重跑（20/865），`mutation.log` 重写为 26243 B。

### 5.14 round-9 审计结论与 N-1/N-2 整改（超轮联合确认轮，PASS）

第九轮是用户 m12834 超轮规则生效后的**联合确认轮**：审计 AI 在提示词里被要求逐项判定 round-8 的 N-1～N-4 是否已关闭，未关闭必须给最小复现，且不得置 DONE。结论 **PASS（限定为当前源码、正式回归与交付证据的独立复审）：未发现尚未修复的阻塞项 B-x；保留 2 项非阻塞收尾问题 N-1/P3、N-2/P3。** 该轮同样声明 configure/build/CTest、C 测试二进制、ASan+UBSan、TSan、C mutation 复跑**全部 NOT_RUN**（审计机 `git status` exit 128、`cmake` ENOENT、`wsl.exe` E_ACCESSDENIED 4294967295、`ssh locker-vm` 255），实际执行的是 Windows Python 四项设计检查（全 exit 0）、`extract_schema.py --check`（exit 0，27525 B）、抽取器内存负控（exit 1）、Node 逐段核算日志、Python AST 解析变异定义（42 名称 / 53 次文本编辑锚点全部 count=1）与内存 SQLite 探针；它明确写明「已有 Linux PASS 按保留运行记录复核，不能改称本审计实际运行 PASS」。

| 编号 | 审计指出的问题 | 整改落点 | 复核方式 |
|---|---|---|---|
| **N-1**/P3 | 主要证据正确，但交接与部分说明残留旧口径（6 处具体矛盾） | ①`handoff.md` §7.3 做法句改为「FAIL 行从同一次 `ctest --output-on-failure` 的 stdout 抽取（`mutation-batch.py:162-177`），没有第二次单独执行测试二进制」；②§7.3 与 §9 `mutation.log` 行的 `test_repository.c` 指纹由旧的 `6ab9a68d1c99`（137410 B 的 round-7 输入）改为 `f1cc609cf4cf` / 138538 B；③§9 `mutation-batch.py` 行的保留常量改为实际值 `s102_r9mut` / `s102_r9build` / `s102_r9mut.log`；④§12 第五轮 N-1 第⑤点改为「m26b 的 4 条失败全部来自 ⑪ trigger 夹具（`:2475`/`:2476`，另两条是 `:2250` helper 行），不含 `:2460`/`:2461`，不能当 partial 覆盖证据；partial 可观测性由三点联合 m26+m28+m33（20/865 = 4 partial + 4 trigger + 12 m33 DDL）证明」；⑤§5.4 m34b 行改为「该 edit 把长度比较与内容比较一起去掉，只剩 `stored == NULL`，是比 m34 更强的形式，不能描述为保留长度分支」；⑥§8 构建路径改为当前主体 `$HOME/s102_r9ev2` 与 VM run `20261024-213500`，并明确 `s102_r7ev` / run `193500` 属 round-7 历史批次 | 六处均按审计给出的「应依据的当前事实」逐字订正；不重跑 C 测试（文字勘误不改生产逻辑） |
| **N-2**/P3 | 外部暂存清零声明与现存条目不符：称 Public/TEMP 已删并复核 0，但只读枚举仍见 `C:/Users/Public` 34 个 `s102_` 条目、系统 TEMP 6 个相关条目 | `handoff.md` §11 整段重写为「11.1 转存为正式证据后删除的暂存物（本轮实测）」+「11.2 有意保留项」，给出每组清理前的实测条目数/字节数与去向：Public 37 条 82,152,050 B → 0；TEMP 10 条（`s102dc`/`s102vm`/`s102vmev`/`s102_r3_*`/`s102_r4_*`/`vm125905`/`vm135713`/`vm135713_run.out`）→ 0；WSL `$HOME` 8 → 0；VM `/tmp` 6 → 0；仓库根 `.tmp_*`/`s102_*`/`_mut*` = 0。删除前逐目录比对：`s102_vm_ev_r9` 16/16、`s102_dc2` 5/5 与证据逐字节相同，`s102_r9mut.py/.log` 与 `mutation-batch.py/.log` 相同，`s102_r9_m26c.*` 与 `mutation-joint-m26c.*` 相同；`s102_r9ev2` 17/18 相同（`design-checks.log` 保留时间戳更新的证据版）；`vm125905`/`vm135713` 与当前 `vm-s102-*` 逐字节不同，属更早批次暂存 | `summary.md` §8 同步删掉 `PUBLIC_LEFT=0` 之类全局断言，改为有批次/时间的事实；10 个审计提示词/运行脚本/原始输出与 6 个取证脚本、2 个解析 JSON 已转存为正式证据（证据目录 56 → 72 个文件） |

审计方另确认：round-8 的 N-1～N-4 均已实际关闭（mutation 主要归因、断言合计、正对照输入、模块构建/VM 入口）；历轮旧阻塞的指定路径已被当前防线与正式回归覆盖，没有用旧版本行号或旧分母认定当前仍 FAIL；`state` 仍为 S1-01 DONE、S1-02 IN_PROGRESS/report=null、active_task/recommended_next=S1-02、阶段 1 `gate_result=NOT_RUN`、hardware profile UNVERIFIED。审计方同时声明：它观察到 `summary.md` / `handoff.md` 在审计期间被外部修改（本卡开发方写入），已采用末次重读，不把这些已改项计作未解决发现。


## 6. 自审与已知缺口

九轮独立审计（`audit.md` 首轮、`audit-round2.md` 复审、`audit-round3.md` 三审、`audit-round4.md` 四审、`audit-round5.md` 五审、`audit-round6.md` 六审、`audit-round7.md` 七审、`audit-round8.md` 八审、`audit-round9.md` 九审（超轮联合确认轮），均为独立审计 AI 产出，2026-10-24）：前七轮结论都是 **FAIL**（首轮 5 项阻塞 B1–B5 + 5 项非阻塞 N1–N5；复审 3 项阻塞 R2-B1/R2-B2/R2-B3 + 4 项非阻塞 R2-N1–R2-N4；三审 2 项阻塞 B-1/B-2 + 3 项非阻塞 N-1–N-3；四审 2 项阻塞 B-1/B-2 + 3 项非阻塞 N-1–N-3；五审 1 项阻塞 B-1 + 2 项非阻塞 N-1/N-2；六审 2 项阻塞 B-1（未知触发器被接纳）/B-2（隐藏生成列绕过列核验）+ 2 项非阻塞 N-1（mutation 归因）/N-2（VM 路径与 partial 影响描述）；七审 1 项阻塞 B-1（表内 CHECK / FOREIGN KEY 绕过 schema 接纳）+ 1 项非阻塞 N-1（mutation 归因/顺序/计数）），第八轮与第九轮结论均为 **PASS**（第八轮无阻塞项、4 项非阻塞 N-1～N-4；第九轮无阻塞项、2 项非阻塞 N-1/P3 与 N-2/P3）。九轮均已逐条整改并补正式回归（§5.6 / §5.7 / §5.8 / §5.9 / §5.10 / §5.11 / §5.12 / §5.13 / §5.14），下面保留的是**整改后仍然存在**的已知缺口，不是审计结论本身。

1. **`synchronous=FULL` 无运行时断言**（m6 变异未被发现）。它是连接级 pragma，SQLite 不提供从另一连接读取它的接口，`PRAGMA synchronous` 在别的连接上读到的是那个连接的设置。当前只在 `repository.c:2366` 的代码路径（`PRAGMA synchronous = FULL`）与文档里保证。若要补强，需要给仓储加一个仅测试可见的观测点——那会引入测试后门，本卡选择不加，并如实登记。
2. **`locker_repo_open` 的 `PRAGMA journal_mode = WAL` 返回值未逐字比对**：`exec_sql` 只看 SQLite 返回码，未断言返回行是 `"wal"`。WAL 是否真生效由 `test_wal_enabled` 从另一连接验证（覆盖了这一风险），但代码本身没做返回值断言。
3. **cJSON 有进程级全局状态**（`global_error`），不可多线程并发解析。本卡的线程模型是「仓储只允许 Core 单线程使用」（`docs/architecture/module-ownership.md:24`），因此不构成缺陷；但这也意味着**不得**把 `locker_repo_*` 端口改成多线程并发调用。竞争场景的测试因此改用**两个 fork 进程**（各自一个 cJSON 上下文、同一库文件），而不是两个 pthread——这才是真实的多进程/多实例竞争形态。
4. **`locker_repo_query` 的页大小上限是 65535 字节**（`LOCKER_STORAGE_PAGE_BYTES_MAX`）。单条文档就超过该上限时返回 `NO_SPACE` 而不是静默截断——这是有意的 fail closed，但意味着超大 `vision_evidence` 记录（目前 73 字节 key、值较小）不会触发；若将来单条记录变大到超过 64 KiB，需要契约负责人重新定上限。
5. **`recovery` 页不接受单键查询**（`run_query` 里 `recovery_page && key_length != 0 → INVALID`），依据 x-rules 10「恢复按 collection 分页，不一次装载无限历史」。
6. **`src/schema_records.json` 是契约的机器抽取子集**，不是手写副本。抽取器已冻结为 `host/storage/tools/extract_schema.py`（审计 N5 要求），并由 CTest `storage.schema_source_drift` 在每次测试时自动比对；若契约变化，`extract_schema.py`（不带 `--check`）重生成即可，漏做会让测试变红而不是静默放行。
7. **`deployment/host/dependencies.lock.json` 与 `README.md` 已按审计 N4 更新**（本轮由集成负责人授权范围内修改）：新增 `vendored` 段记录两棵 vendor 树的归档 SHA-256/逐文件 SHA-256/编译定义，`cjson.consumer_cards` 改为 `["S1-02","S1-05"]`，`unresolved` 四条改写为「SQLite 与 cJSON 已 vendor；CivetWeb/libsodium/TLS 仍由 S1-05 负责」。
8. **嵌入 NUL 的回归曾出现假阳性**（整改中发现并修正）：旧用例把 raw-NUL 塞进 `mutations` 为空数组的 change，会先在 `RepositoryChange` 的 `minItems 1` 被拒，根本没走到 NUL 守卫。现用例改成带合法 mutations 的完整 change（`expected_snapshot_revision` 与 `audits.reason` 各一段），m10/m11 变异才被捕获。这条记录保留下来是因为它说明「断言通过」不等于「覆盖了目标逻辑」。

## 7. behavior

### 7.1 六个端口的行为契约

| 端口 | 前置条件 | 成功语义 | 失败语义 |
|---|---|---|---|
| `locker_repo_open` | `config->schema_version == 1`、`path` 非空 | `*out` 为有效仓储；已按 `initialize_empty` 决定是否建库；已校验 schema 元数据与 `quick_check` | `*out` 保持 NULL；`NOT_READY`（库不存在且未要求初始化）/ `CONFLICT`（schema 版本或迁移元数据不符，**不删库**）/ `IO_ERROR`（打不开或 quick_check 失败）/ `INVALID`（参数非法）/ `NO_SPACE`（内存不足） |
| `locker_repo_query` | `schema_version == 1`、collection 合法、key 与 after_key 互斥（单键分支 `after_key` 必须为空、分页分支 `key` 必须为空） | `result->json` 为 `RepositoryQueryResult`（分页带 `next_after_key`）、`snapshot_revision` 为读事务里的值 | `INVALID`（参数/互斥违反）/ `NO_SPACE`（结果超 `json[65536]`，或单条记录超页上限）/ `IO_ERROR`（SQL 或结果自校验失败） |
| `locker_repo_load_recovery` | 同 query，且**不允许单键查询** | `result->json` 为 `RepositoryRecoveryPage`（多一个 `collection` 字段） | 同上 |
| `locker_repo_commit_change` | `change->length` 在 `(0, 65536]` | 整个 `RepositoryChange` 在**一个 `BEGIN IMMEDIATE` 事务**里生效：checks → mutations → `snapshot_revision = new` | `INVALID`（JSON/schema 非法、`new != expected+1`）/ `CONFLICT`（`expected` 与库里 revision 不符，或 checks 不满足）/ `BUSY` / `NO_SPACE` / `IO_ERROR`；失败整单回滚，不留半个占位/outbox |
| `locker_repo_import_events` | `batch->count` 在 `1..4`；同页同 device+epoch；页内严格递增；每个 key 非 `(0,0)` | 新事件 + 可选 Core 投影变化 + 连续游标推进，**同一事务**提交；`*committed_cursor` = 本页最后一条的 key | `INVALID`（页内乱序/跨设备/跨 epoch/越过 `last_committed`）/ `CONFLICT`（游标不连续）/ `IO_ERROR`；重复导入（`new_count==0`）返回 `OK` 且**不重复改投影、不推进游标** |
| `locker_repo_close` | NULL 安全 | 先 rollback（若有未提交事务）再 `sqlite3_close_v2` | 无返回值 |

### 7.2 DDL 与字段映射

`host/storage/schema-v1.sql`（43 行，唯一 DDL 真源，构建期嵌入为 `locker_storage_schema_sql`）：

```sql
CREATE TABLE IF NOT EXISTS meta (name TEXT PRIMARY KEY NOT NULL, value TEXT NOT NULL) WITHOUT ROWID;
CREATE TABLE IF NOT EXISTS migrations (version INTEGER PRIMARY KEY NOT NULL, applied_at TEXT NOT NULL, checksum TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS documents (collection TEXT NOT NULL, key TEXT NOT NULL, value_json TEXT NOT NULL, PRIMARY KEY (collection, key)) WITHOUT ROWID;
INSERT OR IGNORE INTO meta(name,value) VALUES ('schema_version','1');
INSERT OR IGNORE INTO meta(name,value) VALUES ('snapshot_revision','0');
INSERT OR IGNORE INTO migrations(version,applied_at,checksum) VALUES (1,'1970-01-01T00:00:00Z','schema-v1');
```

字段映射（契约 `x-collection-schemas` 的 18 个受控集合 → `documents` 行）：

| collection | 记录 def | `key` 的取值规则（x-rules 05/06/07/15） | `value_json` |
|---|---|---|---|
| `users` | `UserRecord` | `user.id`（uuid 文本） | 整条 `UserRecord` |
| `username_index` | `IndexRecord` | 规范化 username | `{value, resource_id}` |
| `sessions` | `SessionRecord` | `id`（uuid） | 整条记录 |
| `cards` | `CardRecord` | `user_id`（uuid） | 整条记录 |
| `card_uid_index` | `IndexRecord` | `uid_hex`（8/14/20 位小写 hex） | `{value, resource_id}` |
| `tools` | `ToolRecord` | `id`（uuid） | 整条记录 |
| `operations` | `OperationRecord` | `operation.id`（uuid） | 整条记录 |
| `loans` | `Loan` | `id`（uuid） | 整条记录（归还时改写 `closed_at`，不删不覆盖借用者） |
| `outbox` | `OutboxRecord` | `id`（uuid） | 整条记录 |
| `idempotency` | `IdempotencyRecord` | `key`（uuid） | 整条记录 |
| `audits` | `AuditRecord` | `id`（uuid） | 整条记录 |
| `slot_guards` | `SlotGuardRecord` | 字符串 `"1"` | `{slot_id:1, operation_id}` |
| `device_state` | `DeviceStateRecord` | 设备标识 | 整条记录 |
| `import_cursors` | `ImportCursorRecord` | `<device_id>/<journal_epoch>`（61 B） | `{device_id, journal_epoch, through, continuity_ok}` |
| `device_events` | `DeviceEventRecord` | `<device_id 24>/<epoch 36>/<boot 20 补零>/<seq 20 补零>`（103 B） | 整条记录 |
| `vision_operations` | `VisionOperationRecord` | `operation_id` | 整条记录 |
| `vision_captures` | `VisionCaptureRecord` | 捕获 id | 整条记录 |
| `vision_evidence` | `VisionEvidence` | `operation_id + "/" + evidence_id`（73 B） | 整条记录 |

u64 一律是**已校验十进制文本**（`"0"`、`"18446744073709551615"`），存 `TEXT`，比较走 C 侧数值比较，不用 SQLite 数值列也不靠字典序（x-rules 12）。事件键的 20 位补零让 `BINARY` 排序等于数值排序，于是 `10` 不会排在 `2` 前。

### 7.3 线程 / 生命周期

- `locker_repo_t` **只允许 Core 业务线程使用**（`docs/architecture/module-ownership.md:24`）；仓储内部不持有后台线程、不回调用户代码、不发送设备动作。
- `open`/`close` 是所有者独占的生命周期调用；`close` 不得与其他调用并发，且必须在 Core 停止后调用。并发 `close` 与 `commit_change` 是 data race，不是可恢复错误。
- cJSON 有进程级全局状态，因此**不能**把端口改成多线程并发解析；真实的多实例竞争是「两个进程打开同一库文件」，由 SQLite 文件锁 + `BEGIN IMMEDIATE` 保证只有一个成功（`test_concurrent_slot_guard` 即此形态）。

## 8. docs / residue

- 本目录证据 **54 个文件**。分组：①VM canonical 16 个（`vm-s102-*`，含 `vm-s102-manifest.sha256`、`vm-s102-run-id.txt`、`vm-s102-run-summary.log`）；②清单 2 个（`source-manifest.txt`、`manifest.sha256`）；③设计检查结构化输出 8 个（`check_{contracts,stage_plan,vision_contracts,feasibility}.{json,log}`）；④变异 5 个（`mutation.log`、`mutation-batch.py`、`mutation-joint-m26c.py`、`mutation-batch-parsed.json`、`mutation-batch-attribution.json`）；⑤审计 16 个（`audit.md` + `audit-round2..9.md` 共 9 份报告，`audit-round8-prompt.txt`/`audit-round8-runner.ps1`/`audit-round8-run.log` 与 `audit-round9-prompt.txt`/`audit-round9-runner.ps1`/`audit-round9-run.log`/`audit-round9-output.txt` 共 7 个可复现输入与原始输出）；⑥取证脚本 5 个（`evidence-run-vm.sh`、`evidence-make-snapshot.sh`、`evidence-make-manifest.sh`、`evidence-design-checks.sh`、`evidence-copy-design-checks.sh`）；⑦`summary.md` 与 `handoff.md` 2 个。
- **2026-10-25 更新（用户 m19922 指令）**：原「本机取证 15 个」一组的 WSL2 日志（`configure.log`、`build.log`、`ctest.log`、`unit-tests.log`、`design-checks.log`、`environment.log`、`asan-*.log`、`tsan-*.log`、`manifest-check.log`、`source-verify.log`）连同 `run-id.txt`、`mutation-batch.log`、`mutation-joint-m26c.log`、`evidence-run-wsl.sh` 共 18 个已全部删除。删除后本卡 Linux 证据只剩 VMware 干净 guest；`mutation.log` 与两个 JSON 是由那批 WSL 原始日志归一化得到的记录（文件头部自述 provenance），原始日志已按指令删除，**同一批次无法在 VM 逐字节重放**，但 `mutation-batch.py` 与 `mutation-joint-m26c.py` 的定义仍完整保留、可在 VM 按 §5.4 重新生成等价批次。
- `audit.md` / `audit-round2.md` … / `audit-round9.md` 均由独立审计 AI 产出，开发 AI 不代写；本轮只在它们之外提交整改，不改它们的结论。round-3 审计附录 A 里一段调用脚本主函数的代码因含 markdown 链接形状的括号序列，被 `check_contracts.py` 的链接正则误判为本地链接，已改写成 `fn = m["main"]` + `sys.exit(fn(["--check"]))`（只消除假链接，不改结论与正文）。
- 临时残留：按 round-9 N-2 整改，**不再使用 `PUBLIC_LEFT=0` 之类无法外部复核的全局断言**，改为有批次与时间的事实，完整清单见 `handoff.md` §11.1 / §11.2。本轮（2026-10-24T22:10+08:00）实测：`C:/Users/Public` 清理前 37 个 `s102_*`/`_r9*`/`check_*` 条目共 82,152,050 B → 删除后 0；系统 TEMP 10 个相关条目 → 删除后仅剩 1 个与本卡无关的 `workbuddy-product-spill-*`；WSL `$HOME` 8 个 `s102_*` → 0，`/var/tmp` 计数 0；VM `/tmp` 6 个 `s102*` → 0，`$HOME` 下 `s102_*` 计数 0，卡源码 `SRC_STRAY_ARTIFACTS=0` / `SRC_FILES=1016`；仓库根 `.tmp_*`/`s102_*`/`_mut*`/`_r8*` 计数 0。**先转存后删除**：审计提示词/运行脚本/原始输出、6 个取证脚本、2 个变异解析 JSON 已复制进证据目录并逐字节核对，才删暂存副本。
- 收尾最后一轮清理（2026-10-24T22:22+08:00，改完入口文档与 `stage-plan.json` 之后）：`C:/Users/Public` 剩余 6 个本卡相关条目（`s102_dc3/`、`s102_dc4/` 两个设计检查暂存目录与 4 个 `.sh`，共 2,024 B）全部删除 → 该目录只剩系统自带的 11 个条目（`.MUMUVMM`、`AccountPictures`、`Desktop`、`Documents`、`Downloads`、`Libraries`、`Music`、`Pictures`、`Videos`、`Win`、`desktop.ini`），`s102_*`/`_r9*`/`_mut*`/`_r8*` 过滤计数 0；系统 TEMP 按 `s102|vm1xxxxx|vm_ev|vmlogs` 过滤计数 0。WSL `$HOME/s102_*` 0、`/var/tmp/s102*` 0；VM `/tmp/s102*` 0、`$HOME/s102_*` 0。删除前 `s102_dc4/` 的四个 JSON 与 `design-checks.log` 已复制进本证据目录（`design-checks.log` 头部时间戳 `2026-10-24T22:25:39+08:00`，四项 EXIT=0，`check_stage_plan` 报 `active_task null` / `implementation_cards_done 2`）。
- 仓库内仍存在的 `.o/.a/.so/.exe/.obj/.pyc` 全部落在 `.gitignore` 覆盖的既有构建残留里（`build/host/`（S1-01 的 CMake 构建目录）、`build/keil-env/`、`build/firmware-env/`、`STM32F4xx_HAL_Code/USER/project_demo/keil_project/Objects/`），都是此前 firmware/env 与 S1-01 留下的、**不是本卡产物**；本卡当前主体构建在 WSL `$HOME/s102_r9ev2/` 与 VM `$HOME/locker-workspace/build/S1-02/20261024-213500/` 下完成（源码树之外），不写仓库内 `build/`；`s102_r7ev` 与 run `20261024-193500` 属 round-7 历史批次，仅作历史记录引用。
- VM 内 `~/locker-workspace/S1-02/src`（源码，1016 文件）、`~/locker-workspace/build/S1-02/20261024-213500`（含 `-asan`/`-tsan`）、`~/locker-workspace/logs/S1-02/20261024-213500`、`~/locker-workspace/artifacts/S1-02` 下的快照包与日志按工作区规则保留（非仓库内容；`build/S1-02/` 与 `logs/S1-02/` 另有历史批次 `20261024-{014545,055243,063018,114250,125905,135713,152903,174731,174805,174830,193500}`，均为各轮取证产物）；`src` 只放源码，`SRC_STRAY_ARTIFACTS=0` 已复核。注意顶层 `~/locker-workspace/src` 不含 `host/storage`。
- 共享文件修改（仅本卡获授权的部分）：根 `CMakeLists.txt` 两行接入（`host/storage` + `tests/unit/storage`）；`deployment/host/dependencies.lock.json` 与 `deployment/host/README.md` 的 vendor 状态更新（审计 N4，由集成负责人身份执行）。未改 `contracts/` 真源、未改生成器、未改 `docs/architecture/` 规格正文。


## 9. decision

- **收尾结论：S1-02 DONE**。九轮独立审计中前七轮 FAIL、第八轮（`audit-round8.md`）与第九轮（`audit-round9.md`，超轮联合确认轮）均 PASS；第九轮明确「未发现尚未修复的阻塞项 B-x」，其 2 项非阻塞（N-1/P3 旧口径残留、N-2/P3 暂存清零声明）已在收尾时逐条订正并写进 §5.14，`handoff.md` §11 整段重写为可复核的实测事实。按用户 m12834 的超轮规则（审计超 3 轮由开发 AI 与审计 AI 共同修复并共同确认通过），本卡据此置 DONE。
- 本卡 DONE **不等于**阶段 1 PASS：阶段 1 的门禁卡是 S1-07，`implementation_status` 仍 `IN_PROGRESS`、`gate_result` 仍 `NOT_RUN`。本卡只交付主机侧 SQLite 单写仓储与其证据，不覆盖 RK 存储介质掉电、ARM/32 位、MCU/UART/锁/相机/NPU 等实物门禁。
- 未改 `contracts/` 真源、未改生成器、未改 `docs/architecture/` 规格正文；共享文件只动了根 `CMakeLists.txt` 两行接入与 `deployment/host/` 的 vendor 状态说明（审计 N4，集成负责人身份执行）。
- 不 commit / 不 push（仓库不是 Git 仓库，`git status` exit=128；用户未要求提交）。
- 下一张计划卡是 S1-03（UART 字节端口与独立模拟设备），按用户 m03821/m03823 的全自动循环由下一轮开发 AI 起卡，本卡不代做。
- 入口文档已同步到收尾状态（本卡收尾负责人执行）：`README.md`（当前发给开发AI = S1-03 指令、当前状态列 S1-01/S1-02 DONE、`active_task=null` / `recommended_next=S1-03`、阶段1任务卡行）、`AGENTS.md`（:3 与 :14）、`docs/stages/README.md`（:3 与 :47）、`docs/stages/stage-01-task-cards.md`（:3 状态行）、`docs/AI开发交接流程.md`（:3）、`docs/AI开发执行手册.md`（:70）。`config/development/stage-plan.json` 由 S1-02 `IN_PROGRESS`/report=null 改为 `DONE`/report=`docs/evidence/stage-01/S1-02/summary.md`，`active_task` 置 `null`、`recommended_next` 置 `S1-03`；阶段级字段（`implementation_status=IN_PROGRESS`、`gate_result=NOT_RUN`、`gate_task=S1-07`）未动。改状态后在 WSL2 重跑四项设计检查（`2026-10-24T22:25:39+08:00`）四项仍 exit 0，`check_stage_plan` 报 `active_task null`、`implementation_cards_done 2`。
