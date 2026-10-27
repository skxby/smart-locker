#ifndef LOCKER_COMMON_NUMERIC_H
#define LOCKER_COMMON_NUMERIC_H

/*
 * S1-01 host infrastructure: uint64_t boundary helpers.
 *
 * External u64 values can exceed the SQLite signed integer range, so the storage
 * adapter keeps them as decimal text and compares them numerically, never by
 * plain lexicographic order (docs/architecture/module-ownership.md:57). These
 * helpers define the only accepted decimal spelling: digits only, no sign, no
 * whitespace, no leading zeros (except "0"), at most 20 digits.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "locker_contracts.h"

#ifdef __cplusplus
extern "C" {
#endif

bool locker_u64_is_canonical_decimal(const char *text, size_t length);
locker_status_t locker_u64_parse_decimal(const char *text, size_t length, uint64_t *out_value);
/* Writes the canonical decimal form plus NUL; needs out_size >= digits + 1. */
locker_status_t locker_u64_format_decimal(uint64_t value, char *out_text, size_t out_size);
/* Numeric comparison of two canonical decimal strings: out_order -1/0/1. */
locker_status_t locker_u64_text_compare(const char *lhs, size_t lhs_length, const char *rhs,
                                        size_t rhs_length, int *out_order);
bool locker_u64_add_checked(uint64_t lhs, uint64_t rhs, uint64_t *out_sum);

#ifdef __cplusplus
}
#endif

#endif /* LOCKER_COMMON_NUMERIC_H */
