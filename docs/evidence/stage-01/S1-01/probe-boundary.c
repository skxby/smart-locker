/*
 * Reproduction probe for docs/evidence/stage-01/S1-01/audit.md findings B1-R and
 * B3 (second audit round). Kept as evidence so the remediation can be re-checked
 * against the exact inputs the auditor used. Build (from the repository root):
 *   gcc -std=c11 -Wall -Wextra -Wpedantic -Werror -Ihost/common/include \
 *       -Icontracts/include docs/evidence/stage-01/S1-01/probe-boundary.c \
 *       host/common/src/log.c host/common/src/clock.c -lpthread -o /tmp/probe
 *
 * Exit status: 0 = no leak, 1 = at least one leak (LEAK_CASES > 0).
 */
#include <stdio.h>
#include <string.h>

#include "locker/common/log.h"

static char g_last[4096];
static int g_leaks;

static void sink(void *user, locker_log_level_t level, const char *line) {
    (void)user;
    (void)level;
    snprintf(g_last, sizeof(g_last), "%s", line);
}

static void log_field(locker_logger_t *logger, const char *event, const char *key, const char *value) {
    const locker_log_field_t field = {key, value};
    if (locker_logger_log(logger, LOCKER_LOG_LEVEL_INFO, event, &field, 1u) != LOCKER_STATUS_OK) {
        printf("%s LOG_FAILED\n", event);
        g_leaks += 1;
        return;
    }
    printf("%s %s\n", event, g_last);
}

static void expect_no_leak(const char *event, const char *secret) {
    if (strstr(g_last, secret) != NULL) {
        printf("%s LEAK=1\n", event);
        g_leaks += 1;
    }
}

int main(void) {
    locker_logger_t logger;
    memset(&logger, 0, sizeof(logger));
    if (locker_logger_init(&logger, sink, NULL, "audit", LOCKER_BACKEND_SIMULATOR,
                           LOCKER_LOG_LEVEL_INFO, NULL) != LOCKER_STATUS_OK) {
        printf("INIT_FAILED\n");
        return 2;
    }

    /* B3: 63 'a' + "_token" (69 characters) used to be truncated to 63 and then
       judged benign, so the value reached the sink verbatim. */
    char long_key[80];
    memset(long_key, 'a', 63u);
    memcpy(long_key + 63u, "_token", 7u);
    printf("LONG_KEY_SENSITIVE=%d\n", locker_log_key_is_sensitive(long_key) ? 1 : 0);
    log_field(&logger, "long_key", long_key, "LONG_KEY_SECRET");
    expect_no_leak("long_key", "LONG_KEY_SECRET");

    /* B1-R: single-quoted password with a space. */
    log_field(&logger, "quoted_password", "note", "password='alpha beta'");
    expect_no_leak("quoted_password", "alpha beta");

    /* Adjacent spellings of the same two defects, checked so a partial fix does
       not look complete. */
    log_field(&logger, "json_single_quoted", "note", "{'password':'gamma delta'}");
    expect_no_leak("json_single_quoted", "gamma delta");

    log_field(&logger, "single_quoted_key", "note", "'password'='epsilon zeta'");
    expect_no_leak("single_quoted_key", "epsilon zeta");

    char long_pk[64];
    memset(long_pk, 'k', 40u);
    memcpy(long_pk + 40u, "-Private-Key", 13u);
    log_field(&logger, "long_private_key", long_pk, "LONG_PK_SECRET");
    expect_no_leak("long_private_key", "LONG_PK_SECRET");

    log_field(&logger, "unterminated_single", "note", "password='eta theta");
    expect_no_leak("unterminated_single", "eta theta");

    /* Controls: ordinary prose and a benign long key must survive unchanged. */
    log_field(&logger, "prose", "note", "it's a plain sentence");
    if (strstr(g_last, "it's a plain sentence") == NULL) {
        printf("prose OVER_MASKED=1\n");
        g_leaks += 1;
    }

    char benign[80];
    memset(benign, 'b', 70u);
    benign[70u] = '\0';
    printf("BENIGN_KEY_SENSITIVE=%d\n", locker_log_key_is_sensitive(benign) ? 1 : 0);
    if (locker_log_key_is_sensitive(benign)) {
        g_leaks += 1;
    }

    printf("LEAK_CASES=%d\n", g_leaks);
    locker_logger_deinit(&logger);
    return g_leaks == 0 ? 0 : 1;
}
