#define _POSIX_C_SOURCE 200809L

#include "test_support.h"

#include <string.h>

#include "locker/storage/u64.h"

static void test_parse_accepts(void)
{
    uint64_t value = 123u;
    TEST_CHECK(locker_storage_u64_parse("0", 1u, &value));
    TEST_CHECK_U64(0u, value);
    TEST_CHECK(locker_storage_u64_parse("1", 1u, &value));
    TEST_CHECK_U64(1u, value);
    TEST_CHECK(locker_storage_u64_parse("10", 2u, &value));
    TEST_CHECK_U64(10u, value);
    TEST_CHECK(locker_storage_u64_parse("18446744073709551615", 20u, &value));
    TEST_CHECK_U64(UINT64_MAX, value);
    TEST_CHECK(locker_storage_u64_parse("18446744073709551614", 20u, &value));
    TEST_CHECK_U64(UINT64_MAX - 1u, value);
    TEST_CHECK(locker_storage_u64_parse("9223372036854775808", 19u, &value));
    TEST_CHECK_U64(9223372036854775808ull, value);
}

static void test_parse_rejects(void)
{
    uint64_t value = 7u;
    TEST_CHECK(!locker_storage_u64_parse("", 0u, &value));
    TEST_CHECK_U64(7u, value);
    TEST_CHECK(!locker_storage_u64_parse("00", 2u, &value));
    TEST_CHECK(!locker_storage_u64_parse("01", 2u, &value));
    TEST_CHECK(!locker_storage_u64_parse("007", 3u, &value));
    TEST_CHECK(!locker_storage_u64_parse("-1", 2u, &value));
    TEST_CHECK(!locker_storage_u64_parse("+1", 2u, &value));
    TEST_CHECK(!locker_storage_u64_parse(" 1", 2u, &value));
    TEST_CHECK(!locker_storage_u64_parse("1 ", 2u, &value));
    TEST_CHECK(!locker_storage_u64_parse("1.0", 3u, &value));
    TEST_CHECK(!locker_storage_u64_parse("0x10", 4u, &value));
    TEST_CHECK(!locker_storage_u64_parse("1a", 2u, &value));
    TEST_CHECK(!locker_storage_u64_parse("a", 1u, &value));
    TEST_CHECK(!locker_storage_u64_parse("18446744073709551616", 20u, &value));
    TEST_CHECK(!locker_storage_u64_parse("99999999999999999999", 20u, &value));
    TEST_CHECK(!locker_storage_u64_parse("184467440737095516150", 21u, &value));
    TEST_CHECK(!locker_storage_u64_parse("0", 1u, NULL));
    TEST_CHECK(!locker_storage_u64_parse(NULL, 1u, &value));
    TEST_CHECK_U64(7u, value);
}

static void test_format(void)
{
    char text[32];
    memset(text, 'x', sizeof(text));
    TEST_CHECK(locker_storage_u64_format(0u, text, sizeof(text)));
    TEST_CHECK_STRING("0", text);
    TEST_CHECK(locker_storage_u64_format(1u, text, sizeof(text)));
    TEST_CHECK_STRING("1", text);
    TEST_CHECK(locker_storage_u64_format(42u, text, sizeof(text)));
    TEST_CHECK_STRING("42", text);
    TEST_CHECK(locker_storage_u64_format(UINT64_MAX, text, sizeof(text)));
    TEST_CHECK_STRING("18446744073709551615", text);
    TEST_CHECK(strlen(text) == 20u);

    char tight[21];
    TEST_CHECK(locker_storage_u64_format(UINT64_MAX, tight, sizeof(tight)));
    TEST_CHECK_STRING("18446744073709551615", tight);

    char too_small[20];
    TEST_CHECK(!locker_storage_u64_format(UINT64_MAX, too_small, sizeof(too_small)));
    TEST_CHECK(!locker_storage_u64_format(1u, tight, 1u));
    TEST_CHECK(!locker_storage_u64_format(1u, NULL, sizeof(tight)));
}

static void test_compare_text(void)
{
    /* 2 必须排在 10 之前：这是事件键与游标排序的硬要求（x-rules 12）。 */
    TEST_CHECK(locker_storage_u64_compare_text("2", "10") < 0);
    TEST_CHECK(locker_storage_u64_compare_text("10", "2") > 0);
    TEST_CHECK(locker_storage_u64_compare_text("9", "10") < 0);
    TEST_CHECK(locker_storage_u64_compare_text("10", "10") == 0);
    TEST_CHECK(locker_storage_u64_compare_text("0", "0") == 0);
    TEST_CHECK(locker_storage_u64_compare_text("0", "1") < 0);
    TEST_CHECK(locker_storage_u64_compare_text("18446744073709551615", "18446744073709551614") > 0);
    TEST_CHECK(locker_storage_u64_compare_text("18446744073709551615", "18446744073709551615") == 0);
    TEST_CHECK(locker_storage_u64_compare_text("007", "7") == 0);
    TEST_CHECK(locker_storage_u64_compare_text("000", "0") == 0);
    TEST_CHECK(locker_storage_u64_compare_text("00000000000000000000001", "1") == 0);
    TEST_CHECK(locker_storage_u64_compare_text("1234567890123456789", "12345678901234567890") < 0);
}

/* 非规范文本（不是已校验的十进制）也会被比较函数遇到：仓储内部用它给
   「不该出现」的文本一个确定性顺序，绝不能返回未定义结果或崩溃。
   这段回退分支合法输入永远走不到，必须单独覆盖（审计 round-2 N3 指出
   m4 旧变异点根本没被这条路径覆盖）。 */
static void test_compare_text_noncanonical(void)
{
    /* 长度不同：先按长度给序，避免 "zz" 与 "1" 这类文本互相穿透。 */
    TEST_CHECK(locker_storage_u64_compare_text("zz", "1") > 0);
    TEST_CHECK(locker_storage_u64_compare_text("1", "zz") < 0);
    /* 长度相同：按字节序，结果必须确定且反对称。 */
    TEST_CHECK(locker_storage_u64_compare_text("1a", "1b") < 0);
    TEST_CHECK(locker_storage_u64_compare_text("1b", "1a") > 0);
    TEST_CHECK(locker_storage_u64_compare_text("1a", "1a") == 0);
    /* 一侧合法一侧非法也不能返回 0 或越界。 */
    TEST_CHECK(locker_storage_u64_compare_text("7", "07x") != 0);
    TEST_CHECK(locker_storage_u64_compare_text("07x", "7") != 0);
    /* 超长（21 位）与空串走同一回退路径。 */
    TEST_CHECK(locker_storage_u64_compare_text("99999999999999999999x", "1") > 0);
    TEST_CHECK(locker_storage_u64_compare_text("", "0") < 0);
}

static void test_round_trip(void)
{
    static const uint64_t samples[] = {0u, 1u, 9u, 10u, 99u, 100u, 4294967296u, UINT64_MAX};
    for (size_t index = 0u; index < sizeof(samples) / sizeof(samples[0]); index++) {
        char text[21];
        uint64_t parsed = 0u;
        TEST_CHECK(locker_storage_u64_format(samples[index], text, sizeof(text)));
        TEST_CHECK(locker_storage_u64_parse(text, strlen(text), &parsed));
        TEST_CHECK_U64(samples[index], parsed);
    }
}

int main(void)
{
    test_parse_accepts();
    test_parse_rejects();
    test_format();
    test_compare_text();
    test_compare_text_noncanonical();
    test_round_trip();
    TEST_REPORT("storage.u64");
}
