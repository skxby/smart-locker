#include "locker/common/log.h"

#include <string.h>

#include "locker/common/clock.h"

/*
 * Bounded, redacting log line builder.
 *
 * The whole line is assembled in a caller-owned 512-byte stack buffer: no heap,
 * no shared mutable state, no unbounded formatting. Redaction happens twice:
 *   1. a field whose key is on the deny list has its value replaced entirely;
 *   2. every remaining value passes through locker_log_scrub(), which masks
 *      "key=value"/"key: value"/"Bearer <token>" fragments embedded in free text.
 */

#define LOCKER_LOG_KEY_MAX 64u
#define LOCKER_LOG_VALUE_MAX LOCKER_LOG_LINE_MAX

typedef struct text_writer {
    char *buffer;
    size_t size;
    size_t length;
    bool truncated;
} text_writer_t;

static void writer_init(text_writer_t *writer, char *buffer, size_t size) {
    writer->buffer = buffer;
    writer->size = size;
    writer->length = 0;
    writer->truncated = false;
    if (size > 0) {
        buffer[0] = '\0';
    }
}

static void writer_putc(text_writer_t *writer, char c) {
    if (writer->size == 0) {
        writer->truncated = true;
        return;
    }
    if (writer->length + 1u >= writer->size) {
        writer->truncated = true;
        return;
    }
    writer->buffer[writer->length] = c;
    writer->length += 1u;
    writer->buffer[writer->length] = '\0';
}

static void writer_puts(text_writer_t *writer, const char *text) {
    if (text == NULL) {
        return;
    }
    while (*text != '\0') {
        writer_putc(writer, *text);
        text += 1;
    }
}

static char ascii_lower(char c) {
    if (c >= 'A' && c <= 'Z') {
        return (char)(c - 'A' + 'a');
    }
    return c;
}

static bool equals_ignore_case(const char *lhs, const char *rhs) {
    if (lhs == NULL || rhs == NULL) {
        return false;
    }
    while (*lhs != '\0' && *rhs != '\0') {
        if (ascii_lower(*lhs) != ascii_lower(*rhs)) {
            return false;
        }
        lhs += 1;
        rhs += 1;
    }
    return *lhs == '\0' && *rhs == '\0';
}

/*
 * Keys are matched on the whole original key, never on a truncated copy: a
 * 69-character field name ending in "_token" must still be treated as a token
 * field. Only the normalised equality test uses a fixed buffer, and an equality
 * entry is at most 13 characters, so a normalised key longer than the buffer can
 * never equal one of them.
 */
static bool is_key_separator(char c) {
    return c == '_' || c == '-' || c == '.' || c == ' ' || c == '\t' || c == ':' || c == '/';
}

/*
 * Compares the key against a suffix from the end, skipping separators, so the
 * check stays O(suffix) in time and O(1) in memory for arbitrarily long keys.
 */
static bool key_ends_with_n(const char *key, size_t length, const char *suffix) {
    const size_t suffix_length = strlen(suffix);
    if (suffix_length == 0u) {
        return false;
    }
    size_t matched = 0;
    size_t i = length;
    while (i > 0u && matched < suffix_length) {
        const char c = key[i - 1u];
        i -= 1u;
        if (is_key_separator(c)) {
            continue;
        }
        if (ascii_lower(c) != ascii_lower(suffix[suffix_length - 1u - matched])) {
            return false;
        }
        matched += 1u;
    }
    return matched == suffix_length;
}

/*
 * Sensitive keys are matched after normalisation: the key is lower-cased and the
 * separators '_', '-', '.', ':' and '/' are dropped, then the normalised key must
 * equal a deny entry or end with one. This is what makes the spellings that
 * actually reach a logger match - "token", "X-Token", "authToken",
 * "reader_card_uid", "tls.private_key" - while correlation keys such as
 * "device_id", "operation_id" and "boot_id" stay loggable.
 */
static const char *const k_sensitive_keys[] = {
    "token",     "accesstoken", "refreshtoken", "sessiontoken", "idtoken",
    "password",  "passwd",      "secret",       "clientsecret", "privatekey",
    "authorization", "cookie",  "setcookie",    "apikey",       "credential",
    "carduid",   "rawuid",      "uid",
};

static const char *const k_sensitive_suffixes[] = {
    "token", "secret", "password", "passwd", "privatekey", "credential", "apikey",
    "carduid", "rawuid",
};

/* Returns the normalised length; the text is truncated when the buffer is small. */
static size_t normalize_key(const char *key, size_t length, char *out_text, size_t out_size) {
    size_t out = 0;
    if (out_size == 0) {
        return 0;
    }
    for (size_t i = 0; i < length; ++i) {
        const char c = key[i];
        if (is_key_separator(c)) {
            continue;
        }
        if (out + 1u < out_size) {
            out_text[out] = ascii_lower(c);
        }
        out += 1u;
    }
    out_text[(out < out_size) ? out : (out_size - 1u)] = '\0';
    return out;
}

static bool key_is_sensitive_n(const char *key, size_t length) {
    char normalized[LOCKER_LOG_KEY_MAX];
    if (key == NULL || length == 0u) {
        return false;
    }
    const size_t normalized_length = normalize_key(key, length, normalized, sizeof(normalized));
    if (normalized[0] == '\0') {
        return false;
    }
    /* Equality only: a truncated normalised key cannot equal an entry. */
    if (normalized_length < sizeof(normalized)) {
        for (size_t i = 0; i < sizeof(k_sensitive_keys) / sizeof(k_sensitive_keys[0]); ++i) {
            if (equals_ignore_case(normalized, k_sensitive_keys[i])) {
                return true;
            }
        }
    }
    /* Suffix on the whole original key, so long prefixes cannot hide the suffix. */
    for (size_t i = 0; i < sizeof(k_sensitive_suffixes) / sizeof(k_sensitive_suffixes[0]); ++i) {
        if (key_ends_with_n(key, length, k_sensitive_suffixes[i])) {
            return true;
        }
    }
    return false;
}

bool locker_log_key_is_sensitive(const char *key) {
    if (key == NULL) {
        return false;
    }
    return key_is_sensitive_n(key, strlen(key));
}

static bool is_word_char(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
           c == '_' || c == '-';
}

static bool is_space_char(char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

static bool equals_ignore_case_n(const char *text, size_t length, const char *expected) {
    size_t expected_length = strlen(expected);
    if (length != expected_length) {
        return false;
    }
    for (size_t i = 0; i < length; ++i) {
        if (ascii_lower(text[i]) != ascii_lower(expected[i])) {
            return false;
        }
    }
    return true;
}

static size_t find_word_end(const char *input, size_t length, size_t start) {
    size_t end = start;
    while (end < length && is_word_char(input[end])) {
        end += 1u;
    }
    return end;
}

/*
 * A credential is consumed to its natural end, not to the next "word" character:
 * JWTs and base64 tokens contain '.' , '~', '+', '/' and '=', which is exactly why
 * a word-based scan stopped at the first dot and leaked "part1.part2.part3".
 */
static bool is_credential_char(char c) {
    if (is_space_char(c)) {
        return false;
    }
    switch (c) {
        case '"':
        case '\'':
        case ',':
        case ';':
        case '&':
        case '}':
        case ']':
        case ')':
        case '<':
        case '>':
        case '\\':
            return false;
        default:
            return true;
    }
}

static size_t scan_credential_token(const char *input, size_t length, size_t start) {
    size_t end = start;
    while (end < length && is_credential_char(input[end])) {
        end += 1u;
    }
    return end;
}

/*
 * Scans a quoted value starting at the opening quote and returns the index just
 * past the closing quote. Both quote characters are supported: a password typed
 * as password='alpha beta' is the same secret as password="alpha beta", and the
 * audit found that only the double-quoted form was consumed. An unterminated
 * string returns length and *terminated = false, so the caller masks everything
 * that is left instead of letting the tail escape.
 */
static size_t scan_quoted_value(const char *input, size_t length, size_t start, char quote,
                                bool *terminated) {
    size_t i = start + 1u;
    while (i < length) {
        if (input[i] == '\\' && i + 1u < length) {
            i += 2u;
            continue;
        }
        if (input[i] == quote) {
            *terminated = true;
            return i + 1u;
        }
        i += 1u;
    }
    *terminated = false;
    return length;
}

static size_t scan_unquoted_value(const char *input, size_t length, size_t start) {
    size_t end = start;
    while (end < length) {
        const char c = input[end];
        if (is_space_char(c) || c == '"' || c == '\'' || c == ',' || c == ';' || c == '&' || c == '}' ||
            c == ']' || c == ')') {
            break;
        }
        end += 1u;
    }
    return end;
}

static bool is_auth_scheme(const char *text, size_t length) {
    return equals_ignore_case_n(text, length, "bearer") || equals_ignore_case_n(text, length, "basic") ||
           equals_ignore_case_n(text, length, "digest");
}

static void writer_put_range(text_writer_t *writer, const char *input, size_t begin, size_t end) {
    for (size_t i = begin; i < end; ++i) {
        writer_putc(writer, input[i]);
    }
}

/*
 * Single-pass scrubber: it writes the masked text as it scans, so there is no
 * fixed number of spans to exhaust. The old implementation collected at most
 * LOCKER_LOG_MASK_SPANS (16) spans and then copied the rest of the value
 * verbatim, which let the 17th secret through.
 */
static size_t scrub_stream(const char *input, size_t length, text_writer_t *writer) {
    size_t masked = 0;
    size_t i = 0;
    while (i < length) {
        if (!is_word_char(input[i]) || (i > 0u && is_word_char(input[i - 1u]))) {
            writer_putc(writer, input[i]);
            i += 1u;
            continue;
        }
        const size_t word_end = find_word_end(input, length, i);
        const size_t word_length = word_end - i;

        /* "<scheme> <credential>": mask the credential, not only the scheme. */
        if (is_auth_scheme(input + i, word_length)) {
            size_t value_start = word_end;
            while (value_start < length && is_space_char(input[value_start])) {
                value_start += 1u;
            }
            const size_t value_end = scan_credential_token(input, length, value_start);
            if (value_end > value_start) {
                writer_put_range(writer, input, i, value_start);
                writer_puts(writer, LOCKER_LOG_REDACTED);
                masked += 1u;
                i = value_end;
                continue;
            }
            writer_put_range(writer, input, i, word_end);
            i = word_end;
            continue;
        }

        if (key_is_sensitive_n(input + i, word_length)) {
            size_t cursor = word_end;
            /* JSON form: "password":"..." and its single-quoted twin
               {'password':'...'} - step over the key's closing quote so the
               separator after it is still seen. */
            if (cursor < length && (input[cursor] == '"' || input[cursor] == '\'')) {
                cursor += 1u;
            }
            while (cursor < length && is_space_char(input[cursor])) {
                cursor += 1u;
            }
            if (cursor < length && (input[cursor] == '=' || input[cursor] == ':')) {
                cursor += 1u;
                while (cursor < length && is_space_char(input[cursor])) {
                    cursor += 1u;
                }
                if (cursor < length && (input[cursor] == '"' || input[cursor] == '\'')) {
                    /* Quoted value: consume the whole string, escapes included, so
                       "password":"alpha beta" is masked as one value instead of
                       stopping at the first space. Both quote characters count. */
                    const char quote = input[cursor];
                    bool terminated = false;
                    const size_t quoted_end = scan_quoted_value(input, length, cursor, quote, &terminated);
                    /* Keep the opening quote, mask the contents, then close it, so
                       {"password":"alpha beta"} becomes {"password":"[redacted]"} and
                       stays syntactically valid for a human reader. */
                    writer_put_range(writer, input, i, cursor + 1u);
                    writer_puts(writer, LOCKER_LOG_REDACTED);
                    if (terminated) {
                        writer_putc(writer, quote);
                    }
                    masked += 1u;
                    i = quoted_end;
                    continue;
                }
                size_t value_end = scan_unquoted_value(input, length, cursor);
                /* "Authorization: Bearer <token>": the scheme word is not the
                   secret, so the masked range extends over the following token. */
                if (value_end > cursor && is_auth_scheme(input + cursor, value_end - cursor)) {
                    size_t token_start = value_end;
                    while (token_start < length && is_space_char(input[token_start])) {
                        token_start += 1u;
                    }
                    const size_t token_end = scan_credential_token(input, length, token_start);
                    if (token_end > token_start) {
                        value_end = token_end;
                    }
                }
                writer_put_range(writer, input, i, cursor);
                writer_puts(writer, LOCKER_LOG_REDACTED);
                masked += 1u;
                i = (value_end > i) ? value_end : cursor;
                continue;
            }
        }

        writer_put_range(writer, input, i, word_end);
        i = word_end;
    }
    return masked;
}

size_t locker_log_scrub(const char *input, char *out_text, size_t out_size) {
    if (out_text == NULL || out_size == 0) {
        return 0;
    }
    out_text[0] = '\0';
    if (input == NULL) {
        return 0;
    }
    text_writer_t writer;
    writer_init(&writer, out_text, out_size);
    return scrub_stream(input, strlen(input), &writer);
}

static void writer_put_u64(text_writer_t *writer, uint64_t value) {
    char digits[21];
    size_t count = 0;
    uint64_t remaining = value;
    do {
        digits[count] = (char)('0' + (remaining % 10u));
        count += 1u;
        remaining /= 10u;
    } while (remaining != 0u);
    while (count > 0u) {
        count -= 1u;
        writer_putc(writer, digits[count]);
    }
}

static void sanitize_token(const char *text, char *out_text, size_t out_size) {
    size_t out = 0;
    if (out_size == 0) {
        return;
    }
    if (text != NULL) {
        for (size_t i = 0; text[i] != '\0' && out + 1u < out_size; ++i) {
            char c = text[i];
            if (is_word_char(c) || c == '.' || c == ':' || c == '/') {
                out_text[out] = c;
            } else {
                out_text[out] = '_';
            }
            out += 1u;
        }
    }
    out_text[out] = '\0';
}

const char *locker_log_level_name(locker_log_level_t level) {
    switch (level) {
        case LOCKER_LOG_LEVEL_ERROR:
            return "ERROR";
        case LOCKER_LOG_LEVEL_WARN:
            return "WARN";
        case LOCKER_LOG_LEVEL_INFO:
            return "INFO";
        case LOCKER_LOG_LEVEL_DEBUG:
            return "DEBUG";
        default:
            return "UNKNOWN";
    }
}

const char *locker_log_backend_name(locker_backend_kind_t backend) {
    switch (backend) {
        case LOCKER_BACKEND_SIMULATOR:
            return "simulator";
        case LOCKER_BACKEND_HARDWARE:
            return "hardware";
        default:
            return "unknown";
    }
}

locker_status_t locker_logger_init(locker_logger_t *logger, locker_log_sink_fn sink, void *user,
                                   const char *component, locker_backend_kind_t backend,
                                   locker_log_level_t min_level, const locker_clock_port_t *clock) {
    if (logger == NULL || sink == NULL || component == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    if (min_level != LOCKER_LOG_LEVEL_ERROR && min_level != LOCKER_LOG_LEVEL_WARN &&
        min_level != LOCKER_LOG_LEVEL_INFO && min_level != LOCKER_LOG_LEVEL_DEBUG) {
        return LOCKER_STATUS_INVALID;
    }
    if (backend != LOCKER_BACKEND_SIMULATOR && backend != LOCKER_BACKEND_HARDWARE) {
        return LOCKER_STATUS_INVALID;
    }
    if (clock == NULL) {
        logger->clock = locker_clock_port_real();
    } else {
        if (!locker_clock_port_is_valid(clock)) {
            return LOCKER_STATUS_INVALID;
        }
        logger->clock = *clock;
    }
    logger->user = user;
    logger->sink = sink;
    logger->component = component;
    logger->min_level = min_level;
    logger->backend = backend;
    logger->initialized = true;
    return LOCKER_STATUS_OK;
}

void locker_logger_deinit(locker_logger_t *logger) {
    if (logger == NULL) {
        return;
    }
    logger->user = NULL;
    logger->sink = NULL;
    logger->component = NULL;
    logger->initialized = false;
}

bool locker_logger_enabled(const locker_logger_t *logger, locker_log_level_t level) {
    if (logger == NULL || !logger->initialized) {
        return false;
    }
    if (level != LOCKER_LOG_LEVEL_ERROR && level != LOCKER_LOG_LEVEL_WARN &&
        level != LOCKER_LOG_LEVEL_INFO && level != LOCKER_LOG_LEVEL_DEBUG) {
        return false;
    }
    return (int)level <= (int)logger->min_level;
}

locker_status_t locker_logger_log(locker_logger_t *logger, locker_log_level_t level, const char *event,
                                  const locker_log_field_t *fields, size_t field_count) {
    if (logger == NULL || !logger->initialized || event == NULL || logger->sink == NULL ||
        logger->component == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    if (fields == NULL && field_count != 0u) {
        return LOCKER_STATUS_INVALID;
    }
    if (!locker_logger_enabled(logger, level)) {
        return LOCKER_STATUS_OK;
    }

    char line[LOCKER_LOG_LINE_MAX];
    text_writer_t writer;
    writer_init(&writer, line, sizeof(line));

    uint64_t monotonic_ms = 0;
    if (locker_clock_port_monotonic_ms(&logger->clock, &monotonic_ms) != LOCKER_STATUS_OK) {
        monotonic_ms = 0;
    }
    writer_puts(&writer, "ts=");
    writer_put_u64(&writer, monotonic_ms);

    uint64_t wall_ms = 0;
    if (locker_clock_port_wall_utc_ms(&logger->clock, &wall_ms) == LOCKER_STATUS_OK) {
        writer_puts(&writer, " utc=");
        writer_put_u64(&writer, wall_ms);
    } else {
        writer_puts(&writer, " utc=unavailable");
    }

    writer_puts(&writer, " level=");
    writer_puts(&writer, locker_log_level_name(level));
    writer_puts(&writer, " backend=");
    writer_puts(&writer, locker_log_backend_name(logger->backend));

    char component[LOCKER_LOG_KEY_MAX];
    sanitize_token(logger->component, component, sizeof(component));
    writer_puts(&writer, " component=");
    writer_puts(&writer, component);

    char event_name[LOCKER_LOG_KEY_MAX];
    sanitize_token(event, event_name, sizeof(event_name));
    writer_puts(&writer, " event=");
    writer_puts(&writer, event_name);

    for (size_t i = 0; i < field_count; ++i) {
        const char *key = fields[i].key;
        const char *value = fields[i].value;
        if (key == NULL || key[0] == '\0') {
            continue;
        }
        char sanitized_key[LOCKER_LOG_KEY_MAX];
        sanitize_token(key, sanitized_key, sizeof(sanitized_key));
        if (sanitized_key[0] == '\0') {
            continue;
        }
        writer_putc(&writer, ' ');
        writer_puts(&writer, sanitized_key);
        writer_putc(&writer, '=');
        if (locker_log_key_is_sensitive(key) || locker_log_key_is_sensitive(sanitized_key)) {
            writer_puts(&writer, LOCKER_LOG_REDACTED);
            continue;
        }
        char scrubbed[LOCKER_LOG_VALUE_MAX];
        locker_log_scrub(value, scrubbed, sizeof(scrubbed));
        bool needs_quotes = false;
        for (size_t c = 0; scrubbed[c] != '\0'; ++c) {
            if (is_space_char(scrubbed[c])) {
                needs_quotes = true;
                break;
            }
        }
        if (needs_quotes) {
            writer_putc(&writer, '"');
            writer_puts(&writer, scrubbed);
            writer_putc(&writer, '"');
        } else {
            writer_puts(&writer, scrubbed);
        }
    }

    if (writer.truncated) {
        const size_t size = sizeof(line);
        if (size >= 4u) {
            line[size - 4u] = '.';
            line[size - 3u] = '.';
            line[size - 2u] = '.';
            line[size - 1u] = '\0';
        }
    }
    logger->sink(logger->user, level, line);
    return LOCKER_STATUS_OK;
}
