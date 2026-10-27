#include "test_support.h"

#include "locker/common/uuid.h"

static void set_sample_uuid(locker_uuid_t *uuid) {
    /* 0f8fad5b-d9cb-469f-a165-70867728950e */
    static const uint8_t bytes[16] = {0x0f, 0x8f, 0xad, 0x5b, 0xd9, 0xcb, 0x46, 0x9f,
                                      0xa1, 0x65, 0x70, 0x86, 0x77, 0x28, 0x95, 0x0e};
    memcpy(uuid->bytes, bytes, sizeof(bytes));
}

static void test_parse_canonical_text(void) {
    const char *text = "0f8fad5b-d9cb-469f-a165-70867728950e";
    locker_uuid_t parsed;
    memset(&parsed, 0, sizeof(parsed));

    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_uuid_parse(text, 36u, &parsed));

    locker_uuid_t expected;
    set_sample_uuid(&expected);
    TEST_CHECK(locker_uuid_equal(&parsed, &expected));
    TEST_CHECK(locker_uuid_is_rfc4122_v4(&parsed));
    TEST_CHECK(!locker_uuid_is_nil(&parsed));
}

static void test_parse_rejects_malformed_text(void) {
    static const char *const rejected[] = {
        "",                                                              /* empty */
        "0f8fad5bd9cb469fa16570867728950e",                              /* no hyphens */
        "0F8FAD5B-D9CB-469F-A165-70867728950E",                          /* uppercase */
        "0f8fad5b-d9cb-469f-a165-70867728950",                           /* too short */
        "0f8fad5b-d9cb-469f-a165-70867728950e0",                         /* too long */
        "0f8fad5b_d9cb_469f_a165_70867728950e",                          /* wrong separators */
        "0f8fad5bd9cb-469f-a165-70867728950e-",                          /* hyphen shifted */
        "0f8fad5b-d9cb-469f-a165-70867728950g",                          /* non-hex */
        "{0f8fad5b-d9cb-469f-a165-70867728950e}",                        /* braces */
        "urn:uuid:0f8fad5b-d9cb-469f-a165-70867728950e",                 /* urn prefix */
        " 0f8fad5b-d9cb-469f-a165-70867728950",                          /* leading space, short */
        "00000000-0000-0000-0000-000000000000",                          /* nil is parseable */
    };

    for (size_t i = 0; i + 1u < sizeof(rejected) / sizeof(rejected[0]); ++i) {
        locker_uuid_t parsed;
        memset(&parsed, 0xAA, sizeof(parsed));
        const char *text = rejected[i];
        TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_uuid_parse(text, strlen(text), &parsed));
        /* A rejected parse must not partially overwrite the caller's value. */
        bool untouched = true;
        for (size_t b = 0; b < sizeof(parsed.bytes); ++b) {
            if (parsed.bytes[b] != 0xAAu) {
                untouched = false;
            }
        }
        TEST_CHECK(untouched);
    }

    /* The nil UUID is valid text, but not nil after parsing? It is nil. */
    locker_uuid_t nil;
    const char *nil_text = rejected[sizeof(rejected) / sizeof(rejected[0]) - 1u];
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_uuid_parse(nil_text, strlen(nil_text), &nil));
    TEST_CHECK(locker_uuid_is_nil(&nil));

    /* Length is explicit, so embedded NUL / trailing bytes are rejected. */
    locker_uuid_t parsed;
    memset(&parsed, 0, sizeof(parsed));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID,
                      locker_uuid_parse("0f8fad5b-d9cb-469f-a165-70867728950e", 35u, &parsed));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID,
                      locker_uuid_parse("0f8fad5b-d9cb-469f-a165-70867728950e", 37u, &parsed));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_uuid_parse(NULL, 36u, &parsed));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID,
                      locker_uuid_parse("0f8fad5b-d9cb-469f-a165-70867728950e", 36u, NULL));
}

static void test_format_round_trip(void) {
    locker_uuid_t uuid;
    set_sample_uuid(&uuid);

    char text[37];
    memset(text, 0x5A, sizeof(text));
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_uuid_format(&uuid, text, sizeof(text)));
    TEST_CHECK_STRING("0f8fad5b-d9cb-469f-a165-70867728950e", text);
    TEST_CHECK_U64(36u, strlen(text));

    /* Undersized buffers are rejected rather than truncated. */
    char small[36];
    memset(small, 0x5A, sizeof(small));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_uuid_format(&uuid, small, sizeof(small)));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_uuid_format(&uuid, NULL, 37u));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_uuid_format(NULL, text, sizeof(text)));

    /* Format then parse is lossless. */
    locker_uuid_t reparsed;
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_uuid_parse(text, 36u, &reparsed));
    TEST_CHECK(locker_uuid_equal(&uuid, &reparsed));
}

static void test_nil_and_equality(void) {
    locker_uuid_t nil;
    locker_uuid_t sample;
    memset(&nil, 0, sizeof(nil));
    set_sample_uuid(&sample);

    TEST_CHECK(locker_uuid_is_nil(&nil));
    TEST_CHECK(!locker_uuid_is_nil(&sample));
    TEST_CHECK(!locker_uuid_is_nil(NULL));

    TEST_CHECK(locker_uuid_equal(&nil, &nil));
    TEST_CHECK(locker_uuid_equal(&sample, &sample));
    TEST_CHECK(!locker_uuid_equal(&nil, &sample));
    TEST_CHECK(!locker_uuid_equal(&sample, &nil));
    TEST_CHECK(!locker_uuid_equal(NULL, &sample));

    locker_uuid_t cleared;
    set_sample_uuid(&cleared);
    locker_uuid_clear(&cleared);
    TEST_CHECK(locker_uuid_is_nil(&cleared));
    locker_uuid_clear(NULL); /* must not crash */
}

static void test_variant_and_version_bits(void) {
    locker_uuid_t uuid;
    memset(&uuid, 0, sizeof(uuid));

    /* version nibble must be 4 */
    uuid.bytes[6] = 0x40u;
    uuid.bytes[8] = 0x80u;
    TEST_CHECK(locker_uuid_is_rfc4122_v4(&uuid));

    uuid.bytes[6] = 0x30u;
    TEST_CHECK(!locker_uuid_is_rfc4122_v4(&uuid));

    uuid.bytes[6] = 0x40u;
    uuid.bytes[8] = 0xC0u; /* variant 110 -> not RFC 4122 */
    TEST_CHECK(!locker_uuid_is_rfc4122_v4(&uuid));

    TEST_CHECK(!locker_uuid_is_rfc4122_v4(NULL));
}

int main(void) {
    test_parse_canonical_text();
    test_parse_rejects_malformed_text();
    test_format_round_trip();
    test_nil_and_equality();
    test_variant_and_version_bits();
    TEST_REPORT("common.uuid");
}
