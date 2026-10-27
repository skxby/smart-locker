#ifndef LOCKER_COMMON_LOG_H
#define LOCKER_COMMON_LOG_H

/*
 * S1-01 host infrastructure: bounded, redacting diagnostic logging.
 *
 * Rules (docs/architecture/monitoring-interfaces.md:214, stage-00:178):
 *   - tokens, passwords, private keys and raw card UIDs never reach the sink;
 *   - every line is built in a fixed 512-byte stack buffer, so a log call cannot
 *     grow without bound and the logger holds no mutable shared state;
 *   - lines carry "backend=simulator" during stage 1 (stage-01-task-cards.md:19);
 *   - the timestamp comes from an injected locker_clock_port_t, so tests are
 *     deterministic while production passes the real clock.
 *
 * Redaction is key-based plus a value scrubber. A key is sensitive when, after
 * lower-casing and dropping '_', '-', '.', ':' and '/', it equals or ends with a
 * deny entry - so "token", "X-Token", "authToken", "reader_card_uid" and
 * "tls.private_key" are all caught, while "device_id"/"operation_id" stay
 * loggable. The suffix test runs on the whole original key, so key length is not
 * limited and a long field name ending in "_token" is still redacted. The
 * scrubber then masks, inside free text:
 *   - "<scheme> <credential>" for bearer/basic/digest, where the credential is
 *     consumed to its natural end ('.', '~', '+', '/', '=' included);
 *   - "<sensitive key>=<value>" / "<sensitive key>: <value>", quoted values
 *     included in both '"' and '\'' form, with escapes and an unterminated string
 *     handled by masking the remainder (fail closed);
 *   - the same key form with a bearer/basic/digest scheme before the value.
 * The scrubber streams into the output buffer, so there is no per-value span
 * budget that could run out and let later secrets through.
 *
 * Limits callers must respect: this is defence in depth, not a data-flow proof.
 * A secret typed into prose without a sensitive key and without a
 * key=value/space-scheme shape (for example "the card was 04A2B3C4") is not
 * detectable by pattern; pass secrets as fields under a sensitive key.
 *
 * Threading: locker_logger_t holds no shared mutable state, so one logger may be
 * used from several threads provided the sink itself is thread safe. init/deinit
 * are not thread safe against concurrent log calls: deinit only after the last
 * log call returns (or after joining the threads that log).
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "locker_contracts.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LOCKER_LOG_LINE_MAX 512u
#define LOCKER_LOG_REDACTED "[redacted]"

typedef enum locker_log_level {
    LOCKER_LOG_LEVEL_ERROR = 0,
    LOCKER_LOG_LEVEL_WARN = 1,
    LOCKER_LOG_LEVEL_INFO = 2,
    LOCKER_LOG_LEVEL_DEBUG = 3
} locker_log_level_t;

typedef enum locker_backend_kind {
    LOCKER_BACKEND_SIMULATOR = 0,
    LOCKER_BACKEND_HARDWARE = 1
} locker_backend_kind_t;

typedef void (*locker_log_sink_fn)(void *user, locker_log_level_t level, const char *line);

typedef struct locker_log_field {
    const char *key;
    const char *value;
} locker_log_field_t;

typedef struct locker_logger {
    void *user;
    locker_log_sink_fn sink;
    const char *component;
    locker_clock_port_t clock;
    locker_log_level_t min_level;
    locker_backend_kind_t backend;
    bool initialized;
} locker_logger_t;

/* clock == NULL selects the real clock; component is borrowed and must outlive. */
locker_status_t locker_logger_init(locker_logger_t *logger, locker_log_sink_fn sink, void *user,
                                   const char *component, locker_backend_kind_t backend,
                                   locker_log_level_t min_level, const locker_clock_port_t *clock);
void locker_logger_deinit(locker_logger_t *logger);
bool locker_logger_enabled(const locker_logger_t *logger, locker_log_level_t level);
/* Filtered levels return OK without calling the sink. */
locker_status_t locker_logger_log(locker_logger_t *logger, locker_log_level_t level, const char *event,
                                  const locker_log_field_t *fields, size_t field_count);

const char *locker_log_level_name(locker_log_level_t level);
const char *locker_log_backend_name(locker_backend_kind_t backend);
bool locker_log_key_is_sensitive(const char *key);
/* Masks secrets in free text; returns the number of masked spans. */
size_t locker_log_scrub(const char *input, char *out_text, size_t out_size);

#ifdef __cplusplus
}
#endif

#endif /* LOCKER_COMMON_LOG_H */
