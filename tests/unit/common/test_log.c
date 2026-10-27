#include "test_support.h"

#include <stdio.h>

#include "locker/common/clock.h"
#include "locker/common/log.h"

/*
 * Covers docs/stages/stage-01-task-cards.md:39-46: the log sink must never see
 * tokens or passwords (docs/architecture/monitoring-interfaces.md:214,
 * docs/stages/stage-00-architecture.md:178,
 * docs/architecture/internal-interfaces.md:83).
 */

#define CAPTURE_LINES 8u
#define CAPTURE_LINE_BYTES LOCKER_LOG_LINE_MAX

typedef struct capture_sink {
    char lines[CAPTURE_LINES][CAPTURE_LINE_BYTES];
    size_t count;
    locker_log_level_t last_level;
    size_t overlong_lines;
} capture_sink_t;

static void capture(void *user, locker_log_level_t level, const char *line) {
    capture_sink_t *sink = (capture_sink_t *)user;
    sink->last_level = level;
    if (sink->count >= CAPTURE_LINES) {
        return;
    }
    const size_t length = strlen(line);
    if (length + 1u > CAPTURE_LINE_BYTES) {
        sink->overlong_lines += 1u;
        return;
    }
    memcpy(sink->lines[sink->count], line, length + 1u);
    sink->count += 1u;
}

static uint64_t fake_monotonic(void *user) {
    return *(const uint64_t *)user;
}

static bool fake_wall(void *user, uint64_t *out_wall_ms) {
    (void)user;
    *out_wall_ms = 1700000000000u;
    return true;
}

static locker_status_t fake_uuid(void *user, locker_uuid_t *out_uuid) {
    (void)user;
    memset(out_uuid->bytes, 0x11, sizeof(out_uuid->bytes));
    out_uuid->bytes[6] = 0x40u;
    out_uuid->bytes[8] = 0x80u;
    return LOCKER_STATUS_OK;
}

static locker_logger_t make_logger(capture_sink_t *sink, uint64_t *monotonic_ms,
                                   locker_log_level_t min_level) {
    locker_clock_port_t clock;
    clock.user = monotonic_ms;
    clock.monotonic_ms = fake_monotonic;
    clock.wall_utc_ms = fake_wall;
    clock.new_uuid = fake_uuid;

    locker_logger_t logger;
    memset(&logger, 0, sizeof(logger));
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_logger_init(&logger, capture, sink, "core",
                                                           LOCKER_BACKEND_SIMULATOR, min_level, &clock));
    return logger;
}

static bool any_line_contains(const capture_sink_t *sink, const char *needle) {
    for (size_t i = 0; i < sink->count; ++i) {
        if (test_text_contains(sink->lines[i], needle)) {
            return true;
        }
    }
    return false;
}

static void test_init_validation_and_clock_default(void) {
    capture_sink_t sink;
    memset(&sink, 0, sizeof(sink));
    locker_logger_t logger;

    memset(&logger, 0, sizeof(logger));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_logger_init(NULL, capture, &sink, "core",
                                                                LOCKER_BACKEND_SIMULATOR,
                                                                LOCKER_LOG_LEVEL_INFO, NULL));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_logger_init(&logger, NULL, &sink, "core",
                                                                LOCKER_BACKEND_SIMULATOR,
                                                                LOCKER_LOG_LEVEL_INFO, NULL));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_logger_init(&logger, capture, &sink, NULL,
                                                                LOCKER_BACKEND_SIMULATOR,
                                                                LOCKER_LOG_LEVEL_INFO, NULL));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_logger_init(&logger, capture, &sink, "core",
                                                                LOCKER_BACKEND_SIMULATOR,
                                                                (locker_log_level_t)42, NULL));

    /* NULL clock selects the real clock: the line must still be emitted. */
    memset(&logger, 0, sizeof(logger));
    TEST_CHECK_STATUS(LOCKER_STATUS_OK, locker_logger_init(&logger, capture, &sink, "core",
                                                           LOCKER_BACKEND_SIMULATOR,
                                                           LOCKER_LOG_LEVEL_INFO, NULL));
    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                      locker_logger_log(&logger, LOCKER_LOG_LEVEL_INFO, "started", NULL, 0u));
    TEST_CHECK_U64(1u, sink.count);
    TEST_CHECK(test_text_contains(sink.lines[0], "event=started"));
    TEST_CHECK(test_text_contains(sink.lines[0], "backend=simulator"));
    locker_logger_deinit(&logger);
    TEST_CHECK(!logger.initialized);
}

static void test_level_filtering(void) {
    capture_sink_t sink;
    uint64_t monotonic = 1000u;
    memset(&sink, 0, sizeof(sink));
    locker_logger_t logger = make_logger(&sink, &monotonic, LOCKER_LOG_LEVEL_INFO);

    TEST_CHECK(locker_logger_enabled(&logger, LOCKER_LOG_LEVEL_ERROR));
    TEST_CHECK(locker_logger_enabled(&logger, LOCKER_LOG_LEVEL_INFO));
    TEST_CHECK(!locker_logger_enabled(&logger, LOCKER_LOG_LEVEL_DEBUG));

    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                      locker_logger_log(&logger, LOCKER_LOG_LEVEL_DEBUG, "verbose", NULL, 0u));
    TEST_CHECK_U64(0u, sink.count); /* filtered, sink not called */

    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                      locker_logger_log(&logger, LOCKER_LOG_LEVEL_ERROR, "boom", NULL, 0u));
    TEST_CHECK_U64(1u, sink.count);
    TEST_CHECK(sink.last_level == LOCKER_LOG_LEVEL_ERROR);

    locker_logger_deinit(&logger);
    TEST_CHECK(!locker_logger_enabled(&logger, LOCKER_LOG_LEVEL_ERROR));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID,
                      locker_logger_log(&logger, LOCKER_LOG_LEVEL_ERROR, "boom", NULL, 0u));
}

static void test_line_shape(void) {
    capture_sink_t sink;
    uint64_t monotonic = 987654u;
    memset(&sink, 0, sizeof(sink));
    locker_logger_t logger = make_logger(&sink, &monotonic, LOCKER_LOG_LEVEL_DEBUG);

    const locker_log_field_t fields[] = {
        {"operation", "3f2b"},
        {"state", "REQUESTED"},
    };
    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                      locker_logger_log(&logger, LOCKER_LOG_LEVEL_INFO, "operation.accepted", fields, 2u));
    TEST_CHECK_U64(1u, sink.count);

    const char *line = sink.lines[0];
    TEST_CHECK(test_text_contains(line, "ts=987654"));
    TEST_CHECK(test_text_contains(line, "utc=1700000000000"));
    TEST_CHECK(test_text_contains(line, "level=INFO"));
    TEST_CHECK(test_text_contains(line, "backend=simulator"));
    TEST_CHECK(test_text_contains(line, "component=core"));
    TEST_CHECK(test_text_contains(line, "event=operation.accepted"));
    TEST_CHECK(test_text_contains(line, "operation=3f2b"));
    TEST_CHECK(test_text_contains(line, "state=REQUESTED"));
    TEST_CHECK_U64(0u, sink.overlong_lines);

    locker_logger_deinit(&logger);
}

static void test_secrets_never_reach_sink(void) {
    capture_sink_t sink;
    uint64_t monotonic = 1u;
    memset(&sink, 0, sizeof(sink));
    locker_logger_t logger = make_logger(&sink, &monotonic, LOCKER_LOG_LEVEL_DEBUG);

    const char *secret = "s3cr3t-T0ken-value";
    const char *password = "hunter2-plaintext";
    const char *card_uid = "04A2B3C4D5E6F7";

    const locker_log_field_t fields[] = {
        {"token", secret},
        {"session_token", secret},
        {"Authorization", "Bearer abcdef0123456789"},
        {"password", password},
        {"user_password", password},
        {"card_uid", card_uid},
        {"device_id", "locker-01"},
        {"note", "token=s3cr3t-T0ken-value and password=hunter2-plaintext"},
        {"authorization_header", "Bearer abcdef0123456789"},
    };
    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                      locker_logger_log(&logger, LOCKER_LOG_LEVEL_INFO, "login", fields, 9u));
    TEST_CHECK_U64(1u, sink.count);

    /* The whole captured output must be free of every secret value. */
    TEST_CHECK(!any_line_contains(&sink, secret));
    TEST_CHECK(!any_line_contains(&sink, password));
    TEST_CHECK(!any_line_contains(&sink, "abcdef0123456789"));
    TEST_CHECK(!any_line_contains(&sink, card_uid));
    TEST_CHECK(any_line_contains(&sink, LOCKER_LOG_REDACTED));

    /* Non-sensitive correlation fields survive. */
    TEST_CHECK(any_line_contains(&sink, "device_id=locker-01"));

    locker_logger_deinit(&logger);
}

static void test_key_deny_list(void) {
    TEST_CHECK(locker_log_key_is_sensitive("token"));
    TEST_CHECK(locker_log_key_is_sensitive("TOKEN"));
    TEST_CHECK(locker_log_key_is_sensitive("Token"));
    TEST_CHECK(locker_log_key_is_sensitive("password"));
    TEST_CHECK(locker_log_key_is_sensitive("PASSWORD"));
    TEST_CHECK(locker_log_key_is_sensitive("access_token"));
    TEST_CHECK(locker_log_key_is_sensitive("refresh_token"));
    TEST_CHECK(locker_log_key_is_sensitive("client_secret"));
    TEST_CHECK(locker_log_key_is_sensitive("private_key"));
    TEST_CHECK(locker_log_key_is_sensitive("authorization"));
    TEST_CHECK(locker_log_key_is_sensitive("card_uid"));
    TEST_CHECK(locker_log_key_is_sensitive("user_password"));
    TEST_CHECK(locker_log_key_is_sensitive("api_key"));

    /* Audit B1 follow-up: separator/camel-case spellings that used to slip past
       the exact-name list and reach the sink verbatim. */
    TEST_CHECK(locker_log_key_is_sensitive("X-Token"));
    TEST_CHECK(locker_log_key_is_sensitive("authToken"));
    TEST_CHECK(locker_log_key_is_sensitive("reader_card_uid"));
    TEST_CHECK(locker_log_key_is_sensitive("tls.private_key"));
    TEST_CHECK(locker_log_key_is_sensitive("clientSecret"));
    TEST_CHECK(locker_log_key_is_sensitive("raw_uid"));
    TEST_CHECK(locker_log_key_is_sensitive("set-cookie"));

    /* Ordinary correlation keys stay loggable. */
    TEST_CHECK(!locker_log_key_is_sensitive("device_id"));
    TEST_CHECK(!locker_log_key_is_sensitive("operation_id"));
    TEST_CHECK(!locker_log_key_is_sensitive("boot_id"));
    TEST_CHECK(!locker_log_key_is_sensitive("event_seq"));
    TEST_CHECK(!locker_log_key_is_sensitive("state"));
    TEST_CHECK(!locker_log_key_is_sensitive(""));
    TEST_CHECK(!locker_log_key_is_sensitive(NULL));
}

static void test_scrub_free_text(void) {
    char out[LOCKER_LOG_LINE_MAX];

    size_t masked = locker_log_scrub("token=abc123 user=bob", out, sizeof(out));
    TEST_CHECK_U64(1u, masked);
    TEST_CHECK_STRING("token=[redacted] user=bob", out);

    masked = locker_log_scrub("password: hunter2; next", out, sizeof(out));
    TEST_CHECK_U64(1u, masked);
    TEST_CHECK(!test_text_contains(out, "hunter2"));

    masked = locker_log_scrub("Authorization: Bearer eyJhbGciOi", out, sizeof(out));
    TEST_CHECK(masked >= 1u);
    TEST_CHECK(!test_text_contains(out, "eyJhbGciOi"));

    masked = locker_log_scrub("plain diagnostic text", out, sizeof(out));
    TEST_CHECK_U64(0u, masked);
    TEST_CHECK_STRING("plain diagnostic text", out);

    /* Truncation is bounded and NUL terminated. */
    char tiny[8];
    locker_log_scrub("token=abcdefghij", tiny, sizeof(tiny));
    TEST_CHECK_U64(7u, strlen(tiny));
    TEST_CHECK(!test_text_contains(tiny, "abcdefghij"));

    TEST_CHECK_U64(0u, locker_log_scrub(NULL, out, sizeof(out)));
    TEST_CHECK_U64(0u, locker_log_scrub("x", NULL, 0u));
}

/*
 * Audit B1: the scrubber must consume a credential to its syntactic end, not to
 * the next word character. JWTs contain '.', quoted values contain spaces, and a
 * truncated scan let "Bearer [redacted].part2.part3" reach the sink.
 */
static void test_scrub_credential_syntax(void) {
    char out[LOCKER_LOG_LINE_MAX];

    /* Dotted bearer credential is masked in full, scheme name kept for context. */
    size_t masked = locker_log_scrub("Authorization: Bearer part1.part2.part3", out, sizeof(out));
    TEST_CHECK_U64(1u, masked);
    TEST_CHECK_STRING("Authorization: [redacted]", out);

    /* The audit reproduction, verbatim: the whole value must be masked. */
    masked = locker_log_scrub("note=\"Bearer part1.part2.part3\"", out, sizeof(out));
    TEST_CHECK_U64(1u, masked);
    TEST_CHECK_STRING("note=\"Bearer [redacted]\"", out);

    /* Quoted JSON password containing a space is one value, not two tokens. */
    masked = locker_log_scrub("{\"password\":\"alpha beta\"}", out, sizeof(out));
    TEST_CHECK_U64(1u, masked);
    TEST_CHECK_STRING("{\"password\":\"[redacted]\"}", out);

    /* Escaped quote inside the value does not end it early. */
    masked = locker_log_scrub("password=\"a\\\"b c\"", out, sizeof(out));
    TEST_CHECK_U64(1u, masked);
    TEST_CHECK(!test_text_contains(out, "b c"));
    TEST_CHECK(!test_text_contains(out, "a\\\"b"));

    /* Unterminated quote: mask the remainder instead of letting the tail through. */
    masked = locker_log_scrub("password=\"abc def", out, sizeof(out));
    TEST_CHECK_U64(1u, masked);
    TEST_CHECK(!test_text_contains(out, "abc"));
    TEST_CHECK(!test_text_contains(out, "def"));

    /* Dotted credential under a sensitive key with no auth scheme. */
    masked = locker_log_scrub("token=eyJhbGciOi.J9.zzz", out, sizeof(out));
    TEST_CHECK_U64(1u, masked);
    TEST_CHECK_STRING("token=[redacted]", out);

    /* Basic/Digest are consumed like Bearer. */
    masked = locker_log_scrub("authorization: Basic dXNlcjpwYXNz", out, sizeof(out));
    TEST_CHECK_U64(1u, masked);
    TEST_CHECK(!test_text_contains(out, "dXNlcjpwYXNz"));
}

/*
 * Audit B1-R: single-quoted values were not consumed at all. The scanner only
 * entered the quoted branch for '"', and an unquoted value stopped at the first
 * '\'', so note="password='alpha beta'" reached the sink as
 * note="password=[redacted]'alpha beta'". Every quote form must mask in full.
 */
static void test_scrub_single_quoted_values(void) {
    char out[LOCKER_LOG_LINE_MAX];

    /* The audit reproduction, verbatim. */
    size_t masked = locker_log_scrub("note=\"password='alpha beta'\"", out, sizeof(out));
    TEST_CHECK_U64(1u, masked);
    TEST_CHECK_STRING("note=\"password='[redacted]'\"", out);
    TEST_CHECK(!test_text_contains(out, "alpha beta"));

    /* Bare single-quoted value, no surrounding field quotes. */
    masked = locker_log_scrub("password='alpha beta'", out, sizeof(out));
    TEST_CHECK_U64(1u, masked);
    TEST_CHECK_STRING("password='[redacted]'", out);

    /* Single-quoted JSON-ish key and value. */
    masked = locker_log_scrub("{'password':'alpha beta'}", out, sizeof(out));
    TEST_CHECK_U64(1u, masked);
    TEST_CHECK_STRING("{'password':'[redacted]'}", out);

    /* Single-quoted key with a separate '=' separator. */
    masked = locker_log_scrub("'password'='alpha beta'", out, sizeof(out));
    TEST_CHECK_U64(1u, masked);
    TEST_CHECK_STRING("'password'='[redacted]'", out);

    /* Escaped single quote inside a single-quoted value does not end it. */
    masked = locker_log_scrub("password='a\\'b c'", out, sizeof(out));
    TEST_CHECK_U64(1u, masked);
    TEST_CHECK(!test_text_contains(out, "b c"));

    /* Unterminated single quote fails closed: mask the remainder. */
    masked = locker_log_scrub("password='abc def", out, sizeof(out));
    TEST_CHECK_U64(1u, masked);
    TEST_CHECK(!test_text_contains(out, "abc"));
    TEST_CHECK(!test_text_contains(out, "def"));

    /* Mixed quoting in one value: both secrets masked. */
    masked = locker_log_scrub("token=\"A1\" password='B2'", out, sizeof(out));
    TEST_CHECK_U64(2u, masked);
    TEST_CHECK(!test_text_contains(out, "A1"));
    TEST_CHECK(!test_text_contains(out, "B2"));

    /* An apostrophe in ordinary prose must not swallow the following text. */
    masked = locker_log_scrub("it's a plain sentence", out, sizeof(out));
    TEST_CHECK_U64(0u, masked);
    TEST_CHECK_STRING("it's a plain sentence", out);
}

/*
 * Audit B3: the deny list was evaluated on a 63-character truncated copy of the
 * key, so a long field name ending in "_token" was treated as benign and its
 * value reached the sink. The suffix test must run on the whole original key.
 */
static void test_long_key_suffix_is_sensitive(void) {
    char key[80];
    memset(key, 'a', 63u);
    memcpy(key + 63u, "_token", 7u);

    /* The public predicate and the logger must agree. */
    TEST_CHECK(locker_log_key_is_sensitive(key));

    capture_sink_t sink;
    uint64_t monotonic = 11u;
    memset(&sink, 0, sizeof(sink));
    locker_logger_t logger = make_logger(&sink, &monotonic, LOCKER_LOG_LEVEL_DEBUG);
    const locker_log_field_t fields[] = {{key, "LONG_KEY_SECRET"}};
    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                      locker_logger_log(&logger, LOCKER_LOG_LEVEL_INFO, "long_key", fields, 1u));
    TEST_CHECK_U64(1u, sink.count);
    TEST_CHECK(!any_line_contains(&sink, "LONG_KEY_SECRET"));
    TEST_CHECK(any_line_contains(&sink, "[redacted]"));
    locker_logger_deinit(&logger);

    /* The same holds for a long prefix with separators and mixed case. */
    char spaced[96];
    memset(spaced, 'x', 40u);
    memcpy(spaced + 40u, "-Private-Key", 13u);
    TEST_CHECK(locker_log_key_is_sensitive(spaced));

    /* A long key with no sensitive suffix stays loggable (no false positive). */
    char benign[80];
    memset(benign, 'b', 70u);
    benign[70u] = '\0';
    TEST_CHECK(!locker_log_key_is_sensitive(benign));
    TEST_CHECK(!locker_log_key_is_sensitive("device_identifier"));

    /* Suffix matching still works when separators trail the key. */
    TEST_CHECK(locker_log_key_is_sensitive("session_token"));
    TEST_CHECK(locker_log_key_is_sensitive("session__token"));
    TEST_CHECK(locker_log_key_is_sensitive("SESSION-TOKEN"));
}

/*
 * Audit B2: the previous scrubber collected at most 16 mask spans and copied the
 * rest of the value verbatim, so the 17th secret reached the sink. There is no
 * span budget any more; every secret in the value must be masked.
 */
static void test_scrub_no_span_budget(void) {
    char input[512];
    char out[LOCKER_LOG_LINE_MAX];
    size_t used = 0;

    for (size_t i = 0; i < 24u; ++i) {
        const int written = snprintf(input + used, sizeof(input) - used, "token=v%02u ", (unsigned)i);
        if (written <= 0) {
            break;
        }
        used += (size_t)written;
    }
    const int tail = snprintf(input + used, sizeof(input) - used, "token=LAST_SECRET");
    TEST_CHECK(tail > 0);

    const size_t masked = locker_log_scrub(input, out, sizeof(out));
    TEST_CHECK_U64(25u, masked);
    TEST_CHECK(!test_text_contains(out, "LAST_SECRET"));
    TEST_CHECK(!test_text_contains(out, "v23"));

    /* The logger path shares the scrubber: a multi-secret field must stay clean. */
    capture_sink_t sink;
    uint64_t monotonic = 7u;
    memset(&sink, 0, sizeof(sink));
    locker_logger_t logger = make_logger(&sink, &monotonic, LOCKER_LOG_LEVEL_DEBUG);
    const locker_log_field_t fields[] = {
        {"note", input},
        {"note", "Bearer part1.part2.part3"},
        {"note", "{\"password\":\"alpha beta\"}"},
    };
    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                      locker_logger_log(&logger, LOCKER_LOG_LEVEL_INFO, "leak", fields, 3u));
    TEST_CHECK_U64(1u, sink.count);
    TEST_CHECK(!any_line_contains(&sink, "LAST_SECRET"));
    TEST_CHECK(!any_line_contains(&sink, "part2"));
    TEST_CHECK(!any_line_contains(&sink, "alpha beta"));
    locker_logger_deinit(&logger);
}

/*
 * Audit B1 follow-up: the deny list is matched on a normalised key, so separator
 * and camel-case spellings are redacted by the logger, while correlation keys
 * remain readable.
 */
static void test_key_spelling_variants(void) {
    capture_sink_t sink;
    uint64_t monotonic = 9u;
    memset(&sink, 0, sizeof(sink));
    locker_logger_t logger = make_logger(&sink, &monotonic, LOCKER_LOG_LEVEL_DEBUG);

    const locker_log_field_t fields[] = {
        {"X-Token", "AUDIT_SECRET"},
        {"authToken", "AUDIT_SECRET"},
        {"reader_card_uid", "AUDIT_SECRET"},
        {"tls.private_key", "AUDIT_SECRET"},
        {"device_id", "locker-01"},
        {"operation_id", "op-42"},
    };
    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                      locker_logger_log(&logger, LOCKER_LOG_LEVEL_INFO, "variant", fields, 6u));
    TEST_CHECK_U64(1u, sink.count);
    TEST_CHECK(!any_line_contains(&sink, "AUDIT_SECRET"));
    TEST_CHECK(any_line_contains(&sink, "device_id=locker-01"));
    TEST_CHECK(any_line_contains(&sink, "operation_id=op-42"));

    locker_logger_deinit(&logger);
}

static void test_line_is_bounded(void) {
    capture_sink_t sink;
    uint64_t monotonic = 5u;
    memset(&sink, 0, sizeof(sink));
    locker_logger_t logger = make_logger(&sink, &monotonic, LOCKER_LOG_LEVEL_DEBUG);

    /* Long component/event/values must not overflow the fixed line buffer. */
    char long_value[400];
    memset(long_value, 'A', sizeof(long_value) - 1u);
    long_value[sizeof(long_value) - 1u] = '\0';

    locker_log_field_t fields[8];
    for (size_t i = 0; i < 8u; ++i) {
        fields[i].key = "detail";
        fields[i].value = long_value;
    }
    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                      locker_logger_log(&logger, LOCKER_LOG_LEVEL_INFO, "big", fields, 8u));
    TEST_CHECK_U64(1u, sink.count);
    TEST_CHECK(strlen(sink.lines[0]) < LOCKER_LOG_LINE_MAX);
    TEST_CHECK_U64(0u, sink.overlong_lines);

    locker_logger_deinit(&logger);
}

static void test_invalid_log_arguments(void) {
    capture_sink_t sink;
    uint64_t monotonic = 5u;
    memset(&sink, 0, sizeof(sink));
    locker_logger_t logger = make_logger(&sink, &monotonic, LOCKER_LOG_LEVEL_DEBUG);

    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID, locker_logger_log(NULL, LOCKER_LOG_LEVEL_INFO, "x", NULL, 0u));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID,
                      locker_logger_log(&logger, LOCKER_LOG_LEVEL_INFO, NULL, NULL, 0u));
    TEST_CHECK_STATUS(LOCKER_STATUS_INVALID,
                      locker_logger_log(&logger, LOCKER_LOG_LEVEL_INFO, "x", NULL, 3u));

    /* NULL key/value pairs are skipped, not fatal. */
    const locker_log_field_t fields[] = {{NULL, "v"}, {"k", NULL}, {"", "v"}, {"ok", "value"}};
    TEST_CHECK_STATUS(LOCKER_STATUS_OK,
                      locker_logger_log(&logger, LOCKER_LOG_LEVEL_INFO, "partial", fields, 4u));
    TEST_CHECK_U64(1u, sink.count);
    TEST_CHECK(test_text_contains(sink.lines[0], "ok=value"));

    locker_logger_deinit(&logger);
}

static void test_names(void) {
    TEST_CHECK_STRING("ERROR", locker_log_level_name(LOCKER_LOG_LEVEL_ERROR));
    TEST_CHECK_STRING("WARN", locker_log_level_name(LOCKER_LOG_LEVEL_WARN));
    TEST_CHECK_STRING("INFO", locker_log_level_name(LOCKER_LOG_LEVEL_INFO));
    TEST_CHECK_STRING("DEBUG", locker_log_level_name(LOCKER_LOG_LEVEL_DEBUG));
    TEST_CHECK_STRING("UNKNOWN", locker_log_level_name((locker_log_level_t)99));
    TEST_CHECK_STRING("simulator", locker_log_backend_name(LOCKER_BACKEND_SIMULATOR));
    TEST_CHECK_STRING("hardware", locker_log_backend_name(LOCKER_BACKEND_HARDWARE));
    TEST_CHECK_STRING("unknown", locker_log_backend_name((locker_backend_kind_t)99));
}

int main(void) {
    test_init_validation_and_clock_default();
    test_level_filtering();
    test_line_shape();
    test_secrets_never_reach_sink();
    test_key_deny_list();
    test_scrub_free_text();
    test_scrub_credential_syntax();
    test_scrub_single_quoted_values();
    test_scrub_no_span_budget();
    test_long_key_suffix_is_sensitive();
    test_key_spelling_variants();
    test_line_is_bounded();
    test_invalid_log_arguments();
    test_names();
    TEST_REPORT("common.log");
}
