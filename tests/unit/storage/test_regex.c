#define _POSIX_C_SOURCE 200809L

#include "test_support.h"

#include <string.h>

#include "locker/storage/regex.h"

static void test_literal_and_classes(void)
{
    locker_storage_regex_t *regex = locker_storage_regex_compile("^[0-9a-f]{24}$");
    TEST_CHECK(regex != NULL);
    TEST_CHECK(locker_storage_regex_match(regex, "0123456789abcdef01234567", 24u));
    TEST_CHECK(!locker_storage_regex_match(regex, "0123456789abcdef0123456", 23u));
    TEST_CHECK(!locker_storage_regex_match(regex, "0123456789ABCDEF01234567", 24u));
    TEST_CHECK(!locker_storage_regex_match(regex, "g123456789abcdef01234567", 24u));
    locker_storage_regex_free(regex);
}

static void test_alternation_and_anchors(void)
{
    locker_storage_regex_t *regex = locker_storage_regex_compile("^([0-9a-f]{8}|[0-9a-f]{14}|[0-9a-f]{20})$");
    TEST_CHECK(regex != NULL);
    TEST_CHECK(locker_storage_regex_match(regex, "01234567", 8u));
    TEST_CHECK(locker_storage_regex_match(regex, "0123456789abcd", 14u));
    TEST_CHECK(locker_storage_regex_match(regex, "0123456789abcdef0123", 20u));
    TEST_CHECK(!locker_storage_regex_match(regex, "0123456", 7u));
    TEST_CHECK(!locker_storage_regex_match(regex, "012345678", 9u));
    locker_storage_regex_free(regex);
}

static void test_uuid_pattern(void)
{
    locker_storage_regex_t *regex =
        locker_storage_regex_compile("^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$");
    TEST_CHECK(regex != NULL);
    TEST_CHECK(locker_storage_regex_match(regex, "11111111-2222-4333-8444-555555555555", 36u));
    TEST_CHECK(!locker_storage_regex_match(regex, "11111111-2222-4333-8444-55555555555", 35u));
    TEST_CHECK(!locker_storage_regex_match(regex, "111111112222433384445555555555555555", 36u));
    locker_storage_regex_free(regex);
}

static void test_search_semantics(void)
{
    /* 契约里的 pattern 是「搜索」语义，不是整串匹配；schema 校验器依赖这一点。 */
    locker_storage_regex_t *regex = locker_storage_regex_compile("b+");
    TEST_CHECK(regex != NULL);
    TEST_CHECK(locker_storage_regex_match(regex, "abbbc", 5u));
    TEST_CHECK(locker_storage_regex_match(regex, "b", 1u));
    TEST_CHECK(!locker_storage_regex_match(regex, "ac", 2u));
    locker_storage_regex_free(regex);
}

static void test_fail_closed(void)
{
    /* 不支持的语法必须编译失败，不能静默当成能匹配一切。 */
    TEST_CHECK(locker_storage_regex_compile("(?<name>a)") == NULL);
    TEST_CHECK(locker_storage_regex_compile("(a") == NULL);
    TEST_CHECK(locker_storage_regex_compile("[a-") == NULL);
    TEST_CHECK(locker_storage_regex_compile("a{2,1}") == NULL);
    TEST_CHECK(locker_storage_regex_compile("") == NULL);
    TEST_CHECK(locker_storage_regex_compile(NULL) == NULL);

    locker_storage_regex_t *regex = locker_storage_regex_compile("^a$");
    TEST_CHECK(regex != NULL);
    TEST_CHECK(!locker_storage_regex_match(regex, NULL, 1u));
    TEST_CHECK(!locker_storage_regex_match(regex, "a", 2u));
    locker_storage_regex_free(regex);
    locker_storage_regex_free(NULL);
}

static void test_bounded_repeat(void)
{
    /* 审计 round-4 N-3：零下界的有界重复曾多生成一个可选 atom，导致 ^a{0,1}$ 接受 "aa"。
       {n,m} 的语义必须是「至少 n 个、至多 m 个」，上界正好 m，m+1 必须不匹配。 */
    locker_storage_regex_t *regex = locker_storage_regex_compile("^a{0,1}$");
    TEST_CHECK(regex != NULL);
    TEST_CHECK(locker_storage_regex_match(regex, "", 0u));
    TEST_CHECK(locker_storage_regex_match(regex, "a", 1u));
    TEST_CHECK(!locker_storage_regex_match(regex, "aa", 2u));
    locker_storage_regex_free(regex);

    regex = locker_storage_regex_compile("^a{0,0}$");
    TEST_CHECK(regex != NULL);
    TEST_CHECK(locker_storage_regex_match(regex, "", 0u));
    TEST_CHECK(!locker_storage_regex_match(regex, "a", 1u));
    locker_storage_regex_free(regex);

    /* 契约里 U64 用的正是 {0,19}：19 位必须接受，20 位必须拒绝。 */
    regex = locker_storage_regex_compile("^[0-9]{0,19}$");
    TEST_CHECK(regex != NULL);
    TEST_CHECK(locker_storage_regex_match(regex, "1234567890123456789", 19u));
    TEST_CHECK(!locker_storage_regex_match(regex, "12345678901234567890", 20u));
    locker_storage_regex_free(regex);

    regex = locker_storage_regex_compile("^a{1,3}$");
    TEST_CHECK(regex != NULL);
    TEST_CHECK(!locker_storage_regex_match(regex, "", 0u));
    TEST_CHECK(locker_storage_regex_match(regex, "aa", 2u));
    TEST_CHECK(locker_storage_regex_match(regex, "aaa", 3u));
    TEST_CHECK(!locker_storage_regex_match(regex, "aaaa", 4u));
    locker_storage_regex_free(regex);

    regex = locker_storage_regex_compile("^a{2,}$");
    TEST_CHECK(regex != NULL);
    TEST_CHECK(!locker_storage_regex_match(regex, "a", 1u));
    TEST_CHECK(locker_storage_regex_match(regex, "aaaa", 4u));
    locker_storage_regex_free(regex);
}

int main(void)
{
    test_literal_and_classes();
    test_alternation_and_anchors();
    test_uuid_pattern();
    test_search_semantics();
    test_fail_closed();
    test_bounded_repeat();
    TEST_REPORT("storage.regex");
}
