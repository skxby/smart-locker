#ifndef LOCKER_TEST_SUPPORT_H
#define LOCKER_TEST_SUPPORT_H

/*
 * Minimal assertion harness for the S1-01 host unit tests.
 *
 * The project takes no third-party test dependency (offline build, locked
 * sources only): every test file is one executable registered with CTest and a
 * non-zero exit status is the only failure signal. Nothing here can turn a
 * failed check into a pass, and there is no skip mechanism.
 */

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "locker_contracts.h"

static int locker_test_checks;
static int locker_test_failures;

#define TEST_CHECK(condition)                                                    \
    do {                                                                         \
        locker_test_checks += 1;                                                 \
        if (!(condition)) {                                                      \
            locker_test_failures += 1;                                           \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        }                                                                        \
    } while (0)

#define TEST_CHECK_STATUS(expected, actual)                                          \
    do {                                                                             \
        locker_status_t locker_test_expected_ = (expected);                          \
        locker_status_t locker_test_actual_ = (actual);                              \
        locker_test_checks += 1;                                                     \
        if (locker_test_expected_ != locker_test_actual_) {                          \
            locker_test_failures += 1;                                               \
            fprintf(stderr, "FAIL %s:%d: expected status %d, got %d\n", __FILE__,    \
                    __LINE__, (int)locker_test_expected_, (int)locker_test_actual_); \
        }                                                                            \
    } while (0)

#define TEST_CHECK_STRING(expected, actual)                                           \
    do {                                                                              \
        const char *locker_test_expected_ = (expected);                               \
        const char *locker_test_actual_ = (actual);                                   \
        locker_test_checks += 1;                                                      \
        if (locker_test_expected_ == NULL || locker_test_actual_ == NULL ||           \
            strcmp(locker_test_expected_, locker_test_actual_) != 0) {                \
            locker_test_failures += 1;                                                \
            fprintf(stderr, "FAIL %s:%d: expected %s, got %s\n", __FILE__, __LINE__, \
                    locker_test_expected_ ? locker_test_expected_ : "(null)",         \
                    locker_test_actual_ ? locker_test_actual_ : "(null)");            \
        }                                                                             \
    } while (0)

#define TEST_CHECK_U64(expected, actual)                                           \
    do {                                                                           \
        uint64_t locker_test_expected_ = (uint64_t)(expected);                     \
        uint64_t locker_test_actual_ = (uint64_t)(actual);                         \
        locker_test_checks += 1;                                                   \
        if (locker_test_expected_ != locker_test_actual_) {                        \
            locker_test_failures += 1;                                             \
            fprintf(stderr, "FAIL %s:%d: expected %" PRIu64 ", got %" PRIu64 "\n", \
                    __FILE__, __LINE__, locker_test_expected_, locker_test_actual_); \
        }                                                                          \
    } while (0)

#define TEST_REPORT(name)                                                 \
    do {                                                                  \
        if (locker_test_failures == 0) {                                  \
            printf("PASS %s (%d checks)\n", (name), locker_test_checks);  \
            return 0;                                                     \
        }                                                                 \
        fprintf(stderr, "FAIL %s (%d of %d checks failed)\n", (name),     \
                locker_test_failures, locker_test_checks);                \
        return 1;                                                         \
    } while (0)

/* True when needle occurs in haystack; used by the log redaction tests. The
   attribute keeps -Wunused-function quiet in test files that do not need it. */
#if defined(__GNUC__)
__attribute__((unused))
#endif
static bool test_text_contains(const char *haystack, const char *needle) {
    if (haystack == NULL || needle == NULL) {
        return false;
    }
    return strstr(haystack, needle) != NULL;
}

#endif /* LOCKER_TEST_SUPPORT_H */
