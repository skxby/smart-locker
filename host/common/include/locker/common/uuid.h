#ifndef LOCKER_COMMON_UUID_H
#define LOCKER_COMMON_UUID_H

/*
 * S1-01 host infrastructure: strict UUID text conversion for untrusted input.
 *
 * External identifiers are lowercase canonical UUID text (docs/architecture/
 * internal-interfaces.md:60). Parsing is deliberately strict: 36 characters,
 * hyphens at 8/13/18/23, lowercase hexadecimal only. Uppercase, braces, URN
 * prefixes, whitespace and short forms are rejected instead of normalised.
 */

#include <stdbool.h>
#include <stddef.h>

#include "locker_contracts.h"

#ifdef __cplusplus
extern "C" {
#endif

bool locker_uuid_is_nil(const locker_uuid_t *uuid);
bool locker_uuid_equal(const locker_uuid_t *lhs, const locker_uuid_t *rhs);
void locker_uuid_clear(locker_uuid_t *uuid);

/* Strict parse of exactly 36 bytes of canonical lowercase UUID text. */
locker_status_t locker_uuid_parse(const char *text, size_t length, locker_uuid_t *out_uuid);
/* Writes 36 lowercase characters plus NUL; needs out_size >= 37. */
locker_status_t locker_uuid_format(const locker_uuid_t *uuid, char *out_text, size_t out_size);
/* RFC 4122 version 4 with the variant bits set. */
bool locker_uuid_is_rfc4122_v4(const locker_uuid_t *uuid);

#ifdef __cplusplus
}
#endif

#endif /* LOCKER_COMMON_UUID_H */
