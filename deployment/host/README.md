# 主机依赖锁定与来源说明（S1-01 建立，S1-02 更新 vendor 状态）

本文件记录主机（Linux x86_64）第三方依赖的来源、锁定方式与许可义务。S1-01 建立时本文件不下载、不编译、不 vendor 任何第三方源码；此后 S1-02 已按锁定条目把 SQLite 3.54.1 amalgamation 与 cJSON 1.7.19 vendor 到 `host/storage/third_party/`（见 §2.1），锁定版本、提交与归档摘要均未改变。机器可读清单是同目录的 [dependencies.lock.json](dependencies.lock.json)，上游许可原文在 [licenses/](licenses/)。

## 1. 本卡范围内的实际依赖

`host/common` 基础库（有界队列、单调时钟、UUID/u64 边界工具、受限日志）**没有任何第三方依赖**：只使用 C11、POSIX 线程、`clock_gettime`、`getrandom` 与标准 C 库。因此 S1-01 的 Linux 配置/构建/CTest 只需要 cmake、gcc、make，不涉及网络，也不受下述未 vendor 依赖阻塞。

## 2. 阶段 1 依赖清单与消费卡（已锁定）

| 依赖 | 消费卡 | 用途 | 锁定版本 | 不可变标识 | 许可 | 许可原文 |
| --- | --- | --- | --- | --- | --- | --- |
| SQLite | S1-02 | 单写仓储、事务与 WAL | 3.54.1（`SQLITE_VERSION_NUMBER 3054001`） | amalgamation `3540100`，`sha256=0a9c8e34…ad1f9` | Public Domain | [sqlite-PUBLIC-DOMAIN.txt](licenses/sqlite-PUBLIC-DOMAIN.txt) |
| CivetWeb | S1-05 | HTTPS/WS、REST 适配、TLS 终止 | v1.16 | `commit=d7ba35bb…a15159`，`sha256=f0e471c1…60285` | MIT（含 Lua 5.3 MIT 段） | [civetweb-LICENSE.md](licenses/civetweb-LICENSE.md) |
| cJSON | S1-02、S1-05 | 有界 JSON 解析/序列化 | v1.7.19 | `commit=c859b25d…7be3`，`sha256=7fa616e3…54562` | MIT | [cjson-LICENSE.txt](licenses/cjson-LICENSE.txt) |
| libsodium | S1-05 | Argon2id 口令哈希、常量时间比较 | 1.0.22-RELEASE | tag object `bc5892be…06a7` → `commit=77e1ce5d…86bda`，`sha256=5838bb0c…1631c` | ISC | [libsodium-LICENSE.txt](licenses/libsodium-LICENSE.txt) |
| TLS 库（候选 OpenSSL） | S1-05 | CivetWeb 的 TLS 后端 | openssl-4.0.3 | `commit=af1775b6…7e9e`，`sha256=325b5c80…16d9` | Apache-2.0 | [openssl-LICENSE.txt](licenses/openssl-LICENSE.txt) |

依据：`docs/stages/stage-01-task-cards.md:17` 要求复用既定 CivetWeb、SQLite、cJSON、libsodium 及 TLS 库，不自研 HTTP、密码算法或数据库；实际版本与来源在实施时锁定到提交/tag 并记录 license，禁止浮动 `master`。`docs/项目计划.md:110` 与 `docs/architecture/reviews/architecture-expert.md:137` 已把 CivetWeb 作为 HTTP 选型，`docs/stages/stage-00-architecture.md:142` 要求用 Argon2id 存口令。

TLS 具体实现阶段文档未指定，因此 OpenSSL 4.0.3 只是**已核验的候选**（S1-01 把最新可核验 release 的元数据与许可锁好，方便 S1-05 决策），不是替 S1-05 做的选型。mbedTLS、wolfSSL 未在此锁定。

### 2.1 已 vendor 的源码树（S1-02 落地）

S1-02 把两个锁定条目真正 vendor 进仓库，机器可读记录在 `dependencies.lock.json` 的 `vendored` 段（含每个文件的 SHA-256 与字节数）：

| 依赖 | vendor 目录 | 消费卡 | 归档摘要核对 | 文件 |
| --- | --- | --- | --- | --- |
| SQLite 3.54.1 | `host/storage/third_party/sqlite/` | S1-02 | `archive_sha256` 一致，3 个成员逐字节相同 | `sqlite3.c`、`sqlite3.h`、`sqlite3ext.h`（`shell.c` 不 vendor） |
| cJSON 1.7.19 | `host/storage/third_party/cjson/` | S1-02、S1-05 | `archive_sha256` 一致，3 个成员逐字节相同 | `cJSON.c`、`cJSON.h`、`LICENSE` |

两条构建事实：

1. 两者由同一个目标 `locker_storage_thirdparty`（`host/storage/CMakeLists.txt`）编译，SQLite 的编译形态锁定为 `SQLITE_THREADSAFE=1`、`SQLITE_OMIT_LOAD_EXTENSION=1`、`SQLITE_DEFAULT_MEMSTATUS=0`、`SQLITE_DQS=0`、`SQLITE_USE_URI=1`。
2. 该目标**故意不加**项目的 `locker_apply_warnings()` 警告门禁：第三方源码不在此修改，不应因项目 `-Werror` 而失败。项目自身源码仍全部走警告门禁。

vendor 的理由：目标 VM 内没有 `sqlite3.h`、没有外网也没有 apt 源，而 `SQLITE_THREADSAFE` 等编译形态必须固定，系统包版本不可控。CivetWeb、libsodium 与 TLS 后端仍未 vendor，由 S1-05 负责。

## 3. 本轮在线核验（已完成，仅限元数据）

S1-01 当时经工作机 HTTP 代理（`127.0.0.1:7897`）只做**只读元数据核验**：GitHub REST API 查 release/tag、`raw.githubusercontent.com` 取 LICENSE 原文、`codeload`/`sqlite.org` 下载归档算 SHA-256。S1-01 没有安装、没有编译、没有链接、也没有把任何第三方源码放进仓库（后续 S1-02 才 vendor SQLite 与 cJSON，见 §2.1）。

已核验并写进 lock 的字段：`tag`、`commit`（libsodium 额外记 annotated tag object SHA）、`archive_bytes`、`archive_sha256`、`license`、`license_verified=true`、`verified=true`。

两点如实说明，不是通过项：

1. **`verified` 只表示"发布元数据与许可原文已核对"，不表示该依赖被构建或链接过。** S1-01 没有编译任何第三方源码；第一个真正编译第三方源码的是 S1-02 的 `locker_storage_thirdparty` 目标。
2. OpenSSL 归档第一次用 `Invoke-WebRequest` 下载两次失败（"从传输流收到意外的 EOF 或 0 个字节"），改用 `curl.exe` + 代理环境变量后成功，`sha256` 来自那次成功的完整 55235667 字节下载。该重试方法记在 lock 的 `verification_log.failed_attempts` 里，便于复现。
3. SQLite 不通过 git tag 分发，因此 `commit` 为 `null` 并在 `commit_note` 说明；它由 amalgamation 归档自身的 SHA-256 锁定，版本号是从下载到的 `sqlite3.h` 里读出来的，不是从下载页抄的。

## 4. 锁定规则（后续卡必须遵守）

1. 每个依赖固定到不可变提交（有 tag 的同时记 tag），不引用浮动分支、不带版本号的压缩包或配置期下载。
2. 不在 CMake 配置期联网抓取；只用 vendor 目录或预先填充的缓存。
3. 消费卡首次链接某依赖的同一个提交里，必须把 vendor 进来的源码树与 lock 里记录的 `archive_sha256` 对齐，并保持 `verified=true`；`verified=false` 的依赖不得被链接，这是硬停止而不是告警。
4. 不在本文件记录任何凭据、密钥或证书私钥；依赖说明只描述来源与版本。

## 5. 与固件依赖锁的关系

`deployment/firmware/dependencies.lock.json` 属固件依赖（FreeRTOS、littlefs、本地 HAL），由既有 `scripts/setup_firmware_dependencies.py` 与 `scripts/build_firmware.py` 读取，本卡未修改也未复用该文件。主机依赖单独记录在本目录，两者互不影响。
