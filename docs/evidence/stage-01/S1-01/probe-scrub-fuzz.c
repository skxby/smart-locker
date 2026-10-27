/*
 * Deterministic robustness check for locker_log_scrub(), kept as evidence for the
 * audit.md B1/B2 remediation. It feeds pseudo-random mixtures of sensitive keys,
 * credentials, separators and quotes through the scrubber and asserts that the
 * output is always NUL terminated and inside the caller's buffer, at both full and
 * tiny (17 byte) buffer sizes. Run under ASan/UBSan:
 *
 *   gcc -std=c11 -O1 -g -Wall -Wextra -Wpedantic -Werror \
 *       -fsanitize=address,undefined -fno-sanitize-recover=all \
 *       -Ihost/common/include -Icontracts/include \
 *       docs/evidence/stage-01/S1-01/probe-scrub-fuzz.c \
 *       host/common/src/log.c host/common/src/clock.c -lpthread -o /tmp/fuzz
 *   /tmp/fuzz
 *
 * The PRNG is a fixed-seed xorshift64, so a failure is reproducible.
 */
#include <stdio.h>
#include <string.h>

#include "locker/common/log.h"

static unsigned long g_state = 0x2545F4914F6CDD1DUL;

static unsigned long next_rand(void) {
    g_state ^= g_state << 13;
    g_state ^= g_state >> 7;
    g_state ^= g_state << 17;
    return g_state;
}

static const char *const k_alphabet = "abcXYZ019_-.:/=\"'\\ {}[](),;&*#@!~%^|?+<>\t\n";

static const char *const k_words[] = {
    "token",  "password",        "Bearer",       "X-Token",        "authToken",
    "reader_card_uid", "tls.private_key", "secret", "device_id",  "Authorization",
    "Basic",  "note",
};

int main(void) {
    char input[600];
    char out[LOCKER_LOG_LINE_MAX];
    const size_t alpha_len = strlen(k_alphabet);
    const size_t word_count = sizeof(k_words) / sizeof(k_words[0]);
    const size_t iterations = 200000u;
    size_t max_out = 0;
    size_t failures = 0;

    for (size_t iter = 0; iter < iterations; ++iter) {
        size_t len = 0;
        const size_t target = (size_t)(next_rand() % (sizeof(input) - 1u));
        while (len < target) {
            if ((next_rand() % 4u) == 0u) {
                const char *word = k_words[next_rand() % word_count];
                const size_t word_len = strlen(word);
                if (len + word_len + 1u >= sizeof(input)) {
                    break;
                }
                memcpy(input + len, word, word_len);
                len += word_len;
            } else {
                input[len] = k_alphabet[next_rand() % alpha_len];
                len += 1u;
            }
        }
        input[len] = '\0';

        (void)locker_log_scrub(input, out, sizeof(out));
        const size_t out_len = strlen(out);
        if (out_len >= sizeof(out)) {
            failures += 1u;
        }
        if (out_len > max_out) {
            max_out = out_len;
        }

        /* Truncation path: the result must stay inside the small buffer. */
        char small[17];
        (void)locker_log_scrub(input, small, sizeof(small));
        if (strlen(small) >= sizeof(small)) {
            failures += 1u;
        }
    }

    printf("FUZZ_ITERATIONS=%zu\n", iterations);
    printf("FUZZ_MAX_OUT=%zu (buffer %u)\n", max_out, (unsigned)LOCKER_LOG_LINE_MAX);
    printf("FUZZ_FAILURES=%zu\n", failures);
    return failures == 0u ? 0 : 1;
}
