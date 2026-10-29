# host/simulator — 独立模拟设备（locker-device-sim）

本模块属于 S1-03，用两个 `AF_UNIX SOCK_STREAM` socket 提供 UART-v1 字节与权限 0600 的私有控制入口，使用独立 SQLite 日志，不共享业务库。契约基线 0.2.1，规格以 `docs/architecture/uart-v1.md` 为准。

- 状态：S1-03 **DONE**，实际验证记录见 `docs/evidence/stage-01/S1-03/summary.md`。
- 硬件脉冲只模拟状态与计数，不驱动 GPIO，不证明真实关断。
- SQLite WAL + `synchronous=FULL` 只验证软件持久化语义，不证明 MCU/littlefs/RK 介质掉电可靠性。
- 无 HTTP 接口，不连接真实锁。

## 启动与目标

`locker_simulator` 是无 socket 的引擎，复用 locker_common、locker_device 及现有 locker_storage_thirdparty；`locker-device-sim` 拥有 socket 与控制文本协议，不新增第三方库。

```bash
locker-device-sim --uart PATH --control PATH [--journal PATH] [--device-uid HEX12]
```

`--uart` 输入原始协议字节，输出响应与 DEVICE_EVENT；`--control` 一行一条私有命令；`--journal` 默认 `<control>.journal`；`--device-uid` 必须为 24 个完整 hex 字符，默认 UID 为 `404142434445464748494a4b`。

三个路径在创建任何数据库或 socket 之前校验：uart/control 必须放得进 `sun_path`（108 字节含终止符），journal 必须放得进 108 字节字段。默认 journal 名不做静默截断——control 为 99 字节时 `<control>.journal` 恰好 107 字节可接受，100 字节及以上直接退出码 2 并报告 `journal path too long`。

路径身份按**解析后的目录项**比较，不是按字符串：`D/x` 与 `D/./x`、经父目录符号链接指向同一条目的写法，都算同一个文件。末级是**悬空符号链接**时也解析到链接目标（最多 8 跳，成环或无法解析一律拒绝），因为 SQLite 会沿链接创建目标文件，而 `bind` 前的 `unlink` 删的是同一个目录项。sidecar 按两种形态都要查：解析后的目标是数据库文件实际成为的条目，**字面拼写是本模块自己的保守限制、不是 SQLite 的要求**——vendored SQLite 的 `unixFullPathname`（`host/storage/third_party/sqlite/sqlite3.c` 的 lstat/readlink 分支）会解析末级链接，`-wal`/`-shm` 落在目标名下；即便如此仍同时拒绝 `<链接目标>-wal` 与 `<字面拼写>-wal`，让路径身份不依赖 VFS 的这一细节。拒绝四类冲突——uart 与 control 同条目、journal 与任一 socket 同条目（按链接目标）、`<journal>-wal` 与任一 socket 同条目、`<journal>-shm` 与任一 socket 同条目（后两类按目标与字面拼写两种形态各查一次）——统一报告 `paths must name distinct filesystem entries`。不这样比对的后果是具体的：journal 与 control 同条目时 daemon 先建库，再在 bind 时 `unlink` 掉自己的主库，仍打印 ready，restart 报 `error io_error`，退出后只剩 `-wal`/`-shm`；control 落在 `<journal>-wal` 上时 WAL 被 socket 替换，重启后控制路径变回普通文件、新连接 `errno=111`；uart 与 control 同条目时第二次 bind 会 `unlink` 掉 UART socket，按配置连 UART 的客户端实际进入文本控制解析器。所有拒绝都发生在 `locker_sim_open` 与 `listen_unix` 之前，不创建也不删除任何资源；已存在的普通文件与符号链接原样保留。

两个 socket 均先 `umask(0077)` 再 bind、chmod 0600。启动标识为 `ready device backend: simulator uart=... control=... journal=...`。接收完全复用 wire.c 的有界流解析和逐字节 CRC 重同步。发送为非阻塞尝试加总计 1000ms 写期限；发送失败关闭 UART，持久日志通过重连后 FETCH 补取。

## 私有控制入口

| 命令 | 作用 |
|---|---|
| `status` | 多行 key=value 回读 |
| `door unknown\|closed\|open` | 事务提交模拟输入、revision 与对应门事件 |
| `item unknown\|absent\|present` | 事务提交模拟输入、revision 与对应物品事件 |
| `card HEX` | 逐对解析 4/7/10 字节 UID 并产生读卡事件；引擎 API 另允许空读卡 |
| `reject_next on\|off` | 下一 EXECUTE 一次性拒绝 |
| `drop_ack on\|off` | ACK_PERSISTED 正常应用，但不发该请求响应 |
| `drop_execute_ack on\|off` | EXECUTE 正常接纳/驱动，但不发该请求响应，与 drop_ack/suppress_pulse 独立 |
| `suppress_pulse on\|off` | 意图保留 ACCEPTED，脉冲从未发生；不表示脉冲结束 |
| `fail_journal on\|off` | 注入日志故障；off 不解除真实事务失败的 FAULT 锁存。仅前置注入且未发生事务失败时可恢复 HEALTHY，仍须重新完成 READY 屏障 |
| `inject_gap N` | 持久累计丢失 N 条并锁存 HISTORY_INCOMPLETE |
| `drop_link` | 关闭 UART 客户端，保留设备日志、boot 和会话 |
| `restart` | close + open，boot+1，旧会话失效，保留未决上下文；门/物品变为 UNKNOWN |
| `cursor B:S` | 严格解析并回显游标 |
| `quit` | 回复 ok 后退出 |

status/quit/restart/drop_link 不接受参数。参数必须整 token 有效，数字拒绝符号、溢出和尾垃圾；游标必须两项都零或都非零。单行最多 255 字节，超长回复一次 `error line_too_long`，丢弃至下一换行；内嵌 NUL 回复 bad_argument 并丢弃该行。控制事件提交后及轮询时立即尝试推送，不依赖下一次 UART 请求。

回复为 `ok [detail]` 或 `error <detail>`。语法错误包括 bad_argument/missing_argument/unknown_command/line_too_long；引擎错误包括 invalid/not_ready/conflict/busy/no_space/io_error/unknown/replay_blocked/failed。

status 字段为 session、journal_epoch、boot_id、action_watermark、dropped_events、device_mode、journal_state、door、item、command_state、has_active、continuity_ok、first_available、last_committed、ack_persisted、event_count。

## 恢复、动作与持久化

正常恢复顺序：HELLO → GET_SNAPSHOT → FETCH 到快照提交边界 → 主机可靠落库 → ACK_PERSISTED → 核对并释放旧上下文 → 新鲜 HOST_HEALTH → RESUME_READY。READY 要求输入明确、日志健康连续、无活动上下文，epoch/boot/revision 匹配且持久游标覆盖屏障。Core 观察年龄必须已知且在 5 秒内，相同或倒退 progress 不延长新鲜期；UI 未知不能代替 Core 健康。

EXECUTE 只在 READY 且 Core 健康时接纳。意图提交后、驱动前重新采时并复查 guard 与 `min(not_after, receive+2000ms)`。正常模拟脉冲持续 150ms，请求、事件轮询或关门转换推进 PULSE_FINISHED 并持久更新结果。匹配的 RELEASE_CONTEXT 只有提交清空检查点后才能成功；它不改变业务借还账本。

- 事务固定 BEGIN IMMEDIATE；sim_meta 保存 epoch、boot、当前会话水位、活动/释放检查点和连续性；sim_events 保存事件。
- sim_commands_v2 按 `(session_id, action_no)` 分隔历史，原 sim_commands 保留用于兼容已有独立日志。历史 command 可通过当前会话 QUERY 查询。
- 同会话重复意图须匹配 operation、command、deadline、slot、kind、boot/revision 等完整绑定，返回原记录。新会话水位从零开始，同会话 HELLO 重试不重置；旧指令不能迁移到新会话。
- open 递增 boot 并追加 BOOT，恢复未决意图及终结/释放检查点。restart 将输入置 UNKNOWN，须显式观测，不推定闭门或物品在位。
- 通知队列上限 32；满时只丢实时通知，日志仍可 FETCH，不改变连续性。真正历史缺口持久锁存计数和标志，FETCH 报 gap，ACK 不跨缺口，restart/RESUME 不清故障。
- 门/物品控制与事件同事务，提交失败拒绝该次模拟控制并回滚输入和 revision。接纳、脉冲结果与释放检查点均须提交后才报告确定结果。
- 私有 C 测试 API `locker_sim_control_fail_next` 可注入 BEGIN/INTENT/EVENT/COMMIT/RESULT 一次失败；不新增公开 HTTP/UART 消息。

## 测试与交接

正式测试包括 test_sim_journal.c、test_sim_protocol.c、test_device_port.c 与真实进程 daemon_smoke.py。Python3 缺失时配置明确报 `daemon acceptance NOT_RUN` 并失败，不能靠减少测试数验收。

验证状态见 `docs/evidence/stage-01/S1-03/summary.md`：VMware Ubuntu 26.04.1 LTS（run_id 见 `docs/evidence/stage-01/S1-03/vm-s103-run-summary.log` 首行）上 19/19 CTest、`-Werror` 零警告、ASan+UBSan 0 报告、TSan 0 命中，daemon 进程级 391 条检查 0 失败，56 点位变异结果见同目录 `mutation.log`。真实串口、HAL/GPIO、硬截止和 MCU 掉电均 NOT_RUN。

签名和所有权以生成头及当前头文件为准：`locker_device_close` 返回 locker_status_t；destroy 后指针失效，调用者应置 NULL，不得再次调用；`locker_sim_destroy` 会关闭独立日志。
