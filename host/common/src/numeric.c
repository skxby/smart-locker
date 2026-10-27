#include "locker/common/numeric.h"

#include <string.h>

#define LOCKER_U64_DECIMAL_MAX_DIGITS 20u

bool locker_u64_is_canonical_decimal(const char *text, size_t length) {
    if (text == NULL || length == 0 || length > LOCKER_U64_DECIMAL_MAX_DIGITS) {
        return false;
    }
    if (length > 1u && text[0] == '0') {
        return false;
    }
    for (size_t i = 0; i < length; ++i) {
        if (text[i] < '0' || text[i] > '9') {
            return false;
        }
    }
    return true;
}

locker_status_t locker_u64_parse_decimal(const char *text, size_t length, uint64_t *out_value) {
    if (out_value == NULL || !locker_u64_is_canonical_decimal(text, length)) {
        return LOCKER_STATUS_INVALID;
    }
    uint64_t value = 0;
    for (size_t i = 0; i < length; ++i) {
        uint64_t digit = (uint64_t)(text[i] - '0');
        if (value > (UINT64_MAX - digit) / 10u) {
            return LOCKER_STATUS_INVALID;
        }
        value = value * 10u + digit;
    }
    *out_value = value;
    return LOCKER_STATUS_OK;
}

locker_status_t locker_u64_format_decimal(uint64_t value, char *out_text, size_t out_size) {
    char digits[LOCKER_U64_DECIMAL_MAX_DIGITS];
    size_t count = 0;
    uint64_t remaining = value;
    do {
        digits[count] = (char)('0' + (remaining % 10u));
        count += 1u;
        remaining /= 10u;
    } while (remaining != 0u);
    if (out_text == NULL || out_size < count + 1u) {
        return LOCKER_STATUS_INVALID;
    }
    for (size_t i = 0; i < count; ++i) {
        out_text[i] = digits[count - 1u - i];
    }
    out_text[count] = '\0';
    return LOCKER_STATUS_OK;
}

locker_status_t locker_u64_text_compare(const char *lhs, size_t lhs_length, const char *rhs,
                                        size_t rhs_length, int *out_order) {
    uint64_t lhs_value = 0;
    uint64_t rhs_value = 0;
    if (out_order == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    if (locker_u64_parse_decimal(lhs, lhs_length, &lhs_value) != LOCKER_STATUS_OK ||
        locker_u64_parse_decimal(rhs, rhs_length, &rhs_value) != LOCKER_STATUS_OK) {
        return LOCKER_STATUS_INVALID;
    }
    if (lhs_value < rhs_value) {
        *out_order = -1;
    } else if (lhs_value > rhs_value) {
        *out_order = 1;
    } else {
        *out_order = 0;
    }
    return LOCKER_STATUS_OK;
}

bool locker_u64_add_checked(uint64_t lhs, uint64_t rhs, uint64_t *out_sum) {
    if (out_sum == NULL) {
        return false;
    }
    if (lhs > UINT64_MAX - rhs) {
        return false;
    }
    *out_sum = lhs + rhs;
    return true;
}
