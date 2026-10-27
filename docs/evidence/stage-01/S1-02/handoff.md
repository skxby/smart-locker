# S1-02 SQLite 单写仓储与事务 — 交接文档（handoff）

给下一个 AI / 下一个模块的交接。本文件与 `summary.md` 配套：
`summary.md` 记录**做了什么、证据在哪、验收怎么对**；本文件记录**怎么调用、怎么复现、边界在哪、什么还没做**。

状态：**DONE**（九轮独立审计：前七轮 FAIL——首轮 B1–B5/N1–N5、round-2 R2-B1/R2-B2/R2-B3 + R2-N1–R2-N4、round-3 B-1/B-2 + N-1–N-3、round-4 B-1/B-2 + N-1–N-3、round-5 B-1 + N-1/N-2、round-6 B-1 未知触发器/B-2 隐藏生成列 + N-1 归因/N-2 文档、round-7 B-1 表内 CHECK/FOREIGN KEY + N-1 归因/顺序/计数；第八轮 `audit-round8.md` **PASS**——无阻塞项、4 项非阻塞 N-1～N-4 已整改；第九轮 `audit-round9.md`（用户 m12834 超轮规则下的联合确认轮）**PASS**——无阻塞项、2 项非阻塞 N-1/P3 旧口径残留与 N-2/P3 暂存清零声明已逐条订正，见 `summary.md` §5.14 与本文件 §11）。两轮 PASS 的 C 端 configure/build/CTest/ASan/TSan/C mutation 均为 NOT_RUN（审计侧环境受限），本卡 C 端证据取自开发侧 VMware 干净 guest 的实跑日志（原 WSL2 一组的 18 个产物已按用户 m19922 指令删除，不再作为依据）。本卡 DONE **不等于**阶段 1 PASS：阶段 1 门禁卡是 S1-07，`implementation_status` 仍 IN_PROGRESS、`gate_result` 仍 NOT_RUN。

---

## 1. 一句话结论

`host/storage/` 提供**唯一**的 SQLite 单写仓储：一份 `schema-v1.sql` DDL、一套迁移元数据（`meta` + `migrations`）、六个契约端口 `locker_repo_{open,close,query,load_recovery,commit_change,import_events}`。
事务边界就是卡要求的边界：**操作 + 幂等 + outbox 一起提交**（`commit_change`）、**新事件 + Core 投影 + 游标一起提交**（`import_events`）。
仓储不判断借还成功、不发设备动作、不暴露给 HTTP/UI 线程。

---

## 2. 目录与文件

```
host/storage/
  README.md                      模块说明（构建接入、线程/生命周期、schema 再生成、已知边界）
  CMakeLists.txt                 三个目标：thirdparty / generated / locker_storage
  schema-v1.sql                  唯一 DDL 真源（43 行）
  cmake/embed_file.cmake         构建期把真源文件嵌成 C 字节数组片段（add_custom_command）
  tools/extract_schema.py        契约 schema 子集抽取器（--check 供 CTest 漂移门禁调用）
  include/locker/storage/
    schema.h                     校验器公共入口（纯文本 API，不泄 cJSON）
    event_key.h                  事件键文档 key 编解码
    u64.h                        u64 规范十进制
    regex.h                      受限正则（fail closed）
    embedded.h                   嵌入资源 extern 声明
  src/
    repository.c                 六个端口 + 事务/查询/分页/导入 + schema 结构核验 + DDL 文本比对（2469 行）
    repository_internal.h        模块内部共享声明（不对外）
    schema.c                     JSON Schema 子集校验器（759 行，含嵌入 NUL 守卫）
    schema_internal.h            cJSON 层内部入口 + 嵌入 NUL 守卫声明
    schema_records.json          契约 schema 的冻结子集（47 defs / 18 collections）
    schema_embedded.c            手写外壳，#include 生成的 .inc
    event_key.c / u64.c / regex.c
  third_party/
    sqlite/  sqlite3.c sqlite3.h sqlite3ext.h   （3.54.1 amalgamation，未改）
    cjson/   cJSON.c cJSON.h LICENSE            （1.7.19，未改）
tests/unit/storage/
  CMakeLists.txt    5 个测试目标 + CTest 注册 + storage.schema_source_drift 漂移门禁
  test_repository.c  26 个测试 / 865 断言
  test_schema.c      11 个测试 / 131 断言
  test_event_key.c    7 个测试 /  80 断言
  test_u64.c          6 个测试 /  90 断言
  test_regex.c        6 个测试 /  46 断言
```

根 `CMakeLists.txt` 只有两行属于本卡：`:87 add_subdirectory(host/storage)`、`:92 add_subdirectory(tests/unit/storage)`。

---

## 3. 接口调用（对外只有契约里的六个端口）

头文件：`contracts/include/locker_contracts.h`。**不要**直接 include `host/storage/include/locker/storage/*.h`——那是仓储私有工具头，别的模块依赖它就算越界。

### 3.1 原型（契约行号）

```c
/* :190 */ typedef struct locker_repo locker_repo_t;

/* :273 */ locker_status_t locker_repo_open(const locker_repo_config_t *config, locker_repo_t **out);
/* :275 */ locker_status_t locker_repo_query(locker_repo_t *repo, const locker_repo_query_t *query,
                                              locker_repo_result_t *result);
/* :277 */ void             locker_repo_close(locker_repo_t *repo);   /* NULL 安全 */

/* :211 */ locker_status_t locker_repo_commit_change(locker_repo_t *repo, const locker_change_t *change);
/* :213 */ locker_status_t locker_repo_import_events(locker_repo_t *repo, const locker_event_batch_t *batch,
                                                       const locker_change_t *projection_change,
                                                       locker_event_key_t *committed_cursor);
/* :215 */ locker_status_t locker_repo_load_recovery(locker_repo_t *repo, const locker_repo_query_t *query,
                                                      locker_recovery_t *result);
```

### 3.2 相关 DTO（契约行号）

```c
/* :143 */ typedef struct { char path[256]; uint32_t schema_version; bool initialize_empty; } locker_repo_config_t;
/* :144 */ typedef struct { uint8_t schema_version; char collection[32];
                            char key[193]; char after_key[193]; uint16_t limit; } locker_repo_query_t;
/* :145 */ typedef struct { size_t length; char json[65536]; } locker_change_t;
/* :146 */ typedef struct { size_t length; char json[65536]; uint64_t snapshot_revision; } locker_repo_result_t;
/* :147 */ typedef struct { size_t length; char json[65536]; uint64_t snapshot_revision; } locker_recovery_t;
```

`locker_repo_query_t` 里 `key` 与 `after_key` 用**空串表示 JSON null**：
单键查询 = `key` 非空 + `after_key` 空 + `limit=1`；分页查询 = `key` 空 + `after_key` 空或非空 + `limit=1..100`。

### 3.3 打开 / 关闭（标准写法）

```c
#include <string.h>
#include "locker_contracts.h"

locker_repo_config_t config;
memset(&config, 0, sizeof(config));
strcpy(config.path, "/var/lib/locker/locker.db");
config.schema_version = 1u;      /* 必须等于 LOCKER_STORAGE_REPO_SCHEMA_VERSION */
config.initialize_empty = true;  /* 只在「首次初始化」时为 true；生产重启应为 false */

locker_repo_t *repo = NULL;
locker_status_t status = locker_repo_open(&config, &repo);
if (status != LOCKER_STATUS_OK) {
    /* repo 保证为 NULL；status 见 §6 失败路径表。不要在此清库。 */
    return status;
}
/* ... 使用 ... */
locker_repo_close(repo);
repo = NULL;
```

要点：

- `initialize_empty=true` 只在**首次初始化**时用。若库里已有 `documents` 表，它不会重建，只走校验。
- 生产环境重启必须传 `false`：库不存在时返回 `NOT_READY` 而不是悄悄新建一个空库（这正是「首次初始化与损坏/迁移失败区分」的一半）。
- 打开失败时 `*out` 一定保持 `NULL`，调用方不需要（也不应该）对 `repo` 调 `close`。

### 3.4 提交一次变化（`commit_change`）

入参是 `RepositoryChange` 的 JSON 文本，放进 `locker_change_t.json` 并设 `length`（不含结尾 NUL）。

```c
static const char *change_json =
    "{\"schema_version\":1,"
    "\"expected_snapshot_revision\":\"7\","
    "\"new_snapshot_revision\":\"8\","
    "\"checks\":[{\"collection\":\"slot_guards\",\"key\":\"1\",\"condition\":\"ABSENT\"}],"
    "\"mutations\":["
    "{\"op\":\"PUT\",\"document\":{\"collection\":\"operations\",\"key\":\"<op-uuid>\",\"value\":{...}}},"
    "{\"op\":\"PUT\",\"document\":{\"collection\":\"idempotency\",\"key\":\"<key-uuid>\",\"value\":{...}}},"
    "{\"op\":\"PUT\",\"document\":{\"collection\":\"outbox\",\"key\":\"<outbox-uuid>\",\"value\":{...}}},"
    "{\"op\":\"PUT\",\"document\":{\"collection\":\"slot_guards\",\"key\":\"1\",\"value\":{...}}}"
    "]}";

locker_change_t change;
change.length = strlen(change_json);
memcpy(change.json, change_json, change.length);

locker_status_t status = locker_repo_commit_change(repo, &change);
```

语义：

- 整个 change 在**一个 `BEGIN IMMEDIATE` 事务**里生效：先核 `expected_snapshot_revision` 与 `checks`，再按顺序应用 `mutations`，最后把 `snapshot_revision` 写成 `new`。任一步失败整单回滚。
- `new_snapshot_revision` 必须严格等于 `expected + 1`（`repository.c:704`，revision 比对在 `:713`）；`expected == UINT64_MAX` 直接 `INVALID`。
- 只接受 Core 线程构造的、已通过 schema 校验的 change。公开 API 层不得把用户输入直接转成 mutation（x-rules 08）。

### 3.5 导入设备事件（`import_events`）

```c
locker_event_batch_t batch;
memset(&batch, 0, sizeof(batch));
batch.count = 3u;
/* batch.events[0..2] 由 UART 层填好：key / mono_ms / physical_revision / event_type / ... */
/* batch.first_available / last_committed / next_after 由 UART 层按这一响应填
   （docs/architecture/uart-v1.md:368-370）：
   first_available  = 这条日志里最早还可以取到的记录位置（可以早于本页首条），
   last_committed   = MCU 已经持久提交的日志尾（可以晚于本页末条；注意这不是 RK 本端
                      的落库水位——RK 自己的水位是 committed_cursor / through），
   next_after       = 本页最后一条记录（非空页必须恰好等于本页末条）。
   三者都不能是空日志哨兵：非空页必须给真值，first_available 尤其不能置零。
   三者在 device_id/journal_epoch 上必须与本页事件同日志；
   三个键都必须是「全零哨兵」或「boot_id 与 event_seq 都非零」，半零一律 INVALID。 */

locker_event_key_t committed;
locker_status_t status = locker_repo_import_events(repo, &batch, NULL, &committed);
if (status == LOCKER_STATUS_OK) {
    /* OK 只表示本页记录/投影/游标已同一事务落库，*不等于*可以声称历史完整：
       若该日志此前已锁存缺口（continuity_ok == false），照样返回 OK。
       ACK_PERSISTED 的充分条件见下面语义第 8 条。 */
    /* 把 committed 记下来，作为本端「已持久化到哪」的水位 */
}
```

**没有投影时第三个参数传 `NULL`。** 传一个 `length = 0` 的非空指针不是“空投影”：
端口发现本页确有新事件时会按“给了投影但内容为空”处理并返回 `INVALID`
（`repository.c:1703-1722`）。

语义：

- `count` 必须在 `1..4`（`locker_event_batch_t.events[4]`）；`count == 0` 或 `> 4` 直接 `INVALID`。
- 同页必须**同一个 `device_id` + 同一个 `journal_epoch`**，且 `boot_id/event_seq` 严格递增（`event_keys_adjacent`：同 boot 要求 `next.seq == prev.seq + 1`，换 boot 要求 `next.boot > prev.boot` 且 `next.seq == 1`）；否则 `INVALID`。
- 批次边界必须自洽（审计 B2 / round-2 B2）：`last_committed` 不得为零；`first_available` **也必须非零**（非空页必须给真值，不能拿空日志哨兵顶替）；`last_committed`、`first_available`、`next_after` 必须与本页事件**同日志**（同 `device_id` + 同 `journal_epoch`）；`first_key <= last_key <= last_committed`；`first_available <= first_key`；`next_after == last_key`。任一不满足 `INVALID`，且不改事件、不改投影、不推进游标。
- 边界是**包络**而不是「本页首末条」（审计 round-3 N-3）：`first_available` 与 `last_committed` 都可以严格早于/晚于本页（`first_available < first_key`、`last_key < last_committed` 合法，见 `docs/architecture/uart-v1.md:368-370`），只有 `next_after` 必须恰好等于本页末条；`has_more` 表示 `last_committed` 之前还有更多记录。水位只推进到本页末条，不会跳到 `last_committed`。成功对照见 `test_import_boundary_envelope`。
- 三个边界键与每条事件键都必须是**结构合法**的：`boot_id` 与 `event_seq` **要么都为零（空日志哨兵），要么都非零**；只零一个是半零游标，一律 `INVALID`（`event_key_structure_ok`，依据 `docs/architecture/uart-v1.md:38`「禁止仅其中一个为 0」）。这条检查在 `first_available` 上尤其重要：只清 `boot/seq` 保留 `device/epoch` 的半零值能通过同日志校验，还会被数值比较当成「最早记录」。
- 事件去重靠 `device_events` 的文档 key（`<device 24>/<epoch 36>/<boot 20 补零>/<seq 20 补零>`，103 字节）。**重复事件不重复改投影、不推进游标**：`new_count == 0` 且 `gap_before_page == false` 时直接 `rollback` 并返回已存游标（状态 `OK`）。
- 全重复页但 `gap_before_page == true`（round-2 B1）：缺口事实**不能被「没有新事件」吃掉**。此时不推进 `through`、不重复应用投影，但仍把 `continuity_ok` 锁存为 `false`（`build_cursor_record(&stored_through, false)` + 覆盖写游标后提交），返回 `OK` 且 `*committed_cursor = stored_through`。若该页有事件却查不到已存游标（`!stored_exists`），说明状态不一致，返回 `CONFLICT`。
- 游标连续性（审计 B1）：已有游标时，本页首条必须**恰好接上**已存水位——同 boot 要求 `first.seq == stored.seq + 1`；换 boot 要求 `first.boot > stored.boot`（**允许跳号**，那次启动可能没落任何事件）且 `first.seq == 1`；否则 `CONFLICT`（缺口）。已存水位**不得倒退**：本页首条 `<= stored.through` 也返回 `CONFLICT`。首次建游标要求 `!gap_before_page && boot==1 && seq==1`。
- `continuity_ok` 是**锁存**的：一旦因首建不连续、`gap_before_page` 或「全重复页 + gap」变成 `false`，后续页即使连续也不会被洗白回 `true`。
- 有投影时，投影在**同一事务**里用 `apply_change` 的同一套 checks/revision 规则应用。
- 成功后 `*committed_cursor` = 本页最后一条事件的 key（全重复页为已存 `through`）。
- **`OK` 不等于可以 ACK `ACK_PERSISTED`。** `OK` 只说明「本页记录/投影/游标已同一事务落库」。可 ACK 的充分条件还要加上：本端此前没有未承认的缺口（`continuity_ok == true`），且 `committed_cursor` 相对上一条 ACK 水位**不倒退、不越过该页 `last_committed`**。`continuity_ok == false` 的 `OK` 只能回 `HISTORY_INCOMPLETE`（`docs/architecture/uart-v1.md:50` 的连续性失效锁存，即便 `event_seq` 连续也不能认为无丢失）。

### 3.6 查询与恢复分页

```c
locker_repo_query_t query;
memset(&query, 0, sizeof(query));
query.schema_version = 1u;
strcpy(query.collection, "loans");
/* 单键： */
strcpy(query.key, "<loan-uuid>");
query.limit = 1u;

locker_repo_result_t result;
locker_status_t status = locker_repo_query(repo, &query, &result);
/* result.json 是 RepositoryQueryResult；result.snapshot_revision 是读事务里的 revision */
```

```c
/* 分页：key 留空，after_key 给上一页的 next_after_key */
query.key[0] = '\0';
strcpy(query.after_key, "<上一页 next_after_key>");
query.limit = 50u;
locker_status_t status = locker_repo_query(repo, &query, &result);
```

- 分页按 **key 字节序**（`key > after_key`），不是插入序。
- 页大小同时受 `limit`（≤100）与 65535 字节上限；未取尽时 `next_after_key` 非 null。
- 恢复页用 `locker_repo_load_recovery`，DTO 是 `RepositoryRecoveryPage`（比 query 结果多一个 `collection` 字段）。**恢复页不接受单键查询**（`key` 必须为空），依据 x-rules 10「不一次装载无限历史」。

### 3.7 结果 JSON 的形状

`locker_repo_result_t.json` / `locker_recovery_t.json` 是**契约里的 JSON**，不是自定义结构：

```json
{"schema_version":1,"snapshot_revision":"8",
 "documents":[{"collection":"loans","key":"...","value":{...}}],
 "next_after_key":null}
```

`snapshot_revision` 在 JSON 里是**十进制字符串**（U64），同时也在 C 结构体的 `uint64_t snapshot_revision` 里给一份，方便不解析 JSON 的调用方。

---

## 4. 参数 / 内存 / 线程 / 销毁

### 4.1 内存

- 端口**不分配**调用方需要释放的内存。所有输出都写进调用方给的固定缓冲（`json[65536]`、`length`）。
- 结果超缓冲返回 `NO_SPACE`（`write_json_result`，`repository.c:647`），**不截断、不部分写**。
- `locker_repo_t` 本身由 `open` 内部 `calloc`，由 `close` 释放。中间失败路径全部 `schema_destroy + free + close_v2`，无泄漏（ASan 已验证）。

### 4.2 线程

- `locker_repo_t` **只允许 Core 业务线程使用**（`docs/architecture/module-ownership.md:24`）。仓储内部**没有后台线程**、不回调用户代码、不持有 socket/serial 句柄。
- `open`/`close` 是所有者独占的生命周期调用；`close` 不得与其他调用并发，且必须在 Core 停止后调用。并发 `close` 与 `commit_change` 是 **data race**，不是可恢复错误。
- cJSON 有进程级全局状态（`global_error`），**不能**多线程并发解析。因此不要把端口改成多线程并发调用；真实的并发场景是「两个进程打开同一库文件」，由 SQLite 文件锁 + `BEGIN IMMEDIATE` 保证只有一个成功。
- `SQLITE_THREADSAFE=1` 已编进 amalgamation，但那是 SQLite 内部串行化，不改变上面的单写者模型。

### 4.3 销毁顺序

```
Core 停止业务线程
  → 不再有任何 locker_repo_* 调用
  → locker_repo_close(repo)
  → repo = NULL
```

`close` 内部会先 `rollback` 未提交事务，再 `sqlite3_close_v2`。`close(NULL)` 安全。

---

## 5. 配置 / 依赖 / 接口版本

### 5.1 编译期

- 契约版本由根 `CMakeLists.txt:41-61` 在配置期校验：生成头 `#define LOCKER_CONTRACT_VERSION` 与 `contracts/contract-source.json` 必须一致，否则 `FATAL_ERROR`。当前 **0.2.1**。
- 契约公共头路径由 `LOCKER_CONTRACTS_INCLUDE_DIR`（`CMakeLists.txt:28-31`）注入，仓储 PUBLIC 依赖它，因此消费方 `target_link_libraries(... locker_storage)` 后可直接 include `locker_contracts.h`。
- 仓储 PUBLIC 依赖 `locker_common`（要 `locker_uuid_*`），PRIVATE 依赖 `locker_storage_thirdparty`（SQLite + cJSON）。**消费方看不到 sqlite3.h / cJSON.h**。

### 5.2 运行期

`locker_repo_open` 会设置（`repository.c:2355-2366`）：

| 设置 | 值 | 作用 |
|---|---|---|
| `sqlite3_busy_timeout` | 5000 ms | 竞争时等待而不是立刻 `BUSY` |
| `PRAGMA foreign_keys` | `ON` | 与 `schema-v1.sql` 头部一致 |
| `PRAGMA journal_mode` | `WAL` | 卡固定约束 |
| `PRAGMA synchronous` | `FULL` | 卡固定约束：提交返回前日志已落盘 |

打开后还会做：`verify_schema_tables`（`repository.c:2037`，真实调用顺序 `:2061` 列 → `:2065` 键索引 → `:2071` 对象清单 → `:2077` DDL 文本）：三张表齐全；逐列用 `pragma_table_xinfo`（`:1856`）核验列名/类型/NOT NULL/主键位置，并**拒绝任何 `hidden != 0` 的生成列/隐藏列**（`:1881`，round-6 B-2）；再用 `pragma_index_list`+`pragma_index_xinfo` 核验键列上有升序、`BINARY` 排序的唯一索引（partial UNIQUE 一律 fail-closed，`:2000`）；然后 `verify_official_objects`（`:2086`）要求 `sqlite_*` 之外**恰好三张表、没有视图/索引/触发器**（round-6 B-1）；最后 `verify_official_ddl`（`:2244`）把三张表的 `sqlite_master.sql` 与唯一 `schema-v1.sql` 提取出的文本**逐字节**比对（round-7 B-1：表内 CHECK / FOREIGN KEY 不产生对象、也不改列与索引视图，只有文本比对能拒）。之后还有 `verify_schema_metadata`（`:1765`：`meta.schema_version` 必须 `"1"`、`migrations` 必须有 version 1 行）+ `PRAGMA quick_check`（首行必须 `"ok"`，`:2296`）。

首次初始化（仅当目标库里**没有任何用户对象**——表、视图、索引、触发器都算）把 DDL 与元数据写入包在 `BEGIN IMMEDIATE` / `COMMIT` 里，提交前再跑一次 `verify_schema_tables`，失败 `ROLLBACK`：**有痕迹但结构不全的库直接 `CONFLICT`，不会被 `CREATE TABLE IF NOT EXISTS` 静默补全、也不会保留旧 revision**（审计 B3 / round-2 B3 / round-3 B-1、B-2）。两个易踩的点：①对象计数用 `name NOT GLOB 'sqlite_*'` 而不是 `NOT LIKE 'sqlite_%'`——LIKE 里 `_` 是单字符通配符，会把用户建的 `sqlitex_audit` 误判成内部对象（round-3 B-1）；②`documents` 这个名字被视图占用、或缺 `value_json` 列、或丢了 `(collection,key)` 唯一约束、或键排序规则是 `NOCASE`，全部 `CONFLICT`（round-3 B-2）。

### 5.3 依赖版本（与 `deployment/host/dependencies.lock.json` 对应）

| 依赖 | 版本 | 不可变标识 | 许可 | 归档 SHA-256 |
|---|---|---|---|---|
| SQLite amalgamation | 3.54.1（`SQLITE_VERSION_NUMBER 3054001`） | tag `version-3.54.1` / `sqlite-amalgamation-3540100.zip` | Public Domain（`deployment/host/licenses/sqlite-PUBLIC-DOMAIN.txt`） | `0a9c8e34352dad46006aa82836d5383bff9dc6bc9ce5a60553c05aa30e6ad1f9`（2990966 B） |
| cJSON | 1.7.19 | tag `v1.7.19`，commit `c859b25da02955fef659d658b8f324b5cde87be3` | MIT（`deployment/host/licenses/cjson-LICENSE.txt`） | `7fa616e3046edfa7a28a32d5f9eacfd23f92900fe1f8ccd988c1662f30454562`（356247 B） |

本卡实测：两个归档 SHA-256 与 lock 记录一致；6 个 vendored 文件与归档内对应成员**逐字节相同**。

**注意**：`deployment/host/dependencies.lock.json` 与 `deployment/host/README.md` 已按审计 N4 更新：新增 `vendored` 段（两棵 vendor 树的归档 SHA-256、逐文件 SHA-256、编译定义、`warning_gate` 说明），`cjson.consumer_cards` 改为 `["S1-02","S1-05"]`，`unresolved` 四条改写为「SQLite 与 cJSON 已 vendor；CivetWeb/libsodium/TLS 仍由 S1-05 负责」。CivetWeb/libsodium/TLS 仍未 vendor，属 S1-05。

---

## 6. 失败路径（逐条）

### 6.1 `locker_repo_open`

| 情况 | 返回 | 库文件 |
|---|---|---|
| `out == NULL` / `config == NULL` / `schema_version != 1` / `path` 空 | `INVALID` | 不动 |
| 库文件不存在，`initialize_empty=false` | `NOT_READY` | 不创建 |
| 库文件不存在，`initialize_empty=true` | `OK` | 创建 + 建表 + 写 meta/migrations |
| 文件存在但不是 SQLite（如写入任意文本） | `IO_ERROR` 或 `NOT_READY` | **原样保留，不覆盖**（`initialize_empty=true` 也不覆盖） |
| `meta.schema_version` 不是 `"1"` | `CONFLICT` | 原样保留，**不删库** |
| `migrations` 里没有 version 1 行 | `CONFLICT` | 原样保留 |
| `PRAGMA quick_check` 首行不是 `"ok"` | `IO_ERROR` | 原样保留 |
| 三张表之外还有用户对象（视图/索引/触发器/多出的表），或同名表重复（round-6 B-1） | `CONFLICT` | 原样保留，不删不改 |
| 列集合里出现 `hidden != 0` 的生成列/隐藏列（round-6 B-2） | `CONFLICT` | 原样保留 |
| 三张表的 `sqlite_master.sql` 与 `schema-v1.sql` 提取文本不是逐字节相等（表内 `CHECK` / `FOREIGN KEY` / 等价改写，round-7 B-1） | `CONFLICT` | 原样保留 |
| 打不开（权限/路径） | `IO_ERROR` | 不动 |
| 内存不足 | `NO_SPACE` | 已清理句柄 |

**任何失败都不会自动清库**（x-rules 13）。首次初始化只能走本机 CLI，不是网络请求路径。

### 6.2 `locker_repo_commit_change`

| 情况 | 返回 | 副作用 |
|---|---|---|
| `repo`/`change` 为 NULL，`length==0`，`length > 65536` | `INVALID` | 无 |
| JSON 语法错误 / 尾部有垃圾 | `INVALID` | 无 |
| 不符合 `RepositoryChange` schema（含未知字段、U64 非法、mutations 空、超 32 条） | `INVALID` | 无 |
| `new != expected + 1` 或 `expected == UINT64_MAX` | `INVALID` | 无 |
| 库里 `snapshot_revision != expected` | `CONFLICT` | 无 |
| `checks` 里某条 `ABSENT` 但文档存在 / `EXISTS` 但不存在 | `CONFLICT` | 无 |
| 某个 PUT 的 `value` 不符合对应 collection 的 schema | `INVALID` | **整单回滚**，不留半个占位/outbox |
| SQLite 返回 `BUSY`/`LOCKED` | `BUSY` | 无 |
| 磁盘满 / 内存不足 | `NO_SPACE` | 回滚 |
| 某个 PUT 执行成功但 `sqlite3_changes != 1`（预存触发器 `RAISE(IGNORE)` 静默吞写，round-6 B-1） | `IO_ERROR` | **整单回滚**，revision 不变 |
| 某个 DELETE 目标存在但 `sqlite3_changes != 1`（同上，DELETE 侧） | `IO_ERROR` | **整单回滚**；删除不存在的 key 仍是合法空操作 |
| 其他 SQLite 错误 | `IO_ERROR` | 回滚 |

### 6.3 `locker_repo_import_events`

| 情况 | 返回 | 副作用 |
|---|---|---|
| `repo`/`batch` 为 NULL，`count` 不在 1..4 | `INVALID` | 无 |
| 某事件 `device_id` 不是 24 字符 / key 是 `(0,0)` | `INVALID` | 无 |
| 同页跨 `device_id` 或跨 `journal_epoch` | `INVALID` | 无 |
| 同页 `boot/seq` 不是严格递增 | `INVALID` | 无 |
| 本页最后一条越过 `batch.last_committed` | `INVALID` | 回滚 |
| 游标不连续（跳号 / boot 倒退） | `CONFLICT` | 回滚，一条都不落 |
| 全部是重复事件（`new_count==0`） | `OK` | 不改投影、不推进游标，返回已存游标 |
| `projection_change` 非法 | `INVALID` | 回滚（事件也不落） |
| `projection_change` 的 expected revision 过期 | `CONFLICT` | 回滚 |
| 其他 | `IO_ERROR` / `NO_SPACE` | 回滚 |

### 6.4 `locker_repo_query` / `load_recovery`

| 情况 | 返回 |
|---|---|
| `repo`/`query`/`result` 为 NULL | `INVALID` |
| `schema_version != 1` | `INVALID` |
| `collection` 空或不在 18 个受控集合里 | `INVALID` |
| `key` 或 `after_key` 超 192 字节 | `INVALID` |
| 单键分支却给了 `after_key`，或分页分支给了 `key` | `INVALID` |
| recovery 却给了 `key` | `INVALID` |
| 结果 JSON 超 `json[65536]` | `NO_SPACE` |
| 单条文档就超过 65535 字节页上限 | `NO_SPACE`（不静默截断） |
| SQL 错误 / 结果自校验失败 | `IO_ERROR` |

---

## 7. 复现命令（下一个 AI 直接可跑）

### 7.1 VMware Ubuntu（验收主环境）

```bash
# 0) 连接
ssh locker-vm     # 或 ssh -o BatchMode=yes %USER%@192.0.2.10

# 1) 确认当前源路径与指纹（不要用旧顶层 src）
cd "$HOME/locker-workspace/S1-02/src"
# 用 evidence 里的 source-manifest.txt 生成校验清单（32 条目；第三列是路径）
awk '!/^#/ {print $1"  "$3}' \
  /path/to/repo/docs/evidence/stage-01/S1-02/source-manifest.txt > /tmp/manifest.sha256
sha256sum -c /tmp/manifest.sha256 | tail -3          # 期望 32/32 OK
grep -c ': OK$' <(sha256sum -c /tmp/manifest.sha256 2>/dev/null)   # 期望 32

# 2) 配置 / 构建 / 测试（布局：build 与 logs 在 src 之外）
RUN=repro-$(date +%Y%m%d-%H%M%S)
SRC="$HOME/locker-workspace/S1-02/src"
BUILD="$HOME/locker-workspace/build/S1-02/$RUN"
LOGS="$HOME/locker-workspace/logs/S1-02/$RUN"
mkdir -p "$BUILD" "$LOGS"
cmake -S "$SRC" -B "$BUILD" -DLOCKER_BUILD_HOST=ON -DLOCKER_BUILD_TESTS=ON \
      -DLOCKER_WARNINGS_AS_ERRORS=ON 2>&1 | tee "$LOGS/configure.log"
cmake --build "$BUILD" -j2           2>&1 | tee "$LOGS/build.log"
ctest --test-dir "$BUILD" --output-on-failure 2>&1 | tee "$LOGS/ctest.log"
# 期望：100% tests passed, 0 tests failed out of 12
#       （11 个 C 测试 + storage.schema_source_drift 这个 Python 漂移门禁）

# 3) 逐测试断言数
for t in "$BUILD"/tests/unit/storage/test_* "$BUILD"/tests/unit/common/test_*; do
    [ -x "$t" ] && "$t"
done | tee "$LOGS/unit-tests.log"
# 期望 11 行 PASS（漂移门禁不是 C 二进制，不在其中），合计 1717 断言

# 4) ASan + UBSan
cmake -S "$SRC" -B "$BUILD-asan" -DLOCKER_BUILD_HOST=ON -DLOCKER_BUILD_TESTS=ON \
      -DLOCKER_WARNINGS_AS_ERRORS=ON \
      -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-sanitize-recover=all" \
      -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build "$BUILD-asan" -j2 && ctest --test-dir "$BUILD-asan" --output-on-failure
# 期望 12/12 Passed，无 sanitizer 报告

# 5) TSan
cmake -S "$SRC" -B "$BUILD-tsan" -DLOCKER_BUILD_HOST=ON -DLOCKER_BUILD_TESTS=ON \
      -DLOCKER_WARNINGS_AS_ERRORS=ON \
      -DCMAKE_C_FLAGS="-fsanitize=thread" -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=thread"
cmake --build "$BUILD-tsan" -j2 && ctest --test-dir "$BUILD-tsan" --output-on-failure
# 期望 12/12 Passed，无 ThreadSanitizer 报告

# 6) 四项设计检查（只证明设计一致性）
cd "$SRC"
for s in check_contracts check_stage_plan check_vision_contracts check_feasibility; do
    python3 -B "scripts/$s.py" > "$LOGS/$s.log" 2>&1; echo "$s exit=$?"
done
# 期望 4 个 exit=0、status PASS
```

### 7.2 无 VM 时的本机复现（仅作手工核对，不产生本卡证据）

本卡的 Linux 证据只有 VMware 干净 guest 一组。若一时没有 VM，可以在任意 Linux 文件系统上按同一组 CMake 参数自建构建目录核对行为，但**这不算本卡验收证据**，也不要用它的日志替换 `vm-s102-*`：

```bash
# 构建目录必须放在源码树之外（工作区规则：build/<卡号>/<批次>）
cmake -S . -B "$HOME/locker-workspace/build/S1-02/manual" \
      -DLOCKER_BUILD_HOST=ON -DLOCKER_BUILD_TESTS=ON -DLOCKER_WARNINGS_AS_ERRORS=ON
cmake --build "$HOME/locker-workspace/build/S1-02/manual" -j2
ctest --test-dir "$HOME/locker-workspace/build/S1-02/manual" --output-on-failure
```

按用户 m19922 指令，原先的 WSL2 快速迭代一节（含 `wsl -e` 路径映射与 PowerShell 引号注意事项）已删除，本卡不再保留任何 WSL2 取证流程。

### 7.3 变异测试（验证测试非空洞）

做法（可复用，见 `mutation.log` 头部注释）：把 `CMakeLists.txt + host + tests + contracts` 复制到独立目录，对被测源码做**单个定向变异**，独立构建目录跑完整 CTest，FAIL 行**从同一次 `ctest --output-on-failure` 的 stdout 抽取**（`mutation-batch.py:162-177`），没有第二次单独执行测试二进制。变异前必须校验替换串在文件里**恰好出现 1 次**（`count != 1` 就中止），否则会静默改错位置。

当前 **42 次运行**（34 个具名点位 + 8 个「关掉 DDL 文本兜底」的联合点位），round-7 整改后全部重跑；日志开头 `SOURCE_FINGERPRINT` 记录 11 个被测源文件的 sha256 前 12 位与字节数，证明这一批打在最终源码上（repository.c `b9ab2abaa41e` / 95832 B、test_repository.c `f1cc609cf4cf` / 138538 B 等）。具名点位：m1_revision / m2_dedup / m3_adjacent / m4_u64_fallback / m5_schema_additional / m6_sync（未捕获）/ m7_gap_latch / m8_trace_guard / m9_page_budget（未捕获）/ m9b_doc_bytes / m10_nul_guard_raw / m11_parse_nul / m12_boundary_structure / m13_first_available_zero / m14_gap_latch_early / m15b_view_double（三点联合）/ m16_sqlite_prefix / m17_verify_columns（单点被 DDL 兜底遮蔽）/ m18_verify_key_index（同）/ m20b_any_matching_index（同）/ m21b_skip_column_collation（同）/ m22_skip_extra_unique_index（同）/ m25_regex_bounded_zero / m26_partial_unique（同）/ m26b_partial_plus_objects（m26+m28 联合）/ m27_regex_min_seed / m28_official_objects / m29_table_info（同）/ m30_hidden_only（同）/ m31_put_changes / m32_delete_changes / m33_ddl_skipped / m34_ddl_length_only / m34b_ddl_text_not_compared。联合点位：m17/m18/m20b/m21b/m22/m26/m29/m30 各配一个 `_plus_ddl_off`（即再叠加 m33 关掉 DDL 比对）。m23/m24 与 m17/m18 同锚点、`m15_object_type` 单点无法独立恢复旧行为、`m29b_xinfo_to_info_only` 与 m29 的 edit 逐字节相同（round-7 N-1 判定为重复执行），均已移出批次。逐个位置、期望与实测见 `summary.md` §5.4 与 `mutation.log`。

**42 次运行中 32 次变红、10 次未捕获**（m6_sync / m9_page_budget / m17 / m18 / m20b / m21b / m22 / m26 / m29 / m30，如实登记，不折算成「独立缺陷数」）；以下十三点必须说清（完整版见 `summary.md` §5.4 的「十二点边界」）：

- **m4 的锚点没换，换的是输入可达性**（round-4 N-2 勘误）。锚点一直是 `u64.c:51` 的非法文本回退分支；旧版没有任何用例喂非规范文本，合法输入根本不进这个分支，所以当时的捕获记录不能证明当前实现被覆盖。现新增 `test_compare_text_noncanonical` 专门喂非规范文本（`"7" vs "07x"`、`"" < "0"`），捕获 2/90。
- **m8 原名 `init_tx` 是误称**，它打的是「有痕迹但结构不全的库必须 CONFLICT」这一判定（`repository.c:2393`），**不是** `BEGIN IMMEDIATE`/DDL/`ROLLBACK` 本身。初始化事务中途失败的回滚目前**没有注入式回归**（见 `summary.md` §5.5），不要据 m8 声称已证。
- **m9b 的分母是 850 不是 865**：失败后 `root == NULL`，后续依赖 root 的断言被跳过，不能把成功批次总数当分母。
- **m12 第一版是无效变异**：替换串把三行条件截断成语法错误（`BUILD_EXIT=2`，根本没跑到测试），改成整段三行替换成 `if (0) {` 后重建才捕获——「变异失败」要区分「测试没覆盖」与「变异本身没编译」。本轮 42 个点位全部 `configure=0 build=0`，已排除该干扰。
- **m15b 不能证明「首次 `open(true)` 假成功」**（round-5 N-1 ① 改正）：`test_documents_view_rejected` 的首次 `open(true)` 在 `test_repository.c:2122`，没有 FAIL；唯一视图 FAIL 是 `:2125` 的 `open(false)`（CONFLICT 变 NOT_READY）。即使关掉对象类型/视图/痕迹三处（`repository.c:256 + 2388 + 2393`），初始化前的 `verify_schema_tables`（`:2409`）仍核列，视图列 type 为空、notnull=0、pk=0，会被拒并 ROLLBACK。所以 m15b 的 4 条红行证明的是**返回分类与有痕迹保护有回归**，不是「假成功被捕获」；生产路径本身静态拒绝视图，不存在当前视图漏洞。
- **m10/m11 的红行不用「跨用例 revision 连带」解释**（round-5 N-1 ⑤ 指出旧解释已过期）：现每个 NUL 负例都独立建库、先建立 revision=1 基线，`:1881`/`:1886` 的各次命中是同一用例内的预期后置检查（status 与 documents 数），不是前一个用例推进了 revision。各类 NUL 的独立性另由干净库正对照与两个带合法 mutations 的裸字节用例保证。
- **m23/m24 已从批次移除**（round-4 N-1 登记为重复点位、round-5 整改时删除）：它们与 m17/m18 同锚点、失败行完全相同，属重复执行，保留会虚增捕获数。同理 `m15_object_type` 单点已移出批次（round-3 N-1），其残留目录单独重跑 0 失败，不列为捕获项。
- **m25 的旧描述是错的，已按实际变异重写**（round-5 N-1 ④）：变异点是 `regex.c:531` 零下界分支把 `produced` 从 `1u` 改成 `0u`（不是旧日志写的「循环回退到 index=1」），变红的是 `test_regex.c:80 :93` 的 `{0,1}`/`{0,19}` 断言；`test_regex.c:28` 的正下界缺陷由 m27（`regex.c:562` 播种 `produced`）单独捕获（1/46）。
- **真实调用顺序是列 → 键索引 → 对象清单 → DDL 文本**（round-7 N-1 ② 订正）：`verify_table_columns`（`:2061`）→ `verify_key_index`（`:2065`）→ `verify_official_objects`（`:2071`）→ `verify_official_ddl`（`:2077`）。此前文档写「对象清单在键索引之前」是错的：原版 partial UNIQUE 在 `:2000` 就拒，m26 放行后由**后面**的对象清单再拒，所以单点全绿、联合点才红。
- **round-7 的 DDL 文本比对让 8 个结构单点失去独立可观测性**：`verify_official_ddl` 是兜底层次，关掉任一前置结构检查后，被改过的夹具仍会被 DDL 文本拒掉，测试照旧通过。因此 m17/m18/m20b/m21b/m22/m26/m29/m30 单点如实登记为未捕获，各自配一个 `_plus_ddl_off` 联合点位。**联合点只证明「关掉兜底后该分支的可观测行为」，不能单凭它断言该防线在全部防线开启时不可替代**（round-8 N-1 ③）。按夹具归属拆分红行（审计 round-8 §4.3 口径）：联合点里属于 m33 的 CHECK/FK/等长 DDL 夹具行**恒为 12**，其余才是目标夹具行——m17+ 20、m18+ 12、m20b+ 4、m21b+ 4、m22+ 4、**m26+ 0**、m29+ 4、m30+ 4。m26 联合点目标行为 0，说明两点联合下 partial 夹具仍被对象清单拒。
- **m26 的「未捕获」是遮蔽，不是死代码**（round-6 N-1 ③ 的 round-7 版本）：单点 m26 关掉 partial UNIQUE 的 fail-closed 后，`struct_partial.db` 仍被对象清单与 DDL 文本两道兜底拒掉。**m26b（m26+m28）的 4 条红行全是 ⑪ trigger 夹具（`:2475 :2476`）与 `:2250`×2，没有 `:2460 :2461`**，所以它不构成 partial 的可观测证据（round-8 N-1 ①）；m26+DDL-off 的 12 条也全由 m33 的 DDL 夹具贡献、目标行数为 0。真正把该分支变成可观测的是三点联合 m26+m28+m33：20 条中 4 条归 partial（`:2460 :2461` 与 `:2250`）、4 条归 trigger、12 条归 m33。结论只到「同时关掉两道兜底后该分支确实会独立产生断言失败」。
- **m26b / m34b 是补充点位，不算新的独立缺陷**：它们是为把「被遮蔽的防线」变成可观测而设计的联合形式，与 m26、m34 打在同一处代码。计数口径仍是 42 次运行 / 32 次变红 / 10 次未捕获，不因补充点把捕获数说高。验收时应逐夹具状态行对比 m28、m33、m26b、m26+DDL-off、m26c，不能只比总数（round-8 N-1 建议）。
- **`assertion_failures=` 与真实断言数差 1**：批处理解析的是测试二进制输出里所有以 `FAIL ` 开头的行，其中包括尾部那条 `FAIL storage.repository (N of M checks failed)` 汇总行。因此表中失败数 = 批处理 `assertion_failures` 减 1，且与逐条 FAIL 行完全对应。
---

## 8. 契约 schema 子集的再生成（**必须知道**）

`host/storage/src/schema_records.json` 是 `contracts/internal-data.schema.json` 的**机器抽取子集**（47 defs + 18 collections，已去 `description`、键排序、闭包完整）。抽取器已冻结进仓库：`host/storage/tools/extract_schema.py`。

```bash
# 只校验（CI/CTest 用）：不一致则打印 DRIFT 并 exit 1
python3 -B host/storage/tools/extract_schema.py --check

# 契约变了以后重生成（会改写 src/schema_records.json）
python3 -B host/storage/tools/extract_schema.py
```

抽取规则（脚本实现，不要手改 `schema_records.json`）：

1. 读 `contracts/internal-data.schema.json`，取 `x-collection-schemas`（collection → record def 名，18 条）与 `$defs`（107 个）。
2. 从 18 个 record def **加上 7 个仓储 DTO 根**（`RepoDocument`/`RepoCheck`/`RepoMutation`/`RepositoryChange`/`RepositoryQuery`/`RepositoryQueryResult`/`RepositoryRecoveryPage`）出发做 `$ref` 闭包，得到实际需要的 def 子集（当前 47 个）。少这 7 个根只会得到 40 个 def，闭包不完整。
3. 去掉每个 def 的 `description`（不参与校验，减小体积），`json.dumps(sort_keys=True, separators=(",",":"), ensure_ascii=False)` 单行写出 + 一个换行。**必须用二进制 I/O**：文本模式在 Windows 会把 LF 翻成 CRLF，冻结文件会变成 27526 B 并让门禁误报。
4. 重新构建：`embed_file.cmake` 会在构建期（`add_custom_command`）把它嵌成 `schema_json.inc`。
5. 跑 `storage.test_schema`（131 断言）与 CTest `storage.schema_source_drift` 确认抽取没丢关键字、也没漂移。

未知 `$defs` 名或未解析的 `$ref` 会让脚本直接 `SystemExit`（fail closed），不会静默生成残缺子集。

---

## 9. 证据索引（`docs/evidence/stage-01/S1-02/`，72 个文件）

| 文件 | 内容 |
|---|---|
| `summary.md` | 实施报告（本卡） |
| `handoff.md` | 本文件 |
| `audit.md` | 首轮独立审计（FAIL，B1–B5/N1–N5），开发 AI 不改它的结论 |
| `audit-round2.md` | 第二轮独立审计（FAIL，R2-B1/R2-B2/R2-B3 + R2-N1–R2-N4），开发 AI 不改它的结论 |
| `audit-round3.md` | 第三轮独立审计（FAIL，B-1/B-2 + N-1–N-3），开发 AI 不改它的结论；其附录 A 一段代码里出现 markdown 链接形状的括号序列，被 `check_contracts.py` 误判为本地链接，已改成 `fn = m["main"]` 形式（只消假链接，不改结论） |
| `audit-round4.md` | 第四轮独立审计（FAIL，B-1/B-2 + N-1–N-3），开发 AI 不改它的结论 |
| `audit-round5.md` | 第五轮独立审计（FAIL，B-1 partial UNIQUE 绕过 + N-1 归因/N-2 文档），开发 AI 不改它的结论；其 C 端复现全部 `NOT_RUN`（审计机无 WSL/ssh） |
| `audit-round6.md` | 第六轮独立审计（FAIL，B-1 未知触发器静默吞写 / B-2 隐藏生成列 + N-1 归因 / N-2 文档），开发 AI 不改它的结论；其 C 端复现全部 `NOT_RUN` |
| `audit-round7.md` | 第七轮独立审计（FAIL，B-1 表内 CHECK / FOREIGN KEY 绕过 schema 接纳 + N-1 mutation 归因/顺序/计数），开发 AI 不改它的结论；其 C 端复现全部 `NOT_RUN` |
| `audit-round8.md` | 第八轮独立审计（**PASS**，限定为本轮代码与证据复审：无阻塞项，4 项非阻塞 N-1～N-4），开发 AI 不改它的结论；其 C 端 configure/build/CTest/ASan/TSan/C mutation 全部 `NOT_RUN`（审计机无 WSL/ssh），Linux PASS 只按现存交付日志的历史身份引用 |
| `audit-round9.md` | 第九轮独立审计（**PASS**，用户 m12834 超轮规则下的联合确认轮：无阻塞项，2 项非阻塞 N-1/P3 旧口径残留、N-2/P3 暂存清零声明），26510 B / 315 行；同样声明 C 端全部 `NOT_RUN`，实际执行 Windows Python 四项设计检查、抽取器正负控、Node 逐段核算日志、Python AST 解析变异定义（42 名称 / 53 次文本编辑锚点全部 count=1）与内存 SQLite 探针 |
| `audit-round8-prompt.txt` / `audit-round8-runner.ps1` / `audit-round8-run.log` | 第八轮审计的**可复现输入**：提示词（20223 B）、调用脚本（`codex exec --skip-git-repo-check -s workspace-write`）、运行日志（START/EXIT/END） |
| `audit-round9-prompt.txt` / `audit-round9-runner.ps1` / `audit-round9-run.log` / `audit-round9-output.txt` | 第九轮审计的可复现输入与**原始输出**：提示词 23382 B（含超轮联合确认规则段）、调用脚本、运行日志（START 2026-10-24T21:47:54 / EXIT=0 / END 22:04:54）、原始输出 37992 B（UTF-16LE） |
| `source-manifest.txt` / `manifest.sha256` | 32 个源码文件的 sha256 + 字节数（`generated=2026-10-24T20:59:57+08:00`；26 手写 + 6 vendored；文件自身 sha256=`817ddb073ba2a35b3b69e83e5fc3d8f72429ad27d3f22e076a967edecfa6dd0a`，3563 B）；VM 侧 `sha256sum -c` 32/32 OK（`vm-s102-source-verify.log`） |
| `vm-s102-run-id.txt` | VM 批次 `RUN=20261024-213500`（与 `vm-s102-run-summary.log` 一致） |
| `mutation.log` | **42 次运行**（34 个具名点位 + 8 个 `_plus_ddl_off` 联合点位）的结果、失败断言行号、**32 次变红 / 10 次未捕获**（m6_sync / m9_page_budget / m17 / m18 / m20b / m21b / m22 / m26 / m29 / m30）的如实登记，以及 m23/m24 同锚点重复、`m15_object_type` 单点、`m29b` 与 m29 逐字节相同（重复执行）的移出说明、m26b/m34b 补充点位、真实调用顺序与 DDL 兜底遮蔽的归因；开头有 `SOURCE_FINGERPRINT` 绑定 11 个被测源文件（repository.c `b9ab2abaa41e` / 95832 B、test_repository.c `f1cc609cf4cf` / 138538 B 等） |
| `mutation-batch.py` | **正式保留的变异定义**（round-7 N-1 ④ 的整改：此前收尾时清理掉，导致无法重放）。42 个点位的 `POINTS` 表（每个点位 = 文件 + 原文串 + 替换串 + 锚点）。**脚本内保留原始绝对路径**（`SRC=/mnt/e/stm32项目`、`OUT=/mnt/c/Users/Public/s102_r9mut.log`、工作目录 `$HOME/s102_r9mut` 与 `$HOME/s102_r9build`），重跑前按需改前三个常量；任何人可照 `summary.md` §5.4 重跑并与 `mutation.log` 逐点位比对。**原始批处理输出 `mutation-batch.log` 已按用户 m19922 指令删除**（它是 WSL2 运行产物），`mutation.log` 是同一批的归一化记录 |
| `mutation-batch-parsed.json` / `mutation-batch-attribution.json` | 42 个点位的逐行解析（anchor / edit / patch / af / FAIL 行号）与 helper/m33-DDL/目标夹具的拆分归因，`summary.md` §5.4 与 `mutation.log` 头部 joint 分解式的数据来源 |
| `mutation-joint-m26c.py` | 三点联合变异（m26 + m28 + m33）的定义，用于证明 partial UNIQUE 的 fail-closed 分支在两道兜底之下仍需三点联合才可观测（20/865 = 4 partial + 4 trigger + 12 m33 DDL）。原输出 `mutation-joint-m26c.log` 已按用户 m19922 指令删除（WSL2 运行产物），结论与分解式已并入 `mutation.log` 附录段与 `summary.md` §5.4 |
| `evidence-run-vm.sh` / `evidence-make-snapshot.sh` / `evidence-make-manifest.sh` / `evidence-design-checks.sh` / `evidence-copy-design-checks.sh` | 本轮取证脚本（VM canonical、快照打包、清单生成、设计检查、设计检查回填），删暂存副本前已逐字节归档，可直接照 `summary.md` §5.1 重跑。原 `evidence-run-wsl.sh` 已按用户 m19922 指令删除 |
| `check_contracts.json` / `check_stage_plan.json` / `check_vision_contracts.json` / `check_feasibility.json`（及同名 `.log`） | 四项设计检查的结构化输出与文本输出（exit_code 全 0） |
| `vm-s102-environment.log` | VM 环境（Ubuntu 26.04.1 / 7.3.0-rc5 / ext4 / cmake 4.2.3 / gcc 15.2.0 / make 4.4.1 / ctest 4.2.3 / Python 3.14.4） |
| `vm-s102-source-verify.log` / `vm-s102-manifest.sha256` | 快照指纹校验 `SHACHECK_EXIT=0` / `SHACHECK_OK=32` / `SHACHECK_FAILED=0`、`SRC_STRAY_ARTIFACTS=0`、`SRC_FILES=1016`、清单原文 |
| `vm-s102-run-summary.log` / `vm-s102-run-id.txt` | VM 运行脚本的全部退出码与关键输出（run_id **20261024-213500**，round-8 整改后的最终快照，快照 sha256=`f4279ba59ad7b013bdc9811a8e3f6eafc093e214ea3ea92a79ad92784b47c69f`，81796615 B，1113 个 tar 条目） |
| `vm-s102-configure.log` / `vm-s102-build.log` / `vm-s102-ctest.log` / `vm-s102-unit-tests.log` | VM 内配置/构建/CTest/逐测试（12/12、**865 断言**的 repository、合计 1717） |
| `vm-s102-design-checks.log` | VM 内四项设计检查 |
| `vm-s102-asan-{configure,build,ctest}.log` | VM 内 ASan+UBSan |
| `vm-s102-tsan-{configure,build,ctest}.log` | VM 内 TSan |

**两层证据不可互相替代**：VM 日志（构建/CTest/sanitizer/设计检查的原始运行记录）与 mutation 日志（变异批次的归一化记录）各自独立；不把任何结果改名冒充另一种环境。按用户 m19922 指令，WSL2 一层的 18 个产物已全部删除，本卡 Linux 证据只有 VMware 干净 guest。

---

## 10. 未实现项 / 未验证项

| 项 | 状态 | 说明 |
|---|---|---|
| RK 存储介质掉电可靠性 | **NOT_RUN** | 无 RK 板、无断电测试设备。本卡只验证到 SQLite 语义层（WAL + `synchronous=FULL`）与「正常 close/reopen 后数据仍在」。**不声称已验证掉电持久性** |
| `synchronous=FULL` 的运行时断言 | **未覆盖** | 连接级 pragma，另一连接不可观测；m6 变异未被测试发现（如实登记，未伪造断言） |
| `journal_mode=WAL` 返回值逐字比对 | **未做** | `exec_sql` 只看返回码；WAL 是否真生效由 `test_wal_enabled` 从另一连接验证 |
| 业务闭环（借还全流程） | **不属于本卡** | 属 S1-04 及以后；本卡只提供仓储原语 |
| HTTP/UI 调用仓储 | **禁止** | 本卡未提供任何入口；调用链见 `docs/architecture/api-links.md:63-70` |
| 设备动作发送 | **禁止** | 仓储无 socket/serial 依赖 |
| 32 位 / ARM 交叉编译 | **NOT_RUN** | 只在 x86_64 Linux 验证 |
| CMake 3.16 最低版本 | **NOT_RUN** | 实测用 4.2.3；`cmake_minimum_required(VERSION 3.16)` 未在 3.16 上验证 |
| `deployment/host/dependencies.lock.json` 更新 | **已完成（审计 N4）** | 新增 `vendored`/`maintained_by` 段，`cjson.consumer_cards` 改 `["S1-02","S1-05"]`，`unresolved` 四条改写；CivetWeb/libsodium/TLS 仍留给 S1-05 |
| `host/storage/README.md` | **已建** | 125 行 / 7850 B，含构建接入、线程/生命周期、schema 再生成、已知边界 |
| `host/common/README.md` | **未建** | 属 S1-01 的基础设施模块；本卡未越权补建，模块说明在 S1-01 证据里 |
| 契约 C 声明语法检查（`c_declaration_syntax`） | **NOT_RUN** | 需要可选依赖 pycparser，两个环境的 Python 3.14 都未安装；两处都输出 `NOT_RUN (optional pycparser unavailable)`，status 仍 PASS。未安装依赖来「凑」这条输出 |

---

## 11. 本轮临时残留（交付前清理）

### 11.1 转存为正式证据后删除的暂存物（本轮，2026-10-24T22:10+08:00 实测）

按 round-9 N-2 的要求，这里只写**有批次与时间的事实**，不再使用「PUBLIC_LEFT=0」这类无法被外部复核的全局断言；每一组都给出清理前的实测条目数、字节数与去向。

- **先转存、后删除**：把下列 10 个文件从暂存位置复制进 `docs/evidence/stage-01/S1-02/`，逐个核对字节数后才删暂存副本——`audit-round8-prompt.txt` / `audit-round8-runner.ps1` / `audit-round8-run.log`、`audit-round9-prompt.txt` / `audit-round9-runner.ps1` / `audit-round9-run.log` / `audit-round9-output.txt`（审计可复现输入与原始输出）、`evidence-run-wsl.sh` / `evidence-run-vm.sh` / `evidence-make-snapshot.sh` / `evidence-make-manifest.sh` / `evidence-design-checks.sh` / `evidence-copy-design-checks.sh`（取证脚本）、`mutation-batch-parsed.json` / `mutation-batch-attribution.json`（42 点位的逐行解析与 helper/DDL/目标夹具拆分，正文表格即由此生成）。
- **`C:/Users/Public`**：清理前实测 37 个 `s102_*` / `_r9*` / `check_*` 条目（含 `s102_snapshot_r9.tar.gz` 81,796,615 B、`s102_vm_evidence_r9.tgz` 5,376 B、4 个暂存目录 `s102_r9ev` / `s102_r9ev2` / `s102_vm_ev_r9` / `s102_dc2`、变异脚本与日志、审计提示词与输出、清单辅助脚本），共 82,152,050 B；删除后同一过滤条件计数为 0。删除前逐目录比对：`s102_vm_ev_r9` 16/16 与 `vm-s102-*` 逐字节相同、`s102_dc2` 5/5 相同、`s102_r9ev2` 17/18 相同（唯一差异是 `design-checks.log`，暂存版时间戳 21:32:43、证据版 21:48:08，保留后者）、`s102_r9mut.py/.log` 与 `mutation-batch.py/.log` 逐字节相同、`s102_r9_m26c.py/.log` 与 `mutation-joint-m26c.py/.log` 逐字节相同。
- **系统 TEMP（`%LOCALAPPDATA%\Temp`）**：清理前实测 10 个相关条目并全部删除——`s102dc`（4 文件，含已归档的 check_*.log）、`s102vm`（13）、`s102vmev`（16，与 `vm-s102-*` 同源）、`s102_r3_in.txt` / `s102_r3_out.log` / `s102_r4_in.txt` / `s102_r4_out.log`（round-3/4 审计提示词与输出，其结论已在 `audit-round3.md` / `audit-round4.md` 固化）、`vm125905` / `vm135713`（round-4/5 的 18 文件 VM 日志暂存）、`vm135713_run.out`；清理后同一过滤条件只剩 1 个与本卡无关的 `workbuddy-product-spill-*`。注意 `vm125905`/`vm135713` 与当前 `vm-s102-*` 逐字节不同（属更早批次的 18 文件版），故它们是**历史批次暂存**而不是当前证据的副本，删除不损失当前证据。
- **WSL2 `$HOME`**：清理前 8 个 `s102_*`（`s102_r9ev`、`s102_r9ev2`、`s102_r9mut`、`s102_r9build`、`s102_r9m26c`、`s102_r9m26cbuild` 等构建与变异工作目录），删除后计数 0；`/var/tmp` 下 `s102*` / `s101-*` 计数 0（`VARTMP_BEFORE=0`，此前批次已清）。
- **VM（`ssh locker-vm`）**：`/tmp` 下 6 个 `s102*`（`s102_snapshot_r9.tar.gz`、`s102_vm_evidence_r9.tgz`、`s102_vm_fix.sh`、`s102_vm_r9.sh`、`s102_vm_r9_run.sh`、`s102_vmcln.sh`）删除后计数 0；`$HOME` 下 `s102_*` 计数 0；卡源码目录 `SRC_STRAY_ARTIFACTS=0`（无 `.o/.a/CMakeCache.txt`）、`SRC_FILES=1016`。
- **仓库根**：`.tmp_*` / `s102_*` / `_mut*` / `_r8*` / `_new_mutation*` 计数 0；`build2.log` / `mut_build.log` / `mut_test.log`（早期在仓库根直接跑 make 的失败日志）已在 round-7 收尾删除，非证据。
- **收尾最后一轮清理（2026-10-24T22:22+08:00，改完入口文档与 `stage-plan.json` 之后）**：`C:/Users/Public` 剩 6 个本卡相关条目（`s102_dc3/`、`s102_dc4/` 两个设计检查暂存目录 + 4 个辅助 `.sh`，共 2,024 B）全部删除，该目录只剩系统自带 11 项（`.MUMUVMM`、`AccountPictures`、`Desktop`、`Documents`、`Downloads`、`Libraries`、`Music`、`Pictures`、`Videos`、`Win`、`desktop.ini`）；系统 TEMP 按 `s102|vm1xxxxx|vm_ev|vmlogs` 过滤计数 0；WSL `$HOME/s102_*` 0、`/var/tmp/s102*` 0；VM `/tmp/s102*` 0、`$HOME/s102_*` 0。删除前 `s102_dc4/` 的四个 JSON 与 `design-checks.log` 已复制进证据目录（头部时间戳 `2026-10-24T22:25:39+08:00`，四项 EXIT=0、`active_task null`、`implementation_cards_done 2`）。

### 11.2 有意保留项（不是遗漏）

- **证据目录本体**（`docs/evidence/stage-01/S1-02/`，72 个文件）：正式证据、历轮审计报告、审计提示词/运行脚本/原始输出、变异定义与完整日志、双环境日志，全部保留。
- **VM 内 `~/locker-workspace`**：卡源码 `S1-02/src`（1016 文件）、`build/S1-02/20261024-213500`（含 `-asan`/`-tsan`）、`logs/S1-02/20261024-213500`、`artifacts/S1-02` 按工作区规则保留；`build/S1-02` 与 `logs/S1-02` 下另有历史批次 `20261024-{014545,055243,063018,114250,125905,135713,152903,174731,174805,174830,193500}`，均为各轮取证产物（非本卡源码）。顶层 `~/locker-workspace/src` 不含 `host/storage`，不要当成卡源码目录。
- **仓库内既有构建残留**：`.o/.a/.so/.exe/.obj/.pyc` 全部位于 `.gitignore` 覆盖的 `build/host/`、`build/keil-env/`、`build/firmware-env/`、`STM32F4xx_HAL_Code/USER/project_demo/keil_project/Objects/`，是此前 firmware/env 与 S1-01 的产物，**不是本卡产物**；本卡所有构建都在源码树之外（WSL `$HOME/s102_r9ev2`、VM `~/locker-workspace/build/S1-02/20261024-213500`）完成。


## 12. 审计整改对照（B1–B5 / N1–N5）

首轮审计结论 FAIL，整改落点如下（详细回归见 `summary.md` §5.6）：

| 编号 | 整改落点 |
|---|---|
| B1 游标连续性 | `event_keys_adjacent`（`repository.c:1397`）同 boot 严格 +1、换 boot 要求 `boot` 严格增大（**允许跳号**）且 seq==1；`cursor_is_contiguous`（`:1417`）复用同一判据；`stored` 分支加「水位不倒退」；`gap_before_page` 被消费并锁存 `continuity_ok=false`（`:1697`） |
| B2 批次边界 | 预扫后新增边界块（`repository.c:1507-1571`）：`last_committed` 非零、同 device+epoch、`last_key <= last_committed`、`first_available` 同日志且 `<= first_key`、`next_after == last_key` |
| B3 初始化 | `locker_repo_open`（`:2063`，判定块 `:2120`–`:2140`）用 `db_table_exists(documents)`（`:2124`）+ `db_object_exists`（`:248`）+ `db_user_object_count`（`:279`，`NOT GLOB 'sqlite_*'`）区分「真空库 / 同名非表对象 / 有痕迹库」；首次 DDL+元数据包进 `BEGIN IMMEDIATE`（`:2146`）并在 `COMMIT` 前 `verify_schema_tables`（`:2004`），失败 `ROLLBACK`；已初始化同样先 `verify_schema_tables` 再 `verify_schema_metadata`（`:1746`） |
| B4 分页预算 | `page_documents_budget`（`:954`）用探针信封 + 续页键预留（`KEY_MAX*6+8 = 1160 B`）算真实预算；`collect_page`（`:997`）按每条文档 `cJSON_PrintUnformatted` 长度累加，装不下就返回非空页 + 续页键 |
| B5 嵌入 NUL | `locker_storage_json_text_has_embedded_nul`（`schema.c:35`）拒原始 0x00 与奇偶正确的 `\u0000`；`validate_json`/`validate_document_json` 与 `locker_storage_parse_json`（`repository.c:478`，守卫在 `:483`）入口都先过守卫 |
| N1 | 本文件 §3.5 已改为「没有投影时传 `NULL`」并给可编译示例，明确 `OK ≠ 可 ACK` |
| N2 | 「进程崩溃后重开」已改为「正常 close/reopen」（唯一 fork 场景是竞争测试） |
| N3 | `test_failed_transaction_is_atomic` 改用 SQLite trigger 制造「第一条有效写已发生、第二条 SQL 失败」 |
| N4 | `deployment/host/{dependencies.lock.json,README.md}` 更新 + 新建 `host/storage/README.md`（125 行 / 7850 B） |
| N5 | `u64.c` 改薄适配 `locker_common` numeric；`schema.c` 的 U64 校验走 `locker_u64_parse_decimal`；新增 `tools/extract_schema.py` + CTest `storage.schema_source_drift` |

第二轮（`audit-round2.md`，R2-B1/R2-B2/R2-B3 + R2-N1–R2-N4）整改落点见 `summary.md` §5.7：全重复页也锁存缺口（`:1634` 早退拆三路）、事件键结构校验 `event_key_structure_ok`（`:1384`，半零一律 INVALID）、`first_available` 非空页零值拒绝（`:1554`）、`db_object_exists`/`db_user_object_count` + 提交前 `verify_schema_tables`、B5 空洞负例补齐、m4 换锚点并新增 `test_compare_text_noncanonical`。

第三轮（`audit-round3.md`，B-1/B-2 + N-1–N-3）整改落点见 `summary.md` §5.8：

| 编号 | 整改落点 |
|---|---|
| B-1 对象计数 LIKE 下划线 | `db_user_object_count`（`repository.c:279`）的 SQL 改为 `WHERE name NOT GLOB 'sqlite_*'`（`:291`）。`LIKE` 里 `_` 是单字符通配符，会把用户建的 `sqlitex_audit` 误判为内部对象 → 有痕迹库被当空库补建。回归：`test_schema_structure_verified` 的 `struct_*` 夹具 + `test_documents_view_rejected` |
| B-2 表结构只查表名 | `verify_schema_tables`（`:2036`，循环 `:2059`–`:2068`）升级为逐表核验：`verify_table_columns`（`:1849`，`pragma_table_xinfo` 核列名/类型/NOT NULL/主键位置并拒绝 `hidden != 0`，round-6 B-2）+ `verify_key_index`（`:1973`）/`index_covers_key`（`:1919`，`pragma_index_list` + `pragma_index_xinfo` 核唯一、升序、`BINARY` 排序；round-5 起 partial UNIQUE 一律 fail-closed）；末尾 `verify_official_objects`（`:2079`）要求 `sqlite_*` 之外恰好三张表、无视图/索引/触发器（round-6 B-1）。初始化分支与已初始化分支都调用 |
| N-1 m15 变异是语法错误 | `m15_object_type` 单点如实登记为**未捕获**（`db_object_exists` 退回只查 table 时 `db_user_object_count` 仍数出视图，有痕迹守卫仍拒 → 冗余防线）；新增 `m15b_view_double` 三点联合变异（`:256` + `:2228` + `:2233`）捕获 10 条（当时分母 829；round-7 后同一变异为 4/865，红行随夹具增加而变化），证明视图路径与有痕迹保护确有回归守住 |
| N-2 NUL 属性名负例空洞 | `test_embedded_nul_rejected` 的属性名负例改为 `name` + NUL + `junk`（截断后恰好是合法字段名，唯一拒绝理由只能是 NUL），并补原始 0x00 属性名变体；每类负例单独建库/恢复基线，避免前一条 CONFLICT 连带影响后一条 |
| N-3 文档与数字 | 本文件 §3.5 补「`first_available` 是日志最早可用位置、`last_committed` 是已提交尾、只有 `next_after` 等于本页末条」的包络语义 + `test_import_boundary_envelope` 成功对照；`summary.md` B3 行重写、ctest 0.21s→0.25s；`README.md` 的 `active_task` 与 stage-plan 对齐为 `S1-02` |

第四轮（`audit-round4.md`，B-1/B-2 + N-1–N-3）整改落点见 `summary.md` §5.9：

| 编号 | 整改落点 |
|---|---|
| B-1 反向排序组合 | `verify_key_index`（`repository.c:1973`）不再「任一匹配索引即 OK」：遍历 `pragma_index_list` 里 unique 且非 partial 的索引，**每一个**都必须被 `index_covers_key` 认可，否则 `CONFLICT`（`:2010`）；末尾 `matched_unique == 0` 也 `CONFLICT`（`:2023`）。这样「列默认 BINARY + 主键显式 COLLATE NOCASE + 额外 BINARY 唯一索引」与「额外挂在别的列上的唯一索引」都被拒。回归：`struct_revcoll.db`、`struct_extrauniq.db` 夹具（`test_schema_structure_verified`），定向变异 m20b 与 m22 单点在 round-7 后均未捕获（被 `verify_official_ddl` 兜底遮蔽），各自叠加关掉 DDL 比对的联合点位后捕获 16/865；round-7 又补三个内联约束夹具（`SQL_INLINE_EXTRA_UNIQUE`/`SQL_INLINE_PK_NOCASE`/`SQL_INLINE_PK_DESC`）让这些分支在 round-6 对象清单检查之下仍可独立观测 |
| B-2 源码变动后证据脱节 | 全部源码/测试改动落定后重出 `source-manifest.txt`（32 文件，sha256=`817ddb07…dd0a`），重跑 WSL2 与 VM 的 configure/build/CTest/ASan/TSan（VM run_id=`20261024-213500`），42 次运行变异重跑并把 11 个源文件指纹写进 `mutation.log` 头部 |
| N-1 变异归因与明细 | `m15_object_type` 单点移出批次（残留目录单独重跑 0 失败，与 round-3 N-1 一致）；m17/m18 在 round-7 后单点登记为未捕获（DDL 文本兜底遮蔽），各叠加 `_plus_ddl_off` 后捕获 32/865 与 24/865（新增生成列/内联夹具使 m17 的联合红行由 20 增至 32），并保留 m20b/m21b/m22 逐点位明细；m23/m24 与 m17/m18 同锚点，明确不计入独立覆盖 |
| N-2 归属与残留表述 | `last_committed` 全文改为「MCU 已经持久提交的日志尾，不是 RK 本端落库水位（RK 水位是 `committed_cursor`/`through`）」；§11 残留表述统一为「已清理并复核 0」，构建路径 `/var/tmp` 更正为 `$HOME/s102_*`；`summary.md` 三处勘误（`struct_bare` 是只有元数据表、`m4` 锚点未换、属性名是 `name`+NUL+`junk`） |
| N-3 私有 regex 有界重复 | `regex.c:562` 的有界重复先按 `min_value` 播种 `produced`，再 `for (index = produced; index < max_value; ++index)` 追加可选片段，修掉 `{0,n}` 上界多生成一个 atom 的问题；新增 `test_regex.c` 的 `test_bounded_repeat`（`^a{0,1}$` 必须拒绝 "aa"、`^a{0,0}$` 必须拒绝 "a"、契约里 U64 用的 `^[0-9]{0,19}$` 必须拒绝 20 位）与 `test_compare_text_noncanonical` 的配套断言 |

第五轮（`audit-round5.md`，B-1 + N-1/N-2）整改落点见 `summary.md` §5.10：

| 编号 | 整改落点 |
|---|---|
| B-1 partial UNIQUE 被跳过 | `verify_key_index`（`repository.c:1973`）原先只跳过**非 unique** 索引，partial UNIQUE 被静默忽略：只要加一条 `CREATE UNIQUE INDEX ... WHERE collection='audits'`，结构检查就通过，但两条同属 `audits` 集合的 `AuditRecord` 写入会被这条不兼容唯一约束拒掉。改为 partial UNIQUE 一律 `CONFLICT`（`:2000`–`:2003`，`is_partial != 0` 直接 fail-closed）。回归：夹具 `SQL_PARTIAL_UNIQUE_INDEX`（`test_repository.c:2318`，`partial_collection ON documents(collection) WHERE collection='audits'`），`struct_partial.db` 在 `open(false)`/`open(true)` 两种 `initialize_empty` 下都断言 `CONFLICT` + `repo == NULL`（`:2426`/`:2427`），且 `fixture_object_count == 4`（`:2432`）；`struct_ok` 正对照仍在 `:2380`/`:2385`。定向变异 m26 单点被 round-6 对象清单与 round-7 DDL 文本两道兜底遮蔽（0 失败）；与 m28 联合的 m26b 捕获 4/865，三点联合 m26+m28+m33 捕获 20/865 |
| N-1 归因与口径 | m15b 的 4 条红行改为「证明返回分类与有痕迹保护」，**不再**声称证明「首次 open 假成功」——首次视图由 `verify_schema_tables`（`:2249` 调用点）静态拒绝，`test_documents_view_rejected` 的 `open(true)` 在 `:2122` 无 FAIL、唯一视图 FAIL 是 `:2125` 的 `open(false)`（CONFLICT 变 NOT_READY）；m17/m18 按夹具拆分登记；捕获计数只给三档口径（42 次运行 / 32 次变红 / 10 次未捕获），删除「按独立缺陷 24 点位 22 捕获」的折算；m25 描述重写（零下界 `produced` 种子 `1u`→`0u`，`regex.c:531`），并新增 m27（`regex.c:562` 的 `min_value` 播种） |
| N-2 文档与数字 | 修复本文件原被正文拼接截断的 N-3 行；`summary.md` 合计断言 1583→1609→1681→**1717**（round-8 整改后 common 505 + storage 1212），并订正「1583 应为 1601」的过期说法；`host/storage/README.md` 构建路径改为 `$HOME/locker-workspace/build/S1-02/manual`；`summary.md` 里 `bare.db` 与 `struct_bare` 两处混称已分开 |
第六轮（`audit-round6.md`，B-1/B-2 + N-1/N-2）整改落点见 `summary.md` §5.11：

| 编号 | 整改落点 |
|---|---|
| B-1 未知触发器 | 新增 `verify_official_objects`（`repository.c:2086`，SQL `:2096`）：`sqlite_*` 之外每行必须 `type == 'table'`（`:2113`）、表名 ∈ `{meta,migrations,documents}`（`:2131`）、不重名（`:2121`）、恰好 3 个对象（`:2137`），否则 `CONFLICT`；由 `verify_schema_tables` 末尾（`:2071`）调用，首次初始化 `COMMIT` 前（`:2409`）与已初始化分支（`:2421`）都覆盖。第二道防线：`db_put_document` 在 `step_write` 后 `sqlite3_changes(db) != 1 → IO_ERROR`（`:438`），`db_delete_document` 仅在 `existed && changes != 1` 时 IO_ERROR（`:477`）。回归：夹具 `SQL_TRIGGER_IGNORE`（`test_repository.c:2328`）+ `struct_trigger.db` 双模式 CONFLICT（`:2475`/`:2476`）+ 对象数 4（`:2483`）；新用例 `test_silently_ignored_write_detected`（`:2604`）。变异 m28 4/865、m31 4/865、m32 3/865 |
| B-2 隐藏生成列 | `verify_table_columns`（`:1849`）的列查询改为 `SELECT name, type, [notnull], pk, hidden FROM pragma_table_xinfo(?1) ORDER BY cid`（`:1856`），读 `hidden`（`:1876`）并在列名/类型比较前 `hidden != 0 → CONFLICT`（`:1881`）。回归：夹具 `SQL_GENERATED_COLUMN`（`:2337`，`table_info` 只报 2 列）与 `SQL_GENERATED_APPENDED`（`:2346`，审计原始反例，`table_info` 仍报 3 列），双模式 CONFLICT（`:2490`/`:2491` 与 `:2504`–`:2507`）且对象数 3（`:2498`/`:2513`）。变异 m29、m30 单点在 round-7 后未捕获（DDL 文本兜底遮蔽），各叠加 `_plus_ddl_off` 后 16/865；`m29b` 与 m29 的 edit 逐字节相同，已按 round-7 N-1 删除 |
| N-1 归因 | `mutation.log` 重写为具名段 + 11 行 `SOURCE_FINGERPRINT`，统一 m15b / m10 / m11 / m25 / m26 口径，删除「独立缺陷数」折算，只留「运行次数 / 变红次数 / 未捕获数」三档（当前 42 / 32 / 10），并把变异定义与完整诊断输出落进 `mutation.log` 与 `summary.md` §5.4 |
| N-2 文档 | VM 路径统一为 `~/locker-workspace/S1-02/src` + `build/S1-02/20261024-213500` + `logs/S1-02/20261024-213500` + `artifacts/S1-02`（顶层 `~/locker-workspace/src` 不含 `host/storage`，不是卡源码目录）；partial UNIQUE 影响描述改为「两条 `AuditRecord` 冲突（collection 同为 `audits`）」；本文件 §11 与 §9 的残留/证据计数同步为 56 个文件 |

第七轮（`audit-round7.md`，B-1 + N-1）整改落点见 `summary.md` §5.12：

| 编号 | 整改落点 |
|---|---|
| B-1 表内 CHECK / FOREIGN KEY 绕过接纳 | 新增 `verify_official_ddl`（`repository.c:2244`）：对 `meta`/`migrations`/`documents` 逐表 `SELECT sql FROM sqlite_master WHERE type='table' AND name=?1`（SQL `:2254`），与唯一 `schema-v1.sql` 提取出的期望文本**逐字节**比对（`expected_table_ddl` `:2157`，提取器 `ddl_identifier_char` `:2147`），NULL / 长度不等 / `memcmp` 不等 / 无行 / 多行全部 `CONFLICT`（判定 `:2280`–`:2287`）。由 `verify_schema_tables` 末尾（`:2077`）调用，首次初始化 `COMMIT` 前与已初始化分支都覆盖。之所以不能靠对象清单/列/索引检查：表内 `CHECK`、`FOREIGN KEY` 既不产生用户对象，也不改变 `table_xinfo` 与 `index_xinfo` 的任何字段。回归夹具 `SQL_CHECK_CONSTRAINT`（`test_repository.c:2381`）、`SQL_FOREIGN_KEY`（`:2390`）、`SQL_DDL_SAME_LEN_DIFFERENT_TEXT`（`:2400`，全小写等价定义，存储长度与官方同为 164 B，只有逐字节比较能拒），双模式 `CONFLICT`（`:2542`/`:2543`、`:2554`/`:2555`、`:2567`/`:2568`）且对象数 3（`:2550`/`:2561`/`:2575`）；正对照 `test_official_ddl_positive_control`（`:2725`）用官方 DDL 提交同一份两条 `audits` 的 change 并断言 `revision=1`、`documents_count==2`，证明没有过度收紧。定向变异 m33（关掉调用，`:2077`→`OK`）12/865、m34（只比长度不比内容，`:2281`）4/865、m34b（去掉内容比较整段）12/865 |
| N-1 变异归因/顺序/计数 | ①`m29b_xinfo_to_info_only` 与 `m29_table_info` 的 edit 逐字节相同，按**重复执行**处理并删除，不再当作第二个缺陷；②真实调用顺序订正为 `verify_table_columns`（`:2061`）→ `verify_key_index`（`:2065`）→ `verify_official_objects`（`:2071`）→ `verify_official_ddl`（`:2077`），推翻「对象清单在键索引之前」的旧说法，m26 的「单点绿、联合红」改由「后面的兜底层次拒掉」解释；③`assertion_failures` 含尾部 `FAIL storage.repository (N of M checks failed)` 汇总行，真实断言数 = 记录值 − 1，全表按减 1 口径改写（m28 4、m29 4、m30 4、m31 4、m32 3）；④DELETE 侧失败后 `revision` 仍为 1、文档仍为 1（只有 PUT 侧失败时文档为 0）；⑤m26b（m26+m28 两点联合）的 4 条失败**全部来自 ⑪ trigger 夹具**（`test_repository.c:2475`/`:2476`，另两条是 `:2250` 的 helper 行），**不含** `:2460`/`:2461` 的 partial 夹具，因此不能当 partial 覆盖证据；partial 分支的可观测性由三点联合 m26+m28+m33（20/865，其中 4 条 partial + 4 条 trigger + 12 条 m33 DDL 夹具）证明 |

### 第八轮（`audit-round8.md`，PASS + N-1～N-4）整改落点

| 编号 | 问题 | 整改落点 | 验证 |
|---|---|---|---|
| **N-1**/P2 | mutation 归因：联合点被当独立缺陷数、m26b 的 4 条红行被算作 partial 证据、`assertion_failures` 与真实断言差 1 未说明 | `mutation.log` 头部 joint 段改为完整分解式（`m17+ 32 = 16 helper + 6 m33 DDL + 10 目标夹具` … `m26+ 12 = 6 + 6 + 0`）；`summary.md` §5.4 八个联合点逐行标注拆分；m26b 行明确「4 条全部来自 ⑪ trigger 夹具、不含 `:2460 :2461`」 | 42 次运行 / 32 次变红 / 10 次未捕获，不折算独立缺陷数；`mutation-batch.log` 与 `mutation.log` 逐段对应 |
| **N-2**/P2 | 断言合计算术错误（1715 vs 文档 1707） | 断言表与四处引用统一为 **1717**（common 505 + storage 1212） | `unit-tests.log` 与 `vm-s102-unit-tests.log` 逐项一致：97+5+53+164+133+53=505；80+46+865+131+90=1212 |
| **N-3**/P2 | `test_official_ddl_positive_control` 的 key 不命中 `CHECK(key <> UUID_KEY)`，正对照保真度不足 | 新增 `build_audit_record_with_id`；正对照改用 `k_first_id` 与 `UUID_KEY` 并加两条 `TEST_CHECK(strstr(...) != NULL)` | 断言 1707 → 1717；`test_repository.c` 2965 → 2983 行 / 137410 → 138538 B |
| **N-4**/P3 | 「配置期」表述与构建期 `add_custom_command` 不符；README 的 VM 源码入口写错 | 六处源码/文档注释统一为「构建期」；README 的 VM canonical 段改为卡源码 `$HOME/locker-workspace/S1-02/src` + `build/S1-02/<run>` + `logs/S1-02/<run>` + `artifacts/S1-02` | 改后指纹：`host/storage/CMakeLists.txt` 3832 B、README 7850 B、`embedded.h` 922 B、`schema.h` 2197 B、`schema-v1.sql` 2350 B、`schema_embedded.c` 809 B |


### 第九轮（`audit-round9.md`，PASS + N-1/P3 + N-2/P3）整改落点

第九轮是用户 m12834 超轮规则生效后的联合确认轮，结论 **PASS**：未发现尚未修复的阻塞项 B-x，仅 2 项非阻塞收尾问题；同样声明 C 端 configure/build/CTest、C 测试二进制、ASan+UBSan、TSan、C mutation 复跑全部 `NOT_RUN`。

| 编号 | 审计指出的问题 | 整改落点 |
|---|---|---|
| **N-1**/P3 | 主要证据正确，但交接与部分说明残留旧口径，给出 6 处具体矛盾 | ①§7.3 做法句改为「FAIL 行从同一次 `ctest --output-on-failure` 的 stdout 抽取（`mutation-batch.py:162-177`），没有第二次单独执行测试二进制」；②§7.3 与 §9 的 `test_repository.c` 指纹由旧的 `6ab9a68d1c99`（137410 B 的 round-7 输入）改为 `f1cc609cf4cf` / 138538 B；③§9 `mutation-batch.py` 行的保留常量改为实际值 `s102_r9mut` / `s102_r9build` / `s102_r9mut.log`；④§12 第五轮 N-1 第⑤点改为「m26b 的 4 条失败全部来自 ⑪ trigger 夹具（`:2475`/`:2476`，另两条是 `:2250` helper 行），不含 `:2460`/`:2461`，不能当 partial 覆盖证据」；⑤§5.4 的 m34b 行改为「该 edit 把长度比较与内容比较一起去掉，只剩 `stored == NULL`，是比 m34 更强的形式，不能描述为保留长度分支」；⑥构建路径改为当前主体 `$HOME/s102_r9ev2` 与 VM run `20261024-213500`，并明确 `s102_r7ev` / run `193500` 属 round-7 历史批次 |
| **N-2**/P3 | 称 Public/TEMP 临时项已删并复核 0，但只读枚举仍见 Public 34 个 `s102_` 条目、TEMP 6 个相关条目 | §11 整段重写为「11.1 转存为正式证据后删除的暂存物（本轮实测）」+「11.2 有意保留项」，给出每组清理前的实测条目数与字节数：Public 37 条 82,152,050 B → 0；TEMP 10 条 → 0（仅剩 1 个与本卡无关的 `workbuddy-product-spill-*`）；WSL `$HOME` 8 → 0；VM `/tmp` 6 → 0；仓库根 `.tmp_*`/`s102_*`/`_mut*` = 0。**先转存后删除**：审计提示词/运行脚本/原始输出 10 个、取证脚本 6 个、变异解析 JSON 2 个已复制进证据目录并逐字节核对（证据目录 56 → 72 个文件）。`summary.md` §8 同步删掉 `PUBLIC_LEFT=0` 类全局断言，改为有批次/时间的事实 |


## 13. 下一步（不是本卡）

- 本卡**已 DONE**：九轮独立审计中第八轮（`audit-round8.md`）与第九轮（`audit-round9.md`，超轮联合确认轮）均 PASS；第九轮 2 项非阻塞 N-1/P3（旧口径残留 6 处）与 N-2/P3（暂存清零声明与现存条目不符）已在收尾时逐条订正——前者按审计给出的「应依据的当前事实」改了 §7.3 / §9 / §12 的六处正文，后者把 §11 整段重写为「转存后删除」的实测事实（Public 37 条 82,152,050 B → 0、TEMP 10 条 → 0、WSL `$HOME` 8 → 0、VM `/tmp` 6 → 0），并同步改掉 `summary.md` §8 里的 `PUBLIC_LEFT=0` 类断言。
- 下一张计划卡 S1-03（UART 字节端口与独立模拟设备），按用户 m03821/m03823 的全自动循环由下一轮开发 AI 起卡；本卡不代做、不预置 S1-03 的骨架。
- 阶段 1 门禁卡是 S1-07；本卡 DONE 不使阶段 1 PASS（`implementation_status` 仍 `IN_PROGRESS`、`gate_result` 仍 `NOT_RUN`）。
- 收尾时同步了当前入口（本卡收尾负责人执行）：`README.md` 的「当前发给开发AI」改指 [S1-03 开发指令](../../../ai-prompts/S1-03.md)、当前状态列 S1-01/S1-02 DONE、`active_task=null` / `recommended_next=S1-03`、阶段 1 任务卡行；`AGENTS.md` :3/:14；`docs/stages/README.md` :3/:47；`docs/stages/stage-01-task-cards.md` :3；`docs/AI开发交接流程.md` :3；`docs/AI开发执行手册.md` :70。改完状态后在 WSL2 重跑四项设计检查（`2026-10-24T22:25:39+08:00`）四项仍 exit 0，`check_stage_plan` 报 `active_task null` / `implementation_cards_done 2`。
- 注意 `docs/stages/S1-01-current-handoff.md` 与 `docs/stages/S1-01-vm-final-review.md` 是 S1-01 的**历史收尾/复验指令**，不再指向当前工作；当前入口以 `README.md` 与机器索引为准。
