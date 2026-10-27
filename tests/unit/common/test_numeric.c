#include "test_support.h"

#include "locker/common/numeric.h"

/*
 * Covers docs/stages/stage-01-task-cards.md:39-46: u64 zero, maximum,
 * out-of-range and illegal text all need tests. Storage keeps u64 as canonical
 * decimal text, so ordering must be numeric, never lexicographic
 * (docs/architecture/module-ownership.md:57).
 */

static void test_zero(void) {
    uint64_t value = 1u;
    TEST_CHECK(locker_u64_is_canonical_decimal("0", 1u));
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_u64_parse_decimal("0", 1u, &value));
    TEST_CHECK_U64(0u, value);

    char text[21];
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_u64_format_decimal(0u, text, sizeof(text)));
    TEST_CHECK_STRING("0", text);
}

static void test_maximum(void) {
    const char *max_text = "18446744073709551615";
    uint64_t value = 0;
    TEST_CHECK(locker_u64_is_canonical_decimal(max_text, 20u));
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_u64_parse_decimal(max_text, 20u, &value));
    TEST_CHECK_U64(UINT64_MAX, value);

    char text[21];
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_u64_format_decimal(UINT64_MAX, text, sizeof(text)));
    TEST_CHECK_STRING(max_text, text);
    TEST_CHECK_U64(20u, strlen(text));

    /* 20 digits of buffer is one short: needs 21 for the NUL. */
    char too_small[20];
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_u64_format_decimal(UINT64_MAX, too_small, sizeof(too_small)));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_u64_format_decimal(0u, NULL, 21u));
}

static void test_out_of_range(void) {
    /* One past UINT64_MAX: 20 digits but numerically out of range. */
    const char *overflow = "18446744073709551616";
    uint64_t value = 0;
    TEST_CHECK(locker_u64_is_canonical_decimal(overflow, 20u));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_u64_parse_decimal(overflow, 20u, &value));
    TEST_CHECK_U64(0u, value);

    const char *way_over = "99999999999999999999";
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_u64_parse_decimal(way_over, 20u, &value));

    /* 21 digits can never be canonical. */
    const char *too_long = "184467440737095516150";
    TEST_CHECK(!locker_u64_is_canonical_decimal(too_long, 21u));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_u64_parse_decimal(too_long, 21u, &value));
}

static void test_illegal_text(void) {
    static const struct {
        const char *text;
        size_t length;
    } rejected[] = {
        {"", 0u},          /* empty */
        {"+1", 2u},        /* sign */
        {"-1", 2u},        /* sign */
        {" 1", 2u},        /* leading space */
        {"1 ", 2u},        /* trailing space */
        {"01", 2u},        /* leading zero */
        {"00", 2u},        /* leading zero */
        {"1a", 2u},        /* hex letter */
        {"1.0", 3u},       /* decimal point */
        {"0x10", 4u},      /* hex prefix */
        {"1,000", 5u},     /* thousands separator */
        {"\t1", 2u},       /* tab */
        {"18446744073709551615 ", 21u}, /* max plus trailing space */
    };

    for (size_t i = 0; i < sizeof(rejected) / sizeof(rejected[0]); ++i) {
        uint64_t value = 12345u;
        TEST_CHECK(!locker_u64_is_canonical_decimal(rejected[i].text, rejected[i].length));
        TEST_CHECK_STATUS(LOCKER_STATUS_INVALID,
                          locker_u64_parse_decimal(rejected[i].text, rejected[i].length, &value));
        TEST_CHECK_U64(12345u, value);
    }

    uint64_t value = 0;
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_u64_parse_decimal(NULL, 1u, &value));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_u64_parse_decimal("1", 1u, NULL));
    TEST_CHECK(!locker_u64_is_canonical_decimal(NULL, 1u));
}

static void test_text_compare_is_numeric(void) {
    int order = 99;

    /* Lexicographically "9" > "10", numerically it is the other way round. */
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_u64_text_compare("9", 1u, "10", 2u, &order));
    TEST_CHECK(order == -1);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_u64_text_compare("10", 2u, "9", 1u, &order));
    TEST_CHECK(order == 1);

    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_u64_text_compare("42", 2u, "42", 2u, &order));
    TEST_CHECK(order == 0);

    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_u64_text_compare("0", 1u, "18446744073709551615", 20u, &order));
    TEST_CHECK(order == -1);
    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                      locker_u64_text_compare("18446744073709551615", 20u, "18446744073709551614", 20u, &order));
    TEST_CHECK(order == 1);

    /* Watermark-style monotonic sequence comparison. */
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_u64_text_compare("4294967296", 10u, "4294967295", 10u, &order));
    TEST_CHECK(order == 1);

    /* Illegal input never yields an order. */
    order = 77;
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_u64_text_compare("01", 2u, "1", 1u, &order));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_u64_text_compare("1", 1u, "x", 1u, &order));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_u64_text_compare("1", 1u, "1", 1u, NULL));
}

static void test_add_checked(void) {
    uint64_t sum = 0;

    TEST_CHECK(locker_u64_add_checked(0u, 0u, &sum));
    TEST_CHECK_U64(0u, sum);

    TEST_CHECK(locker_u64_add_checked(1u, 2u, &sum));
    TEST_CHECK_U64(3u, sum);

    TEST_CHECK(locker_u64_add_checked(UINT64_MAX - 1u, 1u, &sum));
    TEST_CHECK_U64(UINT64_MAX, sum);

    sum = 4242u;
    TEST_CHECK(!locker_u64_add_checked(UINT64_MAX, 1u, &sum));
    TEST_CHECK_U64(4242u, sum); /* out parameter untouched on overflow */

    TEST_CHECK(!locker_u64_add_checked(1u, 1u, NULL));
}

static void test_round_trip_all_boundaries(void) {
    static const uint64_t samples[] = {0u,          1u,          9u,          10u,
                                       99u,         100u,        65535u,      4294967295u,
                                       4294967296u, 9223372036854775807ull, 9223372036854775808ull,
                                       UINT64_MAX};
    for (size_t i = 0; i < sizeof(samples) / sizeof(samples[0]); ++i) {
        char text[21];
        TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_u64_format_decimal(samples[i], text, sizeof(text)));
        TEST_CHECK(locker_u64_is_canonical_decimal(text, strlen(text)));
        uint64_t parsed = 0;
        TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_u64_parse_decimal(text, strlen(text), &parsed));
        TEST_CHECK_U64(samples[i], parsed);
    }
}

int main(void) {
    test_zero();
    test_maximum();
    test_out_of_range();
    test_illegal_text();
    test_text_compare_is_numeric();
    test_add_checked();
    test_round_trip_all_boundaries();
    TEST_REPORT("common.numeric");
}
