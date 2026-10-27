#define _POSIX_C_SOURCE 200809L

/*
 * S1-04 单元测试：文本/枚举/时间/摘要这些「纯函数」。
 *
 * 这一组不碰 Core 状态机，只钉死三件容易被悄悄放宽的事：
 *   1. 枚举名是存储层 JSON 的字面量（internal-data.schema.json 里 kind/state/door/item
 *      都是固定字符串），改名字等于改库格式，必须在此变红。
 *   2. U64 与时间戳的文本形式（U64 必须是十进制字符串、时间必须是 RFC 3339 'Z'）。
 *   3. state rank 单调：迟到弱事实不得回退强事实（domain_text.c:302 的注释）。
 */

/* domain_test_support.h 已包含 domain_internal.h 与 test_support.h：这里只用它的 dt_uuid_fill。 */
#include "domain_test_support.h"
#include "domain_sha256.h"
#include "domain_time.h"

#include <string.h>

static void test_enum_names(void)
{
    /* 状态名：9 个，顺序即契约 locker_operation_state_t 的序号。 */
    TEST_CHECK_STRING("REQUESTED", domain_state_name(LOCKER_OPERATION_STATE_REQUESTED));
    TEST_CHECK_STRING("ACCESS_PENDING", domain_state_name(LOCKER_OPERATION_STATE_ACCESS_PENDING));
    TEST_CHECK_STRING("ACCESS_ACTIVE", domain_state_name(LOCKER_OPERATION_STATE_ACCESS_ACTIVE));
    TEST_CHECK_STRING("WAIT_CONFIRM", domain_state_name(LOCKER_OPERATION_STATE_WAIT_CONFIRM));
    TEST_CHECK_STRING("REVIEW_REQUIRED", domain_state_name(LOCKER_OPERATION_STATE_REVIEW_REQUIRED));
    TEST_CHECK_STRING("COMPLETED", domain_state_name(LOCKER_OPERATION_STATE_COMPLETED));
    TEST_CHECK_STRING("CANCELLED", domain_state_name(LOCKER_OPERATION_STATE_CANCELLED));
    TEST_CHECK_STRING("NO_CHANGE", domain_state_name(LOCKER_OPERATION_STATE_NO_CHANGE));
    TEST_CHECK_STRING("FAILED", domain_state_name(LOCKER_OPERATION_STATE_FAILED));
    /* 越界返回 NULL，绝不返回 "UNKNOWN" 之类可被误写入库的兜底串。 */
    TEST_CHECK(domain_state_name(9u) == NULL);
    TEST_CHECK(domain_state_name(255u) == NULL);

    TEST_CHECK_STRING("BORROW", domain_kind_name(LOCKER_OPERATION_KIND_BORROW));
    TEST_CHECK_STRING("RETURN", domain_kind_name(LOCKER_OPERATION_KIND_RETURN));
    TEST_CHECK(domain_kind_name(0u) == NULL);

    TEST_CHECK_STRING("USER", domain_role_name(LOCKER_ROLE_USER));
    TEST_CHECK_STRING("ADMIN", domain_role_name(LOCKER_ROLE_ADMIN));
    TEST_CHECK(domain_role_name(2u) == NULL);

    TEST_CHECK_STRING("UNKNOWN", domain_door_name(LOCKER_DOOR_STATE_UNKNOWN));
    TEST_CHECK_STRING("CLOSED", domain_door_name(LOCKER_DOOR_STATE_CLOSED));
    TEST_CHECK_STRING("OPEN", domain_door_name(LOCKER_DOOR_STATE_OPEN));
    TEST_CHECK(domain_door_name(3u) == NULL);

    TEST_CHECK_STRING("UNKNOWN", domain_item_name(LOCKER_ITEM_STATE_UNKNOWN));
    TEST_CHECK_STRING("ABSENT", domain_item_name(LOCKER_ITEM_STATE_ABSENT));
    TEST_CHECK_STRING("PRESENT", domain_item_name(LOCKER_ITEM_STATE_PRESENT));
    TEST_CHECK(domain_item_name(3u) == NULL);

    TEST_CHECK_STRING("OFFLINE", domain_mode_name(LOCKER_DEVICE_MODE_OFFLINE));
    TEST_CHECK_STRING("RECONCILING", domain_mode_name(LOCKER_DEVICE_MODE_RECONCILING));
    TEST_CHECK_STRING("READY", domain_mode_name(LOCKER_DEVICE_MODE_READY));
    TEST_CHECK_STRING("DEGRADED", domain_mode_name(LOCKER_DEVICE_MODE_DEGRADED));
    TEST_CHECK(domain_mode_name(4u) == NULL);

    TEST_CHECK_STRING("UNSYNCED", domain_time_quality_name(LOCKER_TIME_QUALITY_UNSYNCED));
    TEST_CHECK_STRING("ESTIMATED", domain_time_quality_name(LOCKER_TIME_QUALITY_ESTIMATED));
    TEST_CHECK_STRING("HOST_SYNCED", domain_time_quality_name(LOCKER_TIME_QUALITY_HOST_SYNCED));
    TEST_CHECK(domain_time_quality_name(3u) == NULL);

    TEST_CHECK_STRING("PRESENT_AT_CABINET", domain_action_name(LOCKER_ALLOWED_ACTION_PRESENT_AT_CABINET));
    TEST_CHECK_STRING("WAIT", domain_action_name(LOCKER_ALLOWED_ACTION_WAIT));
    TEST_CHECK_STRING("CONFIRM", domain_action_name(LOCKER_ALLOWED_ACTION_CONFIRM));
    TEST_CHECK_STRING("CANCEL", domain_action_name(LOCKER_ALLOWED_ACTION_CANCEL));
    TEST_CHECK_STRING("ADMIN_REVIEW", domain_action_name(LOCKER_ALLOWED_ACTION_ADMIN_REVIEW));
    TEST_CHECK(domain_action_name(5u) == NULL);

    /* 证明来源：0 是「没有证明」而不是任何字符串。 */
    TEST_CHECK(domain_proof_name(0u) == NULL);
    TEST_CHECK_STRING("CARD", domain_proof_name(1u));
    TEST_CHECK_STRING("LOCAL_UI", domain_proof_name(2u));
    TEST_CHECK(domain_proof_name(3u) == NULL);

    TEST_CHECK_STRING("PREPARED", domain_outbox_state_name(1u));
    TEST_CHECK_STRING("SEND_ATTEMPTED", domain_outbox_state_name(2u));
    TEST_CHECK_STRING("ACCEPTED", domain_outbox_state_name(3u));
    TEST_CHECK_STRING("UNRESOLVED", domain_outbox_state_name(4u));
    TEST_CHECK_STRING("FINISHED", domain_outbox_state_name(5u));
    TEST_CHECK_STRING("REJECTED", domain_outbox_state_name(6u));
    TEST_CHECK(domain_outbox_state_name(0u) == NULL);
    TEST_CHECK(domain_outbox_state_name(7u) == NULL);

    TEST_CHECK_STRING("AVAILABLE", domain_availability_name(LOCKER_TOOL_AVAILABILITY_AVAILABLE));
    TEST_CHECK_STRING("RESERVED", domain_availability_name(LOCKER_TOOL_AVAILABILITY_RESERVED));
    TEST_CHECK_STRING("BORROWED", domain_availability_name(LOCKER_TOOL_AVAILABILITY_BORROWED));
    TEST_CHECK_STRING("REVIEW", domain_availability_name(LOCKER_TOOL_AVAILABILITY_REVIEW));
    TEST_CHECK_STRING("UNAVAILABLE", domain_availability_name(LOCKER_TOOL_AVAILABILITY_UNAVAILABLE));
    TEST_CHECK(domain_availability_name(5u) == NULL);
}

static void test_enum_parsing(void)
{
    uint8_t value = 0xeeu;

    TEST_CHECK(domain_state_from_name("WAIT_CONFIRM", strlen("WAIT_CONFIRM"), &value));
    TEST_CHECK_U64(LOCKER_OPERATION_STATE_WAIT_CONFIRM, value);
    TEST_CHECK(domain_state_from_name("REVIEW_REQUIRED", strlen("REVIEW_REQUIRED"), &value));
    TEST_CHECK_U64(LOCKER_OPERATION_STATE_REVIEW_REQUIRED, value);
    /* 前缀不是名字：长度必须完全相等。 */
    TEST_CHECK(!domain_state_from_name("WAIT", strlen("WAIT"), &value));
    TEST_CHECK(!domain_state_from_name("WAIT_CONFIRM_X", strlen("WAIT_CONFIRM_X"), &value));
    /* 大小写敏感：库里写小写就是坏数据，不能宽容接受。 */
    TEST_CHECK(!domain_state_from_name("wait_confirm", strlen("wait_confirm"), &value));
    TEST_CHECK(!domain_state_from_name(NULL, 0u, &value));
    TEST_CHECK(!domain_state_from_name("REQUESTED", strlen("REQUESTED"), NULL));

    TEST_CHECK(domain_kind_from_name("BORROW", strlen("BORROW"), &value));
    TEST_CHECK_U64(LOCKER_OPERATION_KIND_BORROW, value);
    TEST_CHECK(domain_kind_from_name("RETURN", strlen("RETURN"), &value));
    TEST_CHECK_U64(LOCKER_OPERATION_KIND_RETURN, value);
    TEST_CHECK(!domain_kind_from_name("BORROWED", strlen("BORROWED"), &value));

    TEST_CHECK(domain_role_from_name("ADMIN", strlen("ADMIN"), &value));
    TEST_CHECK_U64(LOCKER_ROLE_ADMIN, value);
    TEST_CHECK(!domain_role_from_name("ROOT", strlen("ROOT"), &value));

    TEST_CHECK(domain_resolution_from_name("COMPLETE_BORROW", strlen("COMPLETE_BORROW"), &value));
    TEST_CHECK_U64(LOCKER_RESOLUTION_COMPLETE_BORROW, value);
    TEST_CHECK(domain_resolution_from_name("COMPLETE_RETURN", strlen("COMPLETE_RETURN"), &value));
    TEST_CHECK_U64(LOCKER_RESOLUTION_COMPLETE_RETURN, value);
    TEST_CHECK(domain_resolution_from_name("NO_CHANGE", strlen("NO_CHANGE"), &value));
    TEST_CHECK_U64(LOCKER_RESOLUTION_NO_CHANGE, value);
    TEST_CHECK(!domain_resolution_from_name("COMPLETE", strlen("COMPLETE"), &value));
}

static void test_state_rank(void)
{
    uint8_t rank = 0u;
    static const uint8_t k_order[] = {
        LOCKER_OPERATION_STATE_REQUESTED, LOCKER_OPERATION_STATE_ACCESS_PENDING,
        LOCKER_OPERATION_STATE_ACCESS_ACTIVE, LOCKER_OPERATION_STATE_WAIT_CONFIRM,
        LOCKER_OPERATION_STATE_REVIEW_REQUIRED,
    };
    uint32_t i;

    /* 越强的事实在后面：迟到弱事实不得回退强事实（domain_text.c:302）。 */
    for (i = 0u; i < sizeof(k_order); i++) {
        uint8_t lower = 0u;
        TEST_CHECK(domain_state_rank(k_order[i], &rank));
        TEST_CHECK_U64(i + 1u, rank);
        if (i > 0u) {
            TEST_CHECK(domain_state_rank(k_order[i - 1u], &lower));
            TEST_CHECK(rank > lower);
        }
    }
    /* 四个终态同级且最强。 */
    TEST_CHECK(domain_state_rank(LOCKER_OPERATION_STATE_COMPLETED, &rank));
    TEST_CHECK_U64(6u, rank);
    TEST_CHECK(domain_state_rank(LOCKER_OPERATION_STATE_CANCELLED, &rank));
    TEST_CHECK_U64(6u, rank);
    TEST_CHECK(domain_state_rank(LOCKER_OPERATION_STATE_NO_CHANGE, &rank));
    TEST_CHECK_U64(6u, rank);
    TEST_CHECK(domain_state_rank(LOCKER_OPERATION_STATE_FAILED, &rank));
    TEST_CHECK_U64(6u, rank);
    /* 未知状态没有 rank，调用方必须自己 fail closed。 */
    TEST_CHECK(!domain_state_rank(9u, &rank));
    TEST_CHECK(!domain_state_rank(0u, NULL));

    TEST_CHECK(domain_terminal_state(LOCKER_OPERATION_STATE_COMPLETED));
    TEST_CHECK(domain_terminal_state(LOCKER_OPERATION_STATE_CANCELLED));
    TEST_CHECK(domain_terminal_state(LOCKER_OPERATION_STATE_NO_CHANGE));
    TEST_CHECK(domain_terminal_state(LOCKER_OPERATION_STATE_FAILED));
    TEST_CHECK(!domain_terminal_state(LOCKER_OPERATION_STATE_REQUESTED));
    TEST_CHECK(!domain_terminal_state(LOCKER_OPERATION_STATE_ACCESS_PENDING));
    TEST_CHECK(!domain_terminal_state(LOCKER_OPERATION_STATE_ACCESS_ACTIVE));
    TEST_CHECK(!domain_terminal_state(LOCKER_OPERATION_STATE_WAIT_CONFIRM));
    /* REVIEW_REQUIRED 不是终态：它还占着柜格（x-rules 第 6 条）。 */
    TEST_CHECK(!domain_terminal_state(LOCKER_OPERATION_STATE_REVIEW_REQUIRED));

    /* 线上 rank（wire 侧只有 1..5）映射到本地状态；越界一律进核验，不放行。 */
    TEST_CHECK_U64(LOCKER_OPERATION_STATE_REQUESTED, domain_state_from_wire_rank(1u));
    TEST_CHECK_U64(LOCKER_OPERATION_STATE_ACCESS_PENDING, domain_state_from_wire_rank(2u));
    TEST_CHECK_U64(LOCKER_OPERATION_STATE_ACCESS_ACTIVE, domain_state_from_wire_rank(3u));
    TEST_CHECK_U64(LOCKER_OPERATION_STATE_WAIT_CONFIRM, domain_state_from_wire_rank(4u));
    TEST_CHECK_U64(LOCKER_OPERATION_STATE_REVIEW_REQUIRED, domain_state_from_wire_rank(5u));
    TEST_CHECK_U64(LOCKER_OPERATION_STATE_REVIEW_REQUIRED, domain_state_from_wire_rank(0u));
    TEST_CHECK_U64(LOCKER_OPERATION_STATE_REVIEW_REQUIRED, domain_state_from_wire_rank(6u));
}

static void test_text_helpers(void)
{
    char buffer[8];

    TEST_CHECK_U64(3u, domain_strnlen("abc", 8u));
    /* 有界：不含 NUL 的缓冲只数到 limit。 */
    TEST_CHECK_U64(2u, domain_strnlen("abc", 2u));
    TEST_CHECK_U64(0u, domain_strnlen(NULL, 8u));
    TEST_CHECK_U64(0u, domain_strnlen("abc", 0u));

    memset(buffer, 0x7f, sizeof(buffer));
    TEST_CHECK(domain_text_copy(buffer, sizeof(buffer), "abc"));
    TEST_CHECK_STRING("abc", buffer);
    /* 装不下就整体失败，绝不截断成另一个合法文本。 */
    memset(buffer, 0x7f, sizeof(buffer));
    TEST_CHECK(!domain_text_copy(buffer, 3u, "abc"));
    TEST_CHECK_U64(0x7fu, (uint8_t)buffer[0]);
    TEST_CHECK(!domain_text_copy(buffer, sizeof(buffer), NULL));
    TEST_CHECK(!domain_text_copy(NULL, sizeof(buffer), "abc"));

    TEST_CHECK(domain_text_equal("abc", "abc"));
    TEST_CHECK(!domain_text_equal("abc", "abd"));
    TEST_CHECK(!domain_text_equal("abc", "ab"));
    TEST_CHECK(!domain_text_equal(NULL, "abc"));
    TEST_CHECK(!domain_text_equal("abc", NULL));
    /* 两个 NULL 不算相等：调用方不能靠它把缺失当匹配。 */
    TEST_CHECK(!domain_text_equal(NULL, NULL));
}

static void test_uuid_text(void)
{
    locker_uuid_t uuid;
    locker_uuid_t parsed;
    char text[LOCKER_DOMAIN_UUID_TEXT];

    dt_uuid_fill(&uuid, 0x11u);
    TEST_CHECK(domain_uuid_to_text(&uuid, text, sizeof(text)));
    TEST_CHECK_U64(36u, strlen(text));
    TEST_CHECK(text[8] == '-' && text[13] == '-' && text[18] == '-' && text[23] == '-');
    TEST_CHECK(domain_uuid_from_text(text, strlen(text), &parsed));
    TEST_CHECK(locker_uuid_equal(&uuid, &parsed));

    /* 缓冲区不够就失败，不写半个 UUID。 */
    TEST_CHECK(!domain_uuid_to_text(&uuid, text, 36u));
    TEST_CHECK(!domain_uuid_to_text(NULL, text, sizeof(text)));

    /* 非法文本一律拒绝。 */
    TEST_CHECK(!domain_uuid_from_text("not-a-uuid", strlen("not-a-uuid"), &parsed));
    TEST_CHECK(!domain_uuid_from_text(text, 35u, &parsed));
    TEST_CHECK(!domain_uuid_from_text(NULL, 0u, &parsed));
    TEST_CHECK(!domain_uuid_from_text(text, strlen(text), NULL));

    TEST_CHECK(domain_uuid_text_valid(text, strlen(text)));
    TEST_CHECK(!domain_uuid_text_valid(text, 35u));
    TEST_CHECK(!domain_uuid_text_valid(NULL, 36u));

    /* 全零 UUID 是「没有」的哨兵，不能被当成有效标识。 */
    locker_uuid_clear(&uuid);
    TEST_CHECK(locker_uuid_is_nil(&uuid));
    TEST_CHECK(domain_uuid_to_text(&uuid, text, sizeof(text)));
    TEST_CHECK_STRING("00000000-0000-0000-0000-000000000000", text);
}

static void test_time_format(void)
{
    char text[LOCKER_DOMAIN_TIMESTAMP_TEXT];
    uint64_t wall = 0u;

    /* 1970-01-01T00:00:00Z */
    TEST_CHECK(locker_domain_time_format_utc(0u, text, sizeof(text)));
    TEST_CHECK_STRING("1970-01-01T00:00:00Z", text);

    /* 2025-10-21T00:00:00Z：契约样例时间戳。 */
    TEST_CHECK(locker_domain_time_parse_utc("2025-10-21T00:00:00Z", strlen("2025-10-21T00:00:00Z"), &wall));
    TEST_CHECK(locker_domain_time_format_utc(wall, text, sizeof(text)));
    TEST_CHECK_STRING("2025-10-21T00:00:00Z", text);

    /* 毫秒被截到秒：格式串只有秒级精度。 */
    TEST_CHECK(locker_domain_time_format_utc(1761004800999ull, text, sizeof(text)));
    TEST_CHECK_STRING("2025-10-21T00:00:00Z", text);

    /* 闰日与年末边界。 */
    TEST_CHECK(locker_domain_time_parse_utc("2024-02-29T23:59:59Z", strlen("2024-02-29T23:59:59Z"), &wall));
    TEST_CHECK(locker_domain_time_format_utc(wall, text, sizeof(text)));
    TEST_CHECK_STRING("2024-02-29T23:59:59Z", text);
    TEST_CHECK(locker_domain_time_parse_utc("2026-12-31T12:34:56Z", strlen("2026-12-31T12:34:56Z"), &wall));
    TEST_CHECK(locker_domain_time_format_utc(wall, text, sizeof(text)));
    TEST_CHECK_STRING("2026-12-31T12:34:56Z", text);

    /* 输出缓冲不足 21 字节直接失败。 */
    TEST_CHECK(!locker_domain_time_format_utc(0u, text, 20u));
    TEST_CHECK(!locker_domain_time_format_utc(0u, NULL, sizeof(text)));
}

static void test_time_parse_rejects(void)
{
    uint64_t wall = 0u;

    /* 分隔符位置固定。 */
    TEST_CHECK(!locker_domain_time_parse_utc("2025/10/21T00:00:00Z", 20u, &wall));
    TEST_CHECK(!locker_domain_time_parse_utc("2025-10-21 00:00:00Z", 20u, &wall));
    /* 范围越界。 */
    TEST_CHECK(!locker_domain_time_parse_utc("2025-13-01T00:00:00Z", 20u, &wall));
    TEST_CHECK(!locker_domain_time_parse_utc("2025-00-01T00:00:00Z", 20u, &wall));
    TEST_CHECK(!locker_domain_time_parse_utc("2025-10-32T00:00:00Z", 20u, &wall));
    TEST_CHECK(!locker_domain_time_parse_utc("2025-02-29T00:00:00Z", 20u, &wall));
    TEST_CHECK(!locker_domain_time_parse_utc("2025-10-21T24:00:00Z", 20u, &wall));
    TEST_CHECK(!locker_domain_time_parse_utc("2025-10-21T00:60:00Z", 20u, &wall));
    TEST_CHECK(!locker_domain_time_parse_utc("2025-10-21T00:00:60Z", 20u, &wall));
    TEST_CHECK(!locker_domain_time_parse_utc("0000-01-01T00:00:00Z", 20u, &wall));
    /* 非数字。 */
    TEST_CHECK(!locker_domain_time_parse_utc("20x5-10-21T00:00:00Z", 20u, &wall));
    /* 尾随垃圾。 */
    TEST_CHECK(!locker_domain_time_parse_utc("2025-10-21T00:00:00Z ", 21u, &wall));
    TEST_CHECK(!locker_domain_time_parse_utc("2025-10-21T00:00:00ZZ", 21u, &wall));
    /* 时区偏移必须完整。 */
    TEST_CHECK(!locker_domain_time_parse_utc("2025-10-21T00:00:00+08", 22u, &wall));
    TEST_CHECK(!locker_domain_time_parse_utc("2025-10-21T00:00:00+24:00", 25u, &wall));
    /* 太短 / NULL。 */
    TEST_CHECK(!locker_domain_time_parse_utc("2025-10-21", 10u, &wall));
    TEST_CHECK(!locker_domain_time_parse_utc(NULL, 20u, &wall));
    TEST_CHECK(!locker_domain_time_parse_utc("2025-10-21T00:00:00Z", 20u, NULL));
}

static void test_time_parse_offsets(void)
{
    uint64_t utc = 0u;
    uint64_t zulu = 0u;

    /* 同一时刻的三种写法必须解出同一个 UTC 毫秒。 */
    TEST_CHECK(locker_domain_time_parse_utc("2025-10-21T00:00:00Z", 20u, &zulu));
    TEST_CHECK(locker_domain_time_parse_utc("2025-10-21T08:00:00+08:00", 25u, &utc));
    TEST_CHECK_U64(zulu, utc);
    TEST_CHECK(locker_domain_time_parse_utc("2025-10-20T16:00:00-08:00", 25u, &utc));
    TEST_CHECK_U64(zulu, utc);
    /* 小写 z 也接受（外部写入的会话过期时间可能是小写）。 */
    TEST_CHECK(locker_domain_time_parse_utc("2025-10-21T00:00:00z", 20u, &utc));
    TEST_CHECK_U64(zulu, utc);
    /* 小数秒最多 3 位，且不影响秒级结果。 */
    TEST_CHECK(locker_domain_time_parse_utc("2025-10-21T00:00:00.123Z", 24u, &utc));
    TEST_CHECK_U64(zulu + 123u, utc);
    TEST_CHECK(locker_domain_time_parse_utc("2025-10-21T00:00:00.1Z", 22u, &utc));
    TEST_CHECK_U64(zulu + 100u, utc);
    /* 小数点后必须有数字。 */
    TEST_CHECK(!locker_domain_time_parse_utc("2025-10-21T00:00:00.Z", 21u, &utc));
}

static void test_sha256(void)
{
    char hex[65];
    uint8_t digest[32];

    /* FIPS 180-4 标准向量，防止自实现的填充/字节序悄悄出错。 */
    TEST_CHECK(locker_domain_sha256_hex("", 0u, hex, sizeof(hex)));
    TEST_CHECK_STRING("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", hex);
    TEST_CHECK(locker_domain_sha256_hex("abc", 3u, hex, sizeof(hex)));
    TEST_CHECK_STRING("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", hex);
    TEST_CHECK(locker_domain_sha256_hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", 56u, hex,
                                       sizeof(hex)));
    TEST_CHECK_STRING("248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1", hex);

    /* 流式与一次性必须一致（幂等哈希就是流式拼出来的）。 */
    locker_domain_sha256_t ctx;
    locker_domain_sha256_init(&ctx);
    locker_domain_sha256_update(&ctx, "ab", 2u);
    locker_domain_sha256_update(&ctx, "c", 1u);
    locker_domain_sha256_final(&ctx, digest);
    locker_domain_sha256("abc", 3u, digest);
    TEST_CHECK(locker_domain_sha256_hex("abc", 3u, hex, sizeof(hex)));
    TEST_CHECK_STRING("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", hex);

    /* 输出缓冲不足 65 字节失败。 */
    TEST_CHECK(!locker_domain_sha256_hex("abc", 3u, hex, 64u));
}

int main(void)
{
    test_enum_names();
    test_enum_parsing();
    test_state_rank();
    test_text_helpers();
    test_uuid_text();
    test_time_format();
    test_time_parse_rejects();
    test_time_parse_offsets();
    test_sha256();
    TEST_REPORT("domain.test_domain_text");
}
