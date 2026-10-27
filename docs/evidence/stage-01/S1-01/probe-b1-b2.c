/*
 * Reproduction probe for docs/evidence/stage-01/S1-01/audit.md findings B1 and B2.
 * Kept as evidence so the remediation can be re-checked against the exact inputs
 * the auditor used. Build (from the repository root):
 *   gcc -std=c11 -Wall -Wextra -Wpedantic -Werror -Ihost/common/include \
 *       -Icontracts/include docs/evidence/stage-01/S1-01/probe-b1-b2.c \
 *       host/common/src/log.c host/common/src/clock.c -lpthread -o /tmp/probe
 */
#include <stdio.h>
#include <string.h>

#include "locker/common/log.h"

static char g_last[4096];

static void sink(void *user, locker_log_level_t level, const char *line) {
    (void)user;
    (void)level;
    snprintf(g_last, sizeof(g_last), "%s", line);
}

static void show(const char *label, const char *needle) {
    const char *at = strstr(g_last, needle);
    printf("%s %s\n", label, at != NULL ? at : "(field not found)");
}

int main(void) {
    locker_logger_t logger;
    memset(&logger, 0, sizeof(logger));
    if (locker_logger_init(&logger, sink, NULL, "probe", LOCKER_BACKEND_SIMULATOR,
                           LOCKER_LOG_LEVEL_DEBUG, NULL) != LOCKER_STATUS_OK) {
        printf("INIT_FAILED\n");
        return 2;
    }

    /* B1: "Bearer <jwt>" in free text - the credential must be masked in full. */
    const locker_log_field_t bearer[] = {{"note", "Bearer part1.part2.part3"}};
    (void)locker_logger_log(&logger, LOCKER_LOG_LEVEL_INFO, "b1_bearer", bearer, 1u);
    show("B1_BEARER", "note=");

    /* B1: quoted JSON password containing a space - one value, not two tokens. */
    const locker_log_field_t quoted[] = {{"note", "{\"password\":\"alpha beta\"}"}};
    (void)locker_logger_log(&logger, LOCKER_LOG_LEVEL_INFO, "b1_quoted", quoted, 1u);
    show("B1_QUOTED", "note=");

    /* B1 follow-up: key spellings that used to bypass the exact-name deny list. */
    const locker_log_field_t variants[] = {
        {"X-Token", "AUDIT_SECRET"},
        {"authToken", "AUDIT_SECRET"},
        {"reader_card_uid", "AUDIT_SECRET"},
        {"tls.private_key", "AUDIT_SECRET"},
    };
    (void)locker_logger_log(&logger, LOCKER_LOG_LEVEL_INFO, "b1_variants", variants, 4u);
    printf("B1_VARIANTS_LEAK=%d\n", strstr(g_last, "AUDIT_SECRET") != NULL ? 1 : 0);

    /* B2: the 17th secret in one value - no span budget may let it through. */
    char value[512];
    size_t used = 0;
    for (size_t i = 0; i < 16u; ++i) {
        used += (size_t)snprintf(value + used, sizeof(value) - used, "token=a ");
    }
    (void)snprintf(value + used, sizeof(value) - used, "token=LAST_SECRET");
    const locker_log_field_t budget[] = {{"note", value}};
    (void)locker_logger_log(&logger, LOCKER_LOG_LEVEL_INFO, "b2_budget", budget, 1u);
    printf("B2_17TH_LEAK=%d\n", strstr(g_last, "LAST_SECRET") != NULL ? 1 : 0);
    show("B2_SINK", "note=");

    locker_logger_deinit(&logger);
    return 0;
}
