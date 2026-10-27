#define _POSIX_C_SOURCE 200809L

#include "test_support.h"

#include <string.h>

#include "locker/common/uuid.h"
#include "locker/storage/event_key.h"

static const char k_device[] = "0123456789abcdef01234567";
static const char k_epoch_text[] = "11111111-2222-4333-8444-555555555555";

static locker_uuid_t make_epoch(void)
{
    locker_uuid_t epoch;
    memset(&epoch, 0, sizeof(epoch));
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_uuid_parse(k_epoch_text, strlen(k_epoch_text), &epoch));
    return epoch;
}

static locker_event_key_t make_key(uint64_t boot_id, uint64_t event_seq)
{
    locker_event_key_t key;
    memset(&key, 0, sizeof(key));
    memcpy(key.device_id, k_device, sizeof(k_device));
    key.journal_epoch = make_epoch();
    key.boot_id = boot_id;
    key.event_seq = event_seq;
    return key;
}

static void test_build_shape(void)
{
    locker_event_key_t key = make_key(1u, 2u);
    char text[LOCKER_STORAGE_EVENT_KEY_TEXT_MAX];
    memset(text, 'x', sizeof(text));
    TEST_CHECK(locker_storage_event_key_build(&key, text, sizeof(text)));
    TEST_CHECK(strlen(text) == 103u);
    TEST_CHECK(strncmp(text, k_device, 24u) == 0);
    TEST_CHECK(text[24] == '/');
    TEST_CHECK(strncmp(text + 25, k_epoch_text, 36u) == 0);
    TEST_CHECK(text[61] == '/');
    /* boot_id/event_seq 必须 20 位补零，字节序才等于数值序。 */
    TEST_CHECK(strncmp(text + 62, "00000000000000000001", 20u) == 0);
    TEST_CHECK(text[82] == '/');
    TEST_CHECK(strncmp(text + 83, "00000000000000000002", 20u) == 0);
    TEST_CHECK(text[103] == '\0');

    char too_small[LOCKER_STORAGE_EVENT_KEY_TEXT_MAX - 1u];
    TEST_CHECK(!locker_storage_event_key_build(&key, too_small, sizeof(too_small)));
    TEST_CHECK(!locker_storage_event_key_build(&key, NULL, sizeof(text)));
    TEST_CHECK(!locker_storage_event_key_build(NULL, text, sizeof(text)));
}

static void test_build_rejects(void)
{
    char text[LOCKER_STORAGE_EVENT_KEY_TEXT_MAX];

    locker_event_key_t zero = make_key(0u, 0u);
    TEST_CHECK(!locker_storage_event_key_build(&zero, text, sizeof(text)));

    locker_event_key_t bad_device = make_key(1u, 1u);
    bad_device.device_id[0] = 'G';
    TEST_CHECK(!locker_storage_event_key_build(&bad_device, text, sizeof(text)));

    bad_device = make_key(1u, 1u);
    bad_device.device_id[0] = 'A';
    TEST_CHECK(!locker_storage_event_key_build(&bad_device, text, sizeof(text)));

    bad_device = make_key(1u, 1u);
    bad_device.device_id[24] = 'x';
    TEST_CHECK(!locker_storage_event_key_build(&bad_device, text, sizeof(text)));

    locker_event_key_t big = make_key(UINT64_MAX, UINT64_MAX);
    TEST_CHECK(locker_storage_event_key_build(&big, text, sizeof(text)));
    TEST_CHECK(strncmp(text + 62, "18446744073709551615", 20u) == 0);
    TEST_CHECK(strncmp(text + 83, "18446744073709551615", 20u) == 0);
}

static void test_parse_round_trip(void)
{
    locker_event_key_t key = make_key(7u, 4294967296ull);
    char text[LOCKER_STORAGE_EVENT_KEY_TEXT_MAX];
    TEST_CHECK(locker_storage_event_key_build(&key, text, sizeof(text)));

    locker_event_key_t parsed;
    memset(&parsed, 0xff, sizeof(parsed));
    TEST_CHECK(locker_storage_event_key_parse(text, &parsed));
    TEST_CHECK(strcmp(parsed.device_id, k_device) == 0);
    TEST_CHECK(locker_uuid_equal(&parsed.journal_epoch, &key.journal_epoch));
    TEST_CHECK_U64(7u, parsed.boot_id);
    TEST_CHECK_U64(4294967296ull, parsed.event_seq);

    TEST_CHECK(!locker_storage_event_key_parse(text, NULL));
    TEST_CHECK(!locker_storage_event_key_parse(NULL, &parsed));

    /* 长度、分隔符、字段字符集全部严格校验，不做宽松解析。 */
    char broken[LOCKER_STORAGE_EVENT_KEY_TEXT_MAX];
    memcpy(broken, text, 104u);
    broken[102] = 'x';
    TEST_CHECK(!locker_storage_event_key_parse(broken, &parsed));
    memcpy(broken, text, 104u);
    broken[24] = '-';
    TEST_CHECK(!locker_storage_event_key_parse(broken, &parsed));
    memcpy(broken, text, 104u);
    broken[62] = ' ';
    TEST_CHECK(!locker_storage_event_key_parse(broken, &parsed));
    memcpy(broken, text, 104u);
    broken[62] = '0';
    broken[63] = '0';
    /* 20 位补零字段允许前导零，但不允许非数字。 */
    TEST_CHECK(locker_storage_event_key_parse(broken, &parsed));
    memcpy(broken, text, 104u);
    broken[83] = 'x';
    TEST_CHECK(!locker_storage_event_key_parse(broken, &parsed));
}

static void test_import_cursor_key(void)
{
    locker_event_key_t key = make_key(3u, 4u);
    char text[LOCKER_STORAGE_IMPORT_CURSOR_KEY_TEXT_MAX];
    memset(text, 'x', sizeof(text));
    TEST_CHECK(locker_storage_import_cursor_key_build(&key, text, sizeof(text)));
    TEST_CHECK(strlen(text) == 61u);
    TEST_CHECK(strncmp(text, k_device, 24u) == 0);
    TEST_CHECK(text[24] == '/');
    TEST_CHECK(strncmp(text + 25, k_epoch_text, 36u) == 0);

    char too_small[LOCKER_STORAGE_IMPORT_CURSOR_KEY_TEXT_MAX - 1u];
    TEST_CHECK(!locker_storage_import_cursor_key_build(&key, too_small, sizeof(too_small)));

    locker_event_key_t other = make_key(9u, 9u);
    char other_text[LOCKER_STORAGE_IMPORT_CURSOR_KEY_TEXT_MAX];
    TEST_CHECK(locker_storage_import_cursor_key_build(&other, other_text, sizeof(other_text)));
    /* 同一 device+epoch 的不同事件共用一个游标 key。 */
    TEST_CHECK_STRING(text, other_text);
}

static void test_is_zero(void)
{
    locker_event_key_t key = make_key(0u, 0u);
    TEST_CHECK(locker_storage_event_key_is_zero(&key));
    key.boot_id = 1u;
    TEST_CHECK(!locker_storage_event_key_is_zero(&key));
    key.boot_id = 0u;
    key.event_seq = 1u;
    TEST_CHECK(!locker_storage_event_key_is_zero(&key));
    TEST_CHECK(!locker_storage_event_key_is_zero(NULL));
}

static void test_compare_numeric_order(void)
{
    /* 2 必须排在 10 之前：这是游标「不能把 10 排在 2 前」的硬约束。 */
    locker_event_key_t two = make_key(1u, 2u);
    locker_event_key_t ten = make_key(1u, 10u);
    TEST_CHECK(locker_storage_event_key_compare(&two, &ten) < 0);
    TEST_CHECK(locker_storage_event_key_compare(&ten, &two) > 0);
    TEST_CHECK(locker_storage_event_key_compare(&two, &two) == 0);

    locker_event_key_t boot_two = make_key(2u, 1u);
    locker_event_key_t boot_ten = make_key(10u, 1u);
    TEST_CHECK(locker_storage_event_key_compare(&boot_two, &boot_ten) < 0);

    locker_event_key_t other_device = make_key(1u, 1u);
    other_device.device_id[23] = '8';
    TEST_CHECK(locker_storage_event_key_compare(&two, &other_device) < 0);

    TEST_CHECK(locker_storage_event_key_compare(NULL, NULL) == 0);
    TEST_CHECK(locker_storage_event_key_compare(NULL, &two) < 0);
    TEST_CHECK(locker_storage_event_key_compare(&two, NULL) > 0);

    /* 文档 key 的字节序必须与数值序一致（20 位补零的意义）。 */
    char two_text[LOCKER_STORAGE_EVENT_KEY_TEXT_MAX];
    char ten_text[LOCKER_STORAGE_EVENT_KEY_TEXT_MAX];
    TEST_CHECK(locker_storage_event_key_build(&two, two_text, sizeof(two_text)));
    TEST_CHECK(locker_storage_event_key_build(&ten, ten_text, sizeof(ten_text)));
    TEST_CHECK(strcmp(two_text, ten_text) < 0);
}

static void test_next_seq(void)
{
    locker_event_key_t key = make_key(1u, 2u);
    locker_event_key_t next;
    TEST_CHECK(locker_storage_event_key_next_seq(&key, &next));
    TEST_CHECK_U64(3u, next.event_seq);
    TEST_CHECK_U64(1u, next.boot_id);
    TEST_CHECK(strcmp(next.device_id, k_device) == 0);

    locker_event_key_t max = make_key(1u, UINT64_MAX);
    TEST_CHECK(!locker_storage_event_key_next_seq(&max, &next));
    TEST_CHECK(!locker_storage_event_key_next_seq(&key, NULL));
    TEST_CHECK(!locker_storage_event_key_next_seq(NULL, &next));
}

int main(void)
{
    test_build_shape();
    test_build_rejects();
    test_parse_round_trip();
    test_import_cursor_key();
    test_is_zero();
    test_compare_numeric_order();
    test_next_seq();
    TEST_REPORT("storage.event_key");
}
