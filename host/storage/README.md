# host/storage — SQLite 单写仓储模块

本模块是**阶段 1 S1-02** 的交付物，也是整个主机侧唯一的 SQLite 访问点。

- 契约基线：0.2.1（`contracts/contract-source.json`），执行前核对过实际版本，未使用旧副本。
- 状态：S1-02 **DONE**（见正式证据）；本模块不是阶段 1 正式门禁，阶段 1 门禁卡是 S1-07。
- 归属：`docs/architecture/module-ownership.md:24` —— SQLite 负责人；禁止 HTTP 线程直接调用、禁止把动作发出放进 SQLite 回调。

## 1. 这个模块做什么、不做什么

做：

- 唯一 DDL 真源 `schema-v1.sql`（`meta` / `migrations` / `documents` 三表）与迁移元数据。
- 六个契约端口 `locker_repo_{open,close,query,load_recovery,commit_change,import_events}`。
- 事务边界：**操作 + 幂等 + outbox + revision 同一事务**；**新事件 + Core 给的投影 + 游标同一事务**。
- 按 `contracts/internal-data.schema.json` 的 18 个受控集合做写入前校验（冻结子集 + fail closed）。
- u64 一律以**已校验十进制文本**存储、按数值比较（x-rules 12）。
- 分页与恢复页按 65535 字节预算裁页，未读完必须给续页键（x-rules 09/10）。

不做（越界）：

- 不判断借还是否成功、不决定业务状态迁移 —— 投影由 Core 构造，仓储只校验并落库。
- 不发送设备动作：本模块没有 socket/serial/HAL 依赖。
- 不改公共 schema 或状态枚举：`contracts/` 在本卡零改动。
- 不提供 HTTP/UI 直连入口。

## 2. 目录

```text
host/storage/
  CMakeLists.txt                 三个目标：thirdparty / generated / locker_storage
  schema-v1.sql                  唯一 DDL 真源（43 行，构建期嵌入）
  cmake/embed_file.cmake         构建期把真源文件逐字节嵌成 C 数组片段
  tools/extract_schema.py        契约 schema 子集抽取 + --check 漂移门禁
  include/locker/storage/        模块私有头（不是契约 API，别的模块不得包含）
    schema.h  event_key.h  u64.h  regex.h  embedded.h
  src/
    repository.c                 六个端口 + 事务/查询/分页/导入
    repository_internal.h        模块内部共享声明（不对外）
    schema.c                     JSON Schema 子集校验器（fail closed）
    schema_internal.h            cJSON 层内部入口（含嵌入 NUL 守卫）
    schema_records.json          契约 schema 的冻结子集（47 defs / 18 collections）
    schema_embedded.c            手写外壳，#include 生成的 .inc
    event_key.c  u64.c  regex.c
  third_party/
    sqlite/  sqlite3.c sqlite3.h sqlite3ext.h   SQLite 3.54.1 amalgamation（未改一字节）
    cjson/   cJSON.c cJSON.h LICENSE            cJSON 1.7.19（未改一字节）
```

对外只有 `contracts/include/locker_contracts.h` 里的六个端口；`host/storage/include/` 下的头是模块私有工具头。

## 3. 构建接入

根 `CMakeLists.txt` 里属于本模块的只有两行：`add_subdirectory(host/storage)` 与 `add_subdirectory(tests/unit/storage)`。
本模块自己提供三个目标：

| 目标 | 内容 | 警告门禁 |
|---|---|---|
| `locker_storage_thirdparty` | vendored `sqlite3.c` + `cJSON.c` | **不加** `locker_apply_warnings`（第三方代码不在此修改） |
| `locker_storage_generated` | 构建期（`add_custom_command`）把 `schema-v1.sql` / `schema_records.json` 嵌成 `.inc` | — |
| `locker_storage` | 本模块 6 个 `.c` | `locker_apply_warnings`（`-Wall -Wextra -Wpedantic`，可选 `-Werror`） |

SQLite 编译定义固定为 `SQLITE_THREADSAFE=1`、`SQLITE_OMIT_LOAD_EXTENSION`、`SQLITE_DEFAULT_MEMSTATUS=0`、`SQLITE_DQS=0`、`SQLITE_USE_URI=1`。

## 4. 构建与测试

```bash
# 构建目录必须在源码树之外。VM 上按 docs/development/vm-home-layout.md 的
# $HOME/locker-workspace 分类放：build/<卡号>/<批次>，src 只放源码。
build="${HOME}/locker-workspace/build/S1-02/manual"
cmake -S . -B "$build" \
      -DLOCKER_BUILD_HOST=ON -DLOCKER_BUILD_TESTS=ON -DLOCKER_WARNINGS_AS_ERRORS=ON
cmake --build "$build" -j2
ctest --test-dir "$build" --output-on-failure
```

Linux证据仅使用VMware Ubuntu；构建需满足「build 目录不在源码树内」即可（例如 `$HOME/s102_build`）；
VM 的 canonical 布局固定为：卡源码在 `$HOME/locker-workspace/S1-02/src`，构建在 `$HOME/locker-workspace/build/S1-02/<run>`，日志在 `$HOME/locker-workspace/logs/S1-02/<run>`，归档在 `$HOME/locker-workspace/artifacts/S1-02`（顶层 `$HOME/locker-workspace/src` 是工作区级旧快照，不含本卡的 `host/storage`）。

`-DLOCKER_BUILD_TESTS=ON` 是必需的，否则 CTest 会报 `No tests were found`（根 CMake 只在打开该选项时才 `enable_testing()` 并接入测试目录）。

当前 12 个 CTest：`common.*` 6 个、`storage.*` 5 个 C 测试 + `storage.schema_source_drift` 契约漂移门禁。
断言数与双环境结果见 `docs/evidence/stage-01/S1-02/summary.md`。

## 5. 契约 schema 子集的再生成（重要）

`src/schema_records.json` 不是手写副本，而是 `contracts/internal-data.schema.json` 的**机器抽取子集**：

- 起点：`x-collection-schemas` 的 18 个记录名 + 7 个仓储 DTO 根（`RepoDocument`/`RepoCheck`/`RepoMutation`/`RepositoryChange`/`RepositoryQuery`/`RepositoryQueryResult`/`RepositoryRecoveryPage`）。
- 沿 `#/$defs/<name>` 做闭包，当前 47 个 def；未知 def 或无法解析的 `$ref` 直接失败。
- 去 `description`、键递归排序、紧凑单行、末尾一个换行；必须用**二进制 I/O** 写，否则 Windows 会把 LF 翻成 CRLF。

契约变化后必须重跑，否则校验器会按旧子集放行/拒绝：

```bash
python -B host/storage/tools/extract_schema.py          # 重新生成
python -B host/storage/tools/extract_schema.py --check  # 只比对，漂移时 exit 1
```

CTest 里的 `storage.schema_source_drift` 就是 `--check`：契约改了而冻结子集没跟上，构建立即变红。
该测试需要 Python 3；没有解释器时 CMake 会打印 `contract-drift gate SKIPPED`，不会假装通过。

## 6. 线程与生命周期

- `locker_repo_t` **只允许 Core 业务线程使用**（`docs/architecture/module-ownership.md:24`）。仓储内部不建线程、不回调用户代码。
- `open`/`close` 由所有者独占；`close` 不得与其他调用并发，且必须在 Core 停止后调用。并发 `close` 与 `commit_change` 是 data race，不是可恢复错误。
- cJSON 有进程级全局状态，因此**不能**把端口改成多线程并发解析。真实的多实例竞争是「两个进程打开同一库文件」，由 SQLite 文件锁 + `BEGIN IMMEDIATE` 保证只有一个成功。
- `SQLITE_THREADSAFE=1` 只说明 SQLite 自身可串行化，**不是**本模块可被多线程并发调用的许可。

## 7. 已知边界（不伪造）

| 项 | 状态 | 说明 |
|---|---|---|
| RK 存储介质掉电可靠性 | **NOT_RUN** | 无 RK 板与断电设备；只验证到 SQLite 语义层（WAL + `synchronous=FULL`）与正常 close/reopen，不声称掉电持久性 |
| `synchronous=FULL` 运行时断言 | **未覆盖** | 连接级 pragma，另一连接不可观测；如实登记，不加测试后门 |
| 32 位 / ARM 交叉编译 | **NOT_RUN** | 只在 x86_64 Linux 验证 |
| 业务闭环（借还全流程） | **不属于本模块** | 属 S1-04 及以后 |

## 8. 相关文档

- 实施报告与证据：`docs/evidence/stage-01/S1-02/summary.md`
- 调用/失败路径/复现方法：`docs/evidence/stage-01/S1-02/handoff.md`
- 独立审计结论：`docs/evidence/stage-01/S1-02/audit.md`
- 依赖锁定与许可：`deployment/host/dependencies.lock.json`、`deployment/host/licenses/`
- 契约真源：`contracts/include/locker_contracts.h`、`contracts/internal-data.schema.json`
