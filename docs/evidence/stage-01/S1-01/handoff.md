# S1-01 交接说明（handoff）

日期 2026-10-23。对应实施报告：同目录 `summary.md`；源码指纹：同目录 `source-manifest.txt`（29 文件，sha256 清单）。
本卡状态 **DONE**（2026-10-23 收尾，独立审计 PASS、无阻塞项）；不 commit/push，不自动开始 S1-02。
下一步由 S1-02 开发 AI 接手：先核对 `config/development/stage-plan.json` 中 S1-01 已 DONE，再读本文件与 `summary.md`。

本文件面向下一位调用者（S1-02 起的 host 侧模块、集成负责人、复核者），说明**怎么用**这套基础库、边界在哪、哪些没做。
第 6 节含 `audit.md` 整改后的调用契约变化，读过旧版本的请以本版为准。

---

## 1. 构建与接入

### 1.1 命令（唯一入口，`docs/stages/stage-01-task-cards.md:125-135`）

```sh
# 仅构建基础库
cmake -S . -B build/host -DLOCKER_BUILD_HOST=ON
cmake --build build/host

# 构建 + 跑单测
cmake -S . -B build/host -DLOCKER_BUILD_HOST=ON -DLOCKER_BUILD_TESTS=ON
cmake --build build/host -j2
ctest --test-dir build/host --output-on-failure
```

### 1.2 CMake 选项（根 `CMakeLists.txt:16-18`）

| 选项 | 默认 | 作用 |
|---|---|---|
| `LOCKER_BUILD_HOST` | `ON` | 关闭后整个 host 构建短路（`CMakeLists.txt:79-82`） |
| `LOCKER_BUILD_TESTS` | `OFF` | 打开后 `enable_testing()` 并加入 `tests/unit/common` |
| `LOCKER_WARNINGS_AS_ERRORS` | `OFF` | GNU/Clang 追加 `-Werror`，MSVC 追加 `/WX` |

基础警告恒开：GNU/Clang `-Wall -Wextra -Wpedantic`；MSVC `/W4`。语言标准 C11、`CMAKE_C_EXTENSIONS OFF`（`CMakeLists.txt:20-22`）；默认构建类型 `RelWithDebInfo`（`:24-26`）。

### 1.3 新增模块怎么接进来

1. 在自己的目录写 `CMakeLists.txt`，目标名用 `locker_*`。
2. 调 `locker_apply_warnings(<target>)`（根 `CMakeLists.txt:65-77` 定义的函数，对所有子目录可见）。
3. 需要契约类型时，链接 `locker_common` 即自动获得 `contracts/include` 的 include 路径（`host/common/CMakeLists.txt:14-18` 以 PUBLIC 传播），**不要**自己 `target_include_directories` 到 `contracts/include`。
4. 需要线程时链接 `locker_common` 即获得 `Threads::Threads`（PUBLIC 传播，`host/common/CMakeLists.txt:20`）。
5. 在根 `CMakeLists.txt` 的 `add_subdirectory(host/common)` 之后追加一行 `add_subdirectory(host/<你的模块>)`。**根文件只有集成负责人能改**（`docs/architecture/module-ownership.md:33`）。

### 1.4 契约版本门禁（`CMakeLists.txt:33-63`）

配置期读 `contracts/include/locker_contracts.h` 的 `#define LOCKER_CONTRACT_VERSION "x.y.z"` 与 `contracts/contract-source.json` 的 `"contract_version": "x.y.z"`，任一读不到或不相等即 `FATAL_ERROR`：

- 头缺失 → `Generated contract header not found: ... - regenerate with: python -B scripts/build_contracts.py`
- 漂移 → `Contract version drift: header=<a> source=<b> - regenerate with: python -B scripts/build_contracts.py`

配置成功打印 `-- locker host build: contract 0.2.1`。这道门禁只防"编译到旧副本"，**不替代** `scripts/check_contracts.py`。

---

## 2. 模块一：有界队列 `locker_bounded_queue`

头文件 `host/common/include/locker/common/bounded_queue.h`（81 行），实现 `host/common/src/bounded_queue.c`（132 行）。

### 2.1 调用方式

```c
locker_bounded_queue_t queue;                 /* 必须先清零 */
sample_message_t storage[64];                 /* 调用方提供，容量 × 元素大小字节 */
memset(&queue, 0, sizeof(queue));
locker_status_t st = locker_bounded_queue_init(&queue, storage, 64, sizeof(sample_message_t));
/* st == LOCKER_STATUS_OK */

locker_bounded_queue_try_push(&queue, &msg);  /* OK / NO_SPACE / INVALID */
locker_bounded_queue_try_pop(&queue, &out);   /* OK / NOT_READY / INVALID */
locker_bounded_queue_peek(&queue, &out);      /* 只看不取 */

locker_bounded_queue_destroy(&queue);
```

### 2.2 参数与内存契约

- `storage` 由**调用方**提供，长度必须 `>= capacity * element_size`，且生命周期必须长于队列（`bounded_queue.h:53`）。库内**不做任何堆分配**，init 之后零 malloc。
- `element_size` 是元素字节宽度；push/pop 用 `memcpy` 复制整个元素，**按值语义**。调用方的消息可以放在栈上、临时帧里、CivetWeb 回调缓冲里，入队后即与调用方无关（这正是"消息生命周期不依赖调用方栈"的实现方式，测试见 `tests/unit/common/test_bounded_queue.c:138-177`）。
- 队列结构体 `locker_bounded_queue_t` 初始化后**不可移动**（内部含 `pthread_mutex_t`），不能按值拷贝或 memmove。
- 元素本身若是含指针的结构体，**深拷贝责任在调用方**；本库只做字节复制，不追踪内部指针。

### 2.3 线程与并发（整改后措辞，见 `bounded_queue.h:16-23`）

- `try_push` / `try_pop` / `peek` / `get_stats` / `size` 可**多线程并发**调用，数据访问持内部 `pthread_mutex`。
- **`init` 与 `destroy` 是所有者独占的生命周期调用**，不是线程安全的：`destroy` 不得与同一队列上的任何其他调用重叠，必须先 join 生产者/消费者线程，与 `pthread_mutex_destroy` 的约束一致。并发 `destroy` vs `push/pop` 是 **data race**，不是可恢复错误，库不会返回错误码兜住它。
- **没有阻塞式 pop**：Core 循环自己负责 drain；队列空返回 `NOT_READY`，调用方决定轮询或退避。这样避免在工作线程里引入条件变量生命周期问题。
- `locker_bounded_queue_capacity / _element_size / _is_initialized` 接收 const 指针、不取锁，只在 init 完成后读取。

### 2.4 失败路径（精确）

| 情形 | 返回 | 副作用 |
|---|---|---|
| `queue==NULL` / `storage==NULL` / `capacity==0` / `element_size==0` | `LOCKER_STATUS_INVALID` | 无 |
| 对已初始化队列再次 init | `LOCKER_STATUS_CONFLICT` | 原队列不受影响 |
| `pthread_mutex_init` 失败 | `LOCKER_STATUS_IO_ERROR` | 无 |
| 队列已满时 push | `LOCKER_STATUS_NO_SPACE` | `rejected_total += 1`；**不覆盖最老元素**，size 不变 |
| 队列空时 pop/peek | `LOCKER_STATUS_NOT_READY` | 无 |
| `out_stats==NULL` 等空指针 | `LOCKER_STATUS_INVALID` | 无 |

统计字段 `locker_bounded_queue_stats_t{size, capacity, pushed_total, popped_total, rejected_total}` 用于暴露"满"这个真实事件，便于上层做背压/告警，而不是静默丢弃。

### 2.5 销毁

`locker_bounded_queue_destroy` 销毁 mutex 并清零 `initialized`，可对已清零/未初始化的队列重复调用（幂等）。**不释放 `storage`**（不是库分配的）。destroy 之后不得再 push/pop；destroy 与并发访问的重叠是 data race（见 2.3）。

---

## 3. 模块二：时钟与可注入端口 `locker_clock`

头文件 `clock.h`（64 行），实现 `clock.c`（191 行）。

### 3.1 生产路径（业务代码只能这样用）

```c
uint64_t now = locker_clock_real_monotonic_ms();      /* 单调毫秒 */
uint64_t wall; bool ok = locker_clock_real_wall_utc_ms(&wall);   /* UTC 毫秒，失败返回 false 且不改 *out */
locker_uuid_t id; locker_clock_real_new_uuid(&id);    /* v4，无熵源则 IO_ERROR */
locker_clock_port_t port = locker_clock_port_real();  /* 传给 logger 等 */
```

**库只导出 `locker_clock_port_real()`，不导出任何 setter**（`clock.h:7-9` 明写），因此测试替身无法通过公共 API 被打开，生产路径必然是真实时钟。这是本卡"业务生产路径使用真实时钟，测试替身不能通过公共 API 控制"的实现方式。

### 3.2 测试路径（只在测试二进制内部）—— 已按真源签名修正

`locker_clock_port_t` 的真源是 `contracts/include/locker_contracts.h:134`：

```c
typedef struct {
    void *user;
    uint64_t (*monotonic_ms)(void *user);                 /* 注意：uint64_t，不是 status */
    bool (*wall_utc_ms)(void *user, uint64_t *out);
    locker_status_t (*new_uuid)(void *user, locker_uuid_t *out);
} locker_clock_port_t;
```

测试在自己的文件里构造 fake 端口（照抄 `tests/unit/common/test_clock.c:21-60` 的写法）：

```c
typedef struct fake_clock {
    uint64_t monotonic_ms;
    uint64_t wall_ms;
    bool wall_available;
    uint32_t next_uuid_seed;
} fake_clock_t;

static uint64_t fake_monotonic(void *user) {          /* 返回 uint64_t，没有状态码 */
    return ((fake_clock_t *)user)->monotonic_ms;
}

static bool fake_wall(void *user, uint64_t *out_wall_ms) {
    const fake_clock_t *clock = (const fake_clock_t *)user;
    if (!clock->wall_available) { return false; }
    *out_wall_ms = clock->wall_ms;
    return true;
}

static locker_status_t fake_new_uuid(void *user, locker_uuid_t *out_uuid) {
    fake_clock_t *clock = (fake_clock_t *)user;
    memset(out_uuid, 0, sizeof(*out_uuid));
    out_uuid->bytes[15] = (uint8_t)++clock->next_uuid_seed;
    return LOCKER_STATUS_OK;
}

/* 具名赋值，不要用位置初始化（首字段是 user，不是函数） */
locker_clock_port_t port = {0};
port.user         = &fake;
port.monotonic_ms = fake_monotonic;
port.wall_utc_ms  = fake_wall;
port.new_uuid     = fake_new_uuid;
```

`locker_logger_init(..., &port)` 即可让日志时间戳可控。生产代码**不得**接收外部传入的时钟来做决策绕过真实时间；本卡没有提供这种入口。

### 3.3 端口校验与 deadline

- `locker_clock_port_is_valid`：三个函数指针非空才算有效；无效端口传给 `locker_logger_init` 返回 `INVALID`。
- `locker_deadline_after(now, timeout)`：**饱和加法**，`now > UINT64_MAX - timeout` 时结果为 `UINT64_MAX`，绝不回绕（`clock.c:168-177`）。
- `locker_deadline_expired(&d, now)`：`valid==false` 的 deadline **视为已过期**（fail closed，`clock.c:179-184`）——未设置的超时不会被当成"永不过期"。
- `locker_deadline_remaining_ms`：已过期返回 0。
- 所有超时按单调钟测量（`docs/architecture/internal-interfaces.md:15`）；墙钟只做日志关联锚点，**不用于超时判断**（会被 NTP 跳变影响）。

### 3.4 随机源

`fill_random` 先试 `getrandom(2)`（EINTR 重试），失败回退 `/dev/urandom`（`clock.c:59-98`）。生成 UUID 时置 version 4 与 RFC 4122 variant 位（`clock.c:100-112`）。两条路都失败 → `LOCKER_STATUS_IO_ERROR`，不返回全零 UUID 冒充成功。

### 3.5 已知限制（整改后如实说明，见 `clock.h:28-39`）

- **时钟失败没有错误通道**。`locker_clock_port_t.monotonic_ms` 的签名是 `uint64_t(void *user)`，契约不给它返回状态的机会。平台调用失败时只能返回 0，而 0 与真实读数（`CLOCK_MONOTONIC` 是 uptime，刚开机确实可能接近 0）**不可区分**。因此 `locker_clock_port_monotonic_ms()` 在这种情况下返回 `LOCKER_STATUS_OK` + `0`，**不是** `IO_ERROR`。
- 需要"时钟不可用时不 fail-open"的调用方，必须自己把"已知区间内 monotonic 没有推进"当作异常处理，不能依赖本端口报错。
- 给端口加状态通道属于**契约负责人**的变更（改 `locker_contracts.h` + 生成器），不在 S1-01 范围；本卡没有为了让测试好写而私改公共类型。
- 未做 `CLOCK_MONOTONIC_RAW` 之类的可选项；语义按 `CLOCK_MONOTONIC`。
- 时钟失败路径**没有自动化故障注入测试**（`clock_gettime` 返回 -1 的分支未在 CTest 覆盖）。

---

## 4. 模块三：UUID 文本 `locker_uuid`

头文件 `uuid.h`，实现 `uuid.c`（111 行）。

- `locker_uuid_t` 来自契约头（`locker_contracts.h`），16 字节。
- `locker_uuid_parse(text, len, &out)`：**严格** 36 字符小写 canonical（`xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx`），位置 8/13/18/23 必须是 `-`，只接受小写 hex。任何不符 → `LOCKER_STATUS_INVALID`，且**不写** `out_uuid`（`uuid.c:48-84`），调用方原值不被部分覆盖。
- `locker_uuid_format(uuid, buf, buf_size)`：需要 `buf_size >= 37`，输出小写 + NUL。缓冲不足返回 `INVALID`。
- `locker_uuid_is_nil / _equal / _clear`：全零判定、16 字节比较、清零。
- `locker_uuid_is_rfc4122_v4`：校验 version nibble == 4 且 variant 高 2 位 == `10b`。
- 契约要求 `request_id` 为小写 UUID（`docs/architecture/internal-interfaces.md:60`），上层**不要**自造大写或带花括号的变体。

---

## 5. 模块四：u64 规范十进制 `locker_u64`

头文件 `numeric.h`，实现 `numeric.c`（87 行）。用途：`boot_id`、`event_seq`、`action_no`、`physical_revision` 这类 u64 在 JSON/日志/DB 之间传递时不出现 `1e18`、`+5`、`007` 这类多义写法。

> 注意（audit.md N4 整改）：**卡 UID 不是 u64**。真源是 `card_uid_len` + `card_uid[10]`（4/7/10 字节，`contracts/include/locker_contracts.h:119`、`:158`；`docs/architecture/uart-v1.md:44`、`:336-337`），仓储侧用 `card_uid_hex`/`uid_hex`（`contracts/internal-data-source.json:1936`、`:2868`）。10 字节塞不进 u64，前导零和长度也会丢，**不要**用本模块的 u64 函数处理卡 UID。

| 函数 | 语义 |
|---|---|
| `locker_u64_is_canonical_decimal(text, len)` | 纯数字、无符号、无空白、无前导零（"0" 本身合法）、长度 1..20 |
| `locker_u64_parse_decimal(text, len, &out)` | 严格按上面规则解析；溢出判定 `value > (UINT64_MAX - digit) / 10`（`numeric.c:22-36`）；失败不写 `out` |
| `locker_u64_format_decimal(value, buf, buf_size)` | 先算位数再校验 `buf_size >= digits+1`（`numeric.c:38-55`）；不足返回 `INVALID` |
| `locker_u64_text_compare(a, la, b, lb)` | **先解析再数值比较**，绝不字典序（否则 "9" > "10"） |
| `locker_u64_add_checked(a, b, &out)` | 溢出返回 `false`，不静默回绕 |

边界已测：0、`UINT64_MAX`（18446744073709551615，20 位）、越界 `18446744073709551616`、21 位、空串、前导零、`+`/`-`、空格、`0x` 前缀、非 ASCII（`tests/unit/common/test_numeric.c`）。

---

## 6. 模块五：受限日志 `locker_logger`（含 audit.md 整改后的调用契约）

头文件 `log.h`（104 行），实现 `log.c`（**623 行**，本卡最大件）。B1/B2/B1-R/B3 整改细节见 `summary.md` 第 6 节，这里只讲**调用方需要知道什么变了**。

### 6.1 调用方式

```c
static void sink(void *user, locker_log_level_t level, const char *line) {
    /* 写文件/stdout；line 只在本次回调期间有效，要留存必须复制 */
}

locker_logger_t logger;
locker_logger_init(&logger, sink, NULL, "core.queue",       /* component：借用，须比 logger 活得久 */
                   LOCKER_BACKEND_SIMULATOR,                 /* 阶段 1 固定 simulator */
                   LOCKER_LOG_LEVEL_INFO, NULL);             /* clock==NULL → 真实时钟 */

const locker_log_field_t fields[] = {
    { "queue", "core_in" },
    { "size", "64" },
};
locker_logger_log(&logger, LOCKER_LOG_LEVEL_INFO, "queue_full", fields, 2);
locker_logger_deinit(&logger);
```

### 6.2 行格式（`log.c:529-623`，`locker_logger_log`）

```
ts=<monotonic_ms> utc=<wall_ms|unavailable> level=<ERROR|WARN|INFO|DEBUG> backend=<simulator|hardware> component=<...> event=<...> key=value key=value...
```

- `backend=simulator` 在阶段 1 是硬性要求（`docs/stages/stage-01-task-cards.md:19`），`LOCKER_BACKEND_HARDWARE` 只允许在真机卡启用。
- 单行上限 `LOCKER_LOG_LINE_MAX`=512 字节，**栈上定长缓冲**；超长截断并在末尾补 `...`。日志调用不会无界增长内存。
- component/event/key 经 `sanitize_token` 白名单化（防注入空格/换行伪造字段）；值含空格时加引号。
- `field_count` 个字段逐条处理，`key==NULL` 或 `value==NULL` 的条目**跳过不致命**。

### 6.3 脱敏（本卡硬验收，整改后）

**第一层：键名拒绝表（归一化匹配，`log.c:89-189`）**

键先**归一化**——丢弃 `_` `-` `.` 空格 `\t` `:` `/`，再转小写——然后与拒绝表**等值或后缀**匹配。后缀匹配跑在**完整原始键**上（`key_ends_with_n()`，`log.c:97`，从键尾向前跳过分隔符比对），所以**键长没有上限**：audit B3 的 69 字符键 `63×'a' + "_token"` 现在命中；归一化等值匹配才用 63 字符定长缓冲，而等值表最长项 13 字符，超长归一化键不可能等于任何一项，因此不会漏判。

- 等值表：`token`、`accesstoken`、`refreshtoken`、`sessiontoken`、`idtoken`、`password`、`passwd`、`secret`、`clientsecret`、`privatekey`、`authorization`、`cookie`、`setcookie`、`apikey`、`credential`、`carduid`、`rawuid`、`uid`。
- 后缀表：`token`、`secret`、`password`、`passwd`、`privatekey`、`credential`、`apikey`、`carduid`、`rawuid`。
- 归一化的效果：`X-Token`、`authToken`、`reader_card_uid`、`tls.private_key`、`clientSecret`、`set-cookie` 全部命中；`device_id`、`operation_id`、`boot_id` 仍可记录。命中则值替换为 `[redacted]`。

**第二层：自由文本掩码 `locker_log_scrub`（`log.c:312-416`）**

`scrub_stream()` **单遍边扫边写，没有 span 预算**（整改删除了原来 16 段的 `LOCKER_LOG_MASK_SPANS` 上限）。掩码三类形态：

1. `<scheme> <credential>`，scheme ∈ bearer/basic/digest。credential 一直吃到自然结束——`.` `~` `+` `/` `=` 都算凭据字符（这正是 B1 的修复点）。
2. `<敏感键>=<value>` / `<敏感键>: <value>`，引号值也覆盖；**单引号与双引号同等对待**（`scan_quoted_value(input, length, start, quote, &terminated)`，`log.c:264`），处理反斜杠转义；**引号未闭合时掩码到结尾**（fail closed）。audit B1-R 的 `password='alpha beta'`、`{'password':'alpha beta'}`、`'password'='alpha beta'` 全部整值掩码，单引号作为键的结束引号也会被跳过（`log.c:350-353`）。
3. 敏感键后接 scheme 再跟值（如 `token=Bearer abc`）。

掩码时**保留键名与方案名**，只替换值：`Authorization: Bearer a.b.c` → `Authorization: [redacted]`；`{"password":"alpha beta"}` → `{"password":"[redacted]"}`；`note="password='alpha beta'"` → `note="password='[redacted]'"`。

**单引号不会误伤散文**：单引号只有在敏感键后作**值的起始引号**时才进入引号分支（`log.c:357`）；普通散文里的撇号（`it's a plain sentence`）走普通字符路径，原样输出（`test_scrub_single_quoted_values` 已断言）。

`locker_log_key_is_sensitive(key)` 公开，供上层在组装字段前自查。

**重要边界（`log.h:29-32` 已写明）**：脱敏是防御纵深，不是数据流证明。**没有敏感键、也不符合 `key=value` / 空格方案形态**的自由文本秘密（例如 "the card was 04A2B3C4"）按模式无法发现。调用方**必须**把秘密作为敏感键字段传入。

### 6.4 线程与生命周期（整改后措辞，见 `log.h:34-37`）

- `locker_logger_t` 内含 sink 指针与 user，**无内部锁、无共享可变状态**。同一 logger 可被多线程调用，但 sink 会并发执行；需要串行输出时由调用方保证（每个线程一个 logger，或 sink 内部加锁）。
- **`locker_logger_init` / `locker_logger_deinit` 不是线程安全的**，不得与并发 `locker_logger_log` 重叠；deinit 必须在最后一次 log 返回之后（或 join 掉会打日志的线程之后）调用。
- **sink 收到的 `line` 是回调期间借用的栈缓冲**（来自 `locker_logger_log` 的定长栈数组）。异步 sink（投递到队列、交给别的线程写盘）**必须自行复制**，回调返回后该指针立即失效。
- `component` 指针是**借用**，必须比 logger 活得久（`log.h:62`）；库不复制字符串。
- `clock == NULL` 时用真实时钟；传入 fake 端口则时间戳可控（测试用）。
- `locker_logger_deinit` 清 `initialized` 并置空 sink，幂等。

### 6.5 失败路径

| 情形 | 返回 |
|---|---|
| `sink==NULL` / `component==NULL` / `min_level` 越界 / `backend` 越界 | `LOCKER_STATUS_INVALID` |
| 传入的 clock 端口无效 | `LOCKER_STATUS_INVALID` |
| 未初始化 / 已 deinit 的 logger | `LOCKER_STATUS_INVALID` |
| 级别低于 `min_level` | `LOCKER_STATUS_OK`，**不调用 sink**（零副作用，`test_level_filtering` 用计数验证） |
| `fields==NULL` 且 `field_count>0` | `LOCKER_STATUS_INVALID` |

---

## 7. 模块六：构建信息 `locker_common_contract_version()`

`build_info.h` / `build_info.c`（7 行）。返回 `LOCKER_CONTRACT_VERSION` 字符串（编译期常量，`"0.2.1"`），供服务启动时打印/健康检查比对。测试 `test_build_info` 校验它与头文件宏一致且形如 `x.y.z`。

---

## 8. 配置 / 依赖 / 接口版本

| 项 | 值 |
|---|---|
| 契约版本 | 0.2.1（`contracts/contract-source.json`，status REVIEW_DRAFT） |
| 生成头 | `contracts/include/locker_contracts.h`（由 `scripts/build_contracts.py` 生成，**不得手改**） |
| 本卡第三方依赖 | **零**。仅 C11 + POSIX threads + `clock_gettime` + `getrandom`/`/dev/urandom` |
| 构建工具（实测） | cmake 4.2.3、gcc 15.2.0、GNU Make 4.4.1、ctest 4.2.3（Ubuntu 26.04 / 26.04.1） |
| 最低声明 | `cmake_minimum_required(VERSION 3.16)`、C11（未在 GCC 12 / Debian 12 实测） |
| 依赖锁定清单 | `deployment/host/dependencies.lock.json`（9704 B）：SQLite→S1-02，CivetWeb/cJSON/libsodium/OpenSSL→S1-05 |
| 许可原文 | `deployment/host/licenses/` 5 份，逐份记 SHA-256 |
| 接口版本 | 本卡不新增对外接口；公共符号即上列 `locker_*` 函数，随契约版本 0.2.1 一起演进 |

### 8.1 依赖锁的读法（给 S1-02 / S1-05）

- 每条的 `verified: true` 只表示"元数据 + 许可已核对"，**不表示可构建/可链接**。
- `unresolved` 第 3 条是硬停止：`verified=false` 的依赖不得链接。
- 真正 vendor 时，必须**重新下载并比对 `archive_sha256`**，比对不符即停止并报告。
- OpenSSL 4.0.3 是**候选**不是决策；TLS 选型属 S1-05，且它需要 CMake+Perl，S1-05 必须复核工具链。
- SQLite 没有 git tag 语义，锁定的是 amalgamation 归档 SHA-256（`https://www.sqlite.org/2026/sqlite-amalgamation-3540100.zip`，2990966 B）。
- 归档**未进仓库**（体积原因），仓库里只有元数据与许可原文。

---

## 9. 复现方法

```sh
# 0. 契约版本先核对（不得静默用旧副本）
python -B scripts/check_contracts.py            # 期望 status PASS, contract_version 0.2.1

# 1. 全新配置 + 构建 + 测试
rm -rf build/host
cmake -S . -B build/host -DLOCKER_BUILD_HOST=ON -DLOCKER_BUILD_TESTS=ON -DLOCKER_WARNINGS_AS_ERRORS=ON
cmake --build build/host -j2
ctest --test-dir build/host --output-on-failure

# 2. 期望
#    configure: "-- locker host build: contract 0.2.1" + "Found Threads: TRUE"，exit 0
#    build:     零警告（-Werror 打开仍通过），exit 0
#    ctest:     "100% tests passed, 0 tests failed out of 6"，exit 0

# 3. 逐测试断言数（整改后，可核对）
for t in test_bounded_queue test_build_info test_clock test_log test_numeric test_uuid; do
  ./build/host/tests/unit/common/$t
done
# 期望 97 / 5 / 53 / 164 / 133 / 53 checks（合计 505）

# 4. 设计一致性
python -B scripts/check_contracts.py
python -B scripts/check_stage_plan.py
python -B scripts/check_vision_contracts.py
python -B scripts/check_feasibility.py
```

对应日志分四组，**互不替代**（详见 `vm-migration-review.md`）：

| 层 | 环境 / 时点 | 日志 | 断言数 |
|---|---|---|---|
| 1 | VMware 干净 guest，**第一轮整改前**（历史） | `vm-configure.log`、`vm-build.log`、`vm-ctest.log`、`vm-unit-tests.log` | 435 |
| 2 | VMware 干净 guest，第一轮整改后 | `vm-final-*.log`（14 个） | 474 |
| 3 | VMware 干净 guest，第二轮整改后 | `vm-final2-*.log`（15 个） | 505 |
| 4 | VMware 干净 guest，**收尾轮复核当前快照**（**验收主证据**） | `vm-close-*.log`（8 个：environment/source-fingerprint/configure/build/ctest/unit-tests/design-checks/index） | 505 |

设计检查输出在 `check_*.json`（Windows 本机，收尾轮 `captured_at=2026-10-23T23:10:12+08:00`，四项 `exit_code=0`/`status=PASS`）、`vm-final2-design-checks.log`（VM 内第二轮）与 `vm-close-design-checks.log`（VM 内收尾轮）。

**干净环境复现**（验收要求，本轮实际做法）：

> 按用户 m19922 指令，原 WSL2 一组证据（12 个日志 + `evidence-run-wsl.sh`）已全部删除；本卡 Linux 证据只有 VMware 干净 guest 四组，任何 WSL2 记录都不再作为证据引用。

1. 把主副本打成固定指纹快照（`CMakeLists.txt`、`host/`、`tests/`、`contracts/`、`scripts/`、`config/`、`docs/`、`deployment/`、`firmware/`、`deliverables/`、`research/`、`.gitignore`、`AGENTS.md`、`README.md`，排除 `build/` 与只读 `STM32F4xx_HAL_Code/`），本轮为 549 文件 / 26,717,272 B，tar sha256=`8f6525c9…36cf`，逐文件清单 sha256=`6727b61c…e873`。
2. `scp` 到 VMware guest，解包到**本地 ext4** 的 `$HOME/locker-workspace/S1-01/src`（**不要**用 `/mnt/hgfs` 共享盘），`sha256sum -c` 必须 549/549 OK 且 `SRC_STRAY_ARTIFACTS=0`。
3. 构建目录放 `$HOME/locker-workspace/build/S1-01/<run_id>`、日志放 `$HOME/locker-workspace/logs/S1-01/<run_id>`（AGENTS.md 纪律：src 只放源码；build/logs/artifacts 与 src 同级，收尾轮实际用的 run_id 是 `close-20261023-225902`）。
4. 在 src 上执行上面的 cmake 三步，结果应与第 4 层日志一致。

**脱敏回归的复现**（整改验证，见同目录）：

```sh
# B1/B2 现象探针：必须全部归零
gcc -std=c11 -Wall -Wextra -Wpedantic -Werror -Ihost/common/include -Icontracts/include \
    docs/evidence/stage-01/S1-01/probe-b1-b2.c host/common/src/log.c host/common/src/clock.c -lpthread -o /tmp/probe
/tmp/probe        # 期望 B1_BEARER note="Bearer [redacted]"、B2_17TH_LEAK=0、PROBE_EXIT=0

# B1-R/B3 现象探针（第二轮审计）：必须 LEAK_CASES=0
gcc -std=c11 -Wall -Wextra -Wpedantic -Werror -Ihost/common/include -Icontracts/include \
    docs/evidence/stage-01/S1-01/probe-boundary.c host/common/src/log.c host/common/src/clock.c -lpthread -o /tmp/probe_boundary
/tmp/probe_boundary   # 期望 LONG_KEY_SENSITIVE=1、LEAK_CASES=0、PROBE_EXIT=0

# 非空洞性（第一轮 B1/B2）：故意把两处缺陷改回去，新测试必须失败
python3 docs/evidence/stage-01/S1-01/nonvacuity-mutate.py
# 期望 MUTATED_TEST_LOG_EXIT=1，7 of 133 checks failed

# 非空洞性（第二轮 B1-R/B3）
python3 docs/evidence/stage-01/S1-01/nonvacuity-mutate-r2.py
# 期望 MUTATED_TEST_LOG_EXIT=1，12 of 164 checks failed
```

---

## 10. 失败路径速查（排障）

| 现象 | 原因 | 处理 |
|---|---|---|
| configure 报 `Generated contract header not found` | `contracts/include/locker_contracts.h` 缺失 | `python -B scripts/build_contracts.py` |
| configure 报 `Contract version drift: header=a source=b` | 生成头与契约源不同步（正是本门禁要拦的情况） | 重新生成契约头；**不要**改 CMakeLists 绕过 |
| configure 报 `Could not read LOCKER_CONTRACT_VERSION` | 头文件被手改/格式变了 | 重新生成，不要手改生成物 |
| `find_package(Threads REQUIRED)` 失败 | 缺 pthread 开发包 | 装 `build-essential`/`libc6-dev` |
| 链接期 `undefined reference to pthread_...` | 目标没链 `locker_common` | 链接 `locker_common`（它 PUBLIC 传播 `Threads::Threads`） |
| `try_push` 一直 `NO_SPACE` | 消费者没 drain，或容量设得太小 | 看 `get_stats().rejected_total`；默认 Core 输入队列 64（`internal-interfaces.md:14`） |
| 日志里出现明文秘密 | 秘密被放进了自由文本键，且不符合任何可识别形态 | 改用敏感键名；或调用方先 `locker_log_scrub` |
| sink 里出现乱码/半截行 | sink 把 `line` 存下来异步用，但没复制 | 见 6.4：`line` 是借用栈缓冲，异步 sink 必须复制 |
| 偶发崩溃/挂死在 destroy | destroy 与 push/pop 并发 | 见 2.3：先 join 再 destroy |

---

## 11. 未实现 / 未验证（如实列出，不冒充完成）

### 本卡范围内**未做**（属后续卡）

- 业务状态机、REST 路由、锁驱动、持久化：**未建**（本卡明令禁止越界）。
- 线程池、条件变量阻塞队列、事件循环：**未做**。队列是 try_ 语义，阻塞等待由上层实现。
- 日志落盘/轮转/异步 sink：**未做**，只有 sink 回调，落盘由调用方（S1-05 及以后）实现。
- 结构化 JSON 日志输出：**未做**，当前是 `key=value` 文本行。
- 指标/健康检查接口：**未做**。

### 未验证（NOT_RUN，不伪造 PASS）

- 整改后代码**已在** VMware 干净 guest 复验（`vm-final2-*.log` 15 份，含 configure/build/ctest/逐测试/ASan/TSan/fuzz/两轮 mutation/探针/设计检查）；收尾轮又对**当前快照**独立复核一次（`vm-close-*.log` 8 份，`SRC_FILES=549`、`SRC_STRAY_ARTIFACTS=0`、6/6 PASS、505 断言），故此项为已验证（限 x86_64 干净 Linux 主机侧）。`vm-*.log`（整改前）、`vm-final-*.log`（第一轮后）、`vm-final2-*.log`（第二轮后）保留为历史证据，不覆盖。
- VMware 的 Python 3.14.4 缺 pycparser，故 VM 内 `check_contracts` 的 `c_declaration_syntax` 为 `NOT_RUN (optional pycparser unavailable)`，与 Windows/WSL 的 `PASS (parse only)` 不同；整体 status 仍 PASS，差异如实保留（见 `vm-final2-design-checks.log`）。
- 依赖归档本轮**未重新下载**复验 SHA-256，只用已落盘元数据与许可原文核对。
- 未在 GCC 12 / Debian 12 / `cmake 3.16` 精确下限上实测；两个验证环境都是 Ubuntu 26.04 + GCC 15.2 + cmake 4.2.3。
- 未做 32 位 / 非 x86 平台验证（`size_t` 与 `uint64_t` 的宽度假设只在 64 位 Linux 上实测）。
- 时钟故障注入（`clock_gettime` 返回失败）**无自动化测试**；契约无错误通道，只补了文档（见 3.5）。
- `locker_logger_t` 跨线程并发写只跑了 TSan 下的既有测试，未做专门压力测试。
- 第三方库（SQLite/CivetWeb/cJSON/libsodium/OpenSSL）**未编译、未链接、未 vendor**，只有元数据与许可核验。
- Keil/ARMCC、Android、RK/NPU、UART/相机/锁真机：全部 NOT_RUN，不属本卡。

### 收尾时已处理的交接项（原"待集成负责人处理"，现均已同步）

- 默认入口状态：`README.md`、`docs/stages/README.md`、`docs/stages/stage-01-task-cards.md`、`AGENTS.md`、`docs/AI开发交接流程.md`、`docs/AI开发执行手册.md` 已统一为"S1-01 已完成（DONE）、下一卡 S1-02"（`audit.md` N2 关闭，N5 勘误）。
- `config/development/stage-plan.json`：S1-01 `status="DONE"`、`report="docs/evidence/stage-01/S1-01/summary.md"`、`active_task=null`、`recommended_next="S1-02"`、`activity="IMPLEMENTATION"`。
- `docs/ai-prompts/*`：因任务卡与手册正文变化，已运行 `python -B scripts/build_ai_execution_manual.py` 重新生成（180 份指令、`manifest.json` 指纹刷新），`--check` 通过。
- 阶段 1 `implementation_status="IN_PROGRESS"`、`gate_result="NOT_RUN"`：**本卡 DONE 不等于阶段 1 PASS**，S1-07 才是阶段 1 门禁。

---

## 12. 残留与边界

- 本轮临时文件（宿主 `%TEMP%\s1_01_*`、`s101_*`，WSL2 `/var/tmp/s101_*`，VMware guest `~/s1_01_*`、`~/s101_*`、`~/s1_01_clean`、`~/audit_clean.*`、旧 `~/work/locker/`）已清理；VMware 的 `~/locker-workspace/S1-01/` 作为本轮 VM 复验的工作区保留（非仓库内容），已在 `vm-migration-review.md` 说明。仓库内只留正式测试、证据日志、探针源码与 `source-manifest.txt`。
- 构建产物在 `build/`（`.gitignore` 已忽略），未入库。
- 未修改：`contracts/`、`scripts/`、`config/`、`firmware/`、`docs/stages/`、`docs/architecture/`、`deployment/firmware/`、`E:/equipment-vision`、原 HAL 与锁定 vendor。
- 未 commit、未 push、未生成 `audit.md`（`audit.md` 三轮均为审计方原文，本卡只读取不改写）；S1-01 已按证据标 DONE，**未把阶段 1 标 PASS**。
- 证据目录共 68 个文件，五组证据严格分开：WSL2 当前轮、VMware 第一轮整改前（`vm-*.log`）、VMware 第一轮整改后（`vm-final-*.log`）、VMware 第二轮整改后（`vm-final2-*.log`）、收尾轮 VM 复核（`vm-close-*.log`）。
