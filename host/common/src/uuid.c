#include "locker/common/uuid.h"

#include <string.h>

static bool is_lower_hex(char c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
}

static int hex_value(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    return -1;
}

static bool is_hyphen_position(size_t index) {
    return index == 8u || index == 13u || index == 18u || index == 23u;
}

bool locker_uuid_is_nil(const locker_uuid_t *uuid) {
    if (uuid == NULL) {
        return false;
    }
    for (size_t i = 0; i < sizeof(uuid->bytes); ++i) {
        if (uuid->bytes[i] != 0u) {
            return false;
        }
    }
    return true;
}

bool locker_uuid_equal(const locker_uuid_t *lhs, const locker_uuid_t *rhs) {
    if (lhs == NULL || rhs == NULL) {
        return false;
    }
    return memcmp(lhs->bytes, rhs->bytes, sizeof(lhs->bytes)) == 0;
}

void locker_uuid_clear(locker_uuid_t *uuid) {
    if (uuid != NULL) {
        memset(uuid->bytes, 0, sizeof(uuid->bytes));
    }
}

locker_status_t locker_uuid_parse(const char *text, size_t length, locker_uuid_t *out_uuid) {
    if (text == NULL || out_uuid == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    if (length != 36u) {
        return LOCKER_STATUS_INVALID;
    }
    locker_uuid_t parsed;
    memset(&parsed, 0, sizeof(parsed));
    size_t byte_index = 0;
    int high_nibble = -1;
    for (size_t i = 0; i < 36u; ++i) {
        char c = text[i];
        if (is_hyphen_position(i)) {
            if (c != '-') {
                return LOCKER_STATUS_INVALID;
            }
            continue;
        }
        if (!is_lower_hex(c)) {
            return LOCKER_STATUS_INVALID;
        }
        int value = hex_value(c);
        if (high_nibble < 0) {
            high_nibble = value;
        } else {
            parsed.bytes[byte_index] = (uint8_t)((high_nibble << 4) | value);
            byte_index += 1u;
            high_nibble = -1;
        }
    }
    if (byte_index != sizeof(parsed.bytes) || high_nibble >= 0) {
        return LOCKER_STATUS_INVALID;
    }
    *out_uuid = parsed;
    return LOCKER_STATUS_OK;
}

locker_status_t locker_uuid_format(const locker_uuid_t *uuid, char *out_text, size_t out_size) {
    static const char hex_digits[] = "0123456789abcdef";
    if (uuid == NULL || out_text == NULL || out_size < 37u) {
        return LOCKER_STATUS_INVALID;
    }
    size_t out = 0;
    for (size_t i = 0; i < sizeof(uuid->bytes); ++i) {
        if (is_hyphen_position(out)) {
            out_text[out] = '-';
            out += 1u;
        }
        out_text[out] = hex_digits[(uuid->bytes[i] >> 4) & 0x0Fu];
        out += 1u;
        out_text[out] = hex_digits[uuid->bytes[i] & 0x0Fu];
        out += 1u;
    }
    out_text[out] = '\0';
    return (out == 36u) ? LOCKER_STATUS_OK : LOCKER_STATUS_INVALID;
}

bool locker_uuid_is_rfc4122_v4(const locker_uuid_t *uuid) {
    if (uuid == NULL) {
        return false;
    }
    return (uuid->bytes[6] & 0xF0u) == 0x40u && (uuid->bytes[8] & 0xC0u) == 0x80u;
}
