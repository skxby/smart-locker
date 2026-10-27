#include "locker/storage/event_key.h"

#include <string.h>

#include "locker/common/uuid.h"
#include "locker/storage/u64.h"

#define LOCKER_STORAGE_DEVICE_ID_LEN 24u
#define LOCKER_STORAGE_UUID_TEXT_LEN 36u
#define LOCKER_STORAGE_U64_TEXT_LEN 20u
#define LOCKER_STORAGE_IMPORT_CURSOR_LEN (LOCKER_STORAGE_DEVICE_ID_LEN + 1u + LOCKER_STORAGE_UUID_TEXT_LEN)
#define LOCKER_STORAGE_EVENT_KEY_LEN \
    (LOCKER_STORAGE_IMPORT_CURSOR_LEN + 1u + LOCKER_STORAGE_U64_TEXT_LEN + 1u + LOCKER_STORAGE_U64_TEXT_LEN)

static bool is_lower_hex(char character)
{
    return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f');
}

static bool device_id_is_valid(const char *text)
{
    for (size_t index = 0u; index < LOCKER_STORAGE_DEVICE_ID_LEN; ++index) {
        if (!is_lower_hex(text[index])) {
            return false;
        }
    }
    return true;
}

/* 20 位补零字段专用解析：允许前导零（补零是格式的一部分），但必须是 20 位纯十进制
   且不超过 UINT64_MAX。不能复用 locker_storage_u64_parse——它按「规范十进制文本」
   拒绝前导零，用于存储值而不是定宽游标字段。 */
static bool parse_u64_padded(const char *text, uint64_t *out)
{
    uint64_t value = 0u;
    for (size_t index = 0u; index < LOCKER_STORAGE_U64_TEXT_LEN; ++index) {
        const char character = text[index];
        if (character < '0' || character > '9') {
            return false;
        }
        const uint64_t digit = (uint64_t)(character - '0');
        if (value > (UINT64_MAX - digit) / 10u) {
            return false;
        }
        value = value * 10u + digit;
    }
    *out = value;
    return true;
}

static bool append_u64_padded(char *out, size_t out_size, size_t offset, uint64_t value)
{
    char digits[LOCKER_STORAGE_U64_TEXT_LEN + 1u];
    if (!locker_storage_u64_format(value, digits, sizeof(digits))) {
        return false;
    }
    const size_t length = strlen(digits);
    if (length > LOCKER_STORAGE_U64_TEXT_LEN) {
        return false;
    }
    const size_t padding = LOCKER_STORAGE_U64_TEXT_LEN - length;
    if (offset + padding + length + 1u > out_size) {
        return false;
    }
    for (size_t index = 0u; index < padding; ++index) {
        out[offset + index] = '0';
    }
    memcpy(out + offset + padding, digits, length);
    return true;
}

bool locker_storage_event_key_is_zero(const locker_event_key_t *key)
{
    if (key == NULL) {
        return false;
    }
    return key->boot_id == 0u && key->event_seq == 0u;
}

bool locker_storage_event_key_build(const locker_event_key_t *key, char *out, size_t out_size)
{
    char uuid_text[LOCKER_STORAGE_UUID_TEXT_LEN + 1u];
    if (key == NULL || out == NULL || out_size < LOCKER_STORAGE_EVENT_KEY_LEN + 1u) {
        return false;
    }
    if (!device_id_is_valid(key->device_id) || key->device_id[LOCKER_STORAGE_DEVICE_ID_LEN] != '\0') {
        return false;
    }
    if (locker_uuid_format(&key->journal_epoch, uuid_text, sizeof(uuid_text)) != LOCKER_STATUS_OK) {
        return false;
    }
    if (locker_storage_event_key_is_zero(key)) {
        return false;
    }
    memcpy(out, key->device_id, LOCKER_STORAGE_DEVICE_ID_LEN);
    out[LOCKER_STORAGE_DEVICE_ID_LEN] = '/';
    memcpy(out + LOCKER_STORAGE_DEVICE_ID_LEN + 1u, uuid_text, LOCKER_STORAGE_UUID_TEXT_LEN);
    out[LOCKER_STORAGE_IMPORT_CURSOR_LEN] = '/';
    if (!append_u64_padded(out, out_size, LOCKER_STORAGE_IMPORT_CURSOR_LEN + 1u, key->boot_id)) {
        return false;
    }
    out[LOCKER_STORAGE_IMPORT_CURSOR_LEN + 1u + LOCKER_STORAGE_U64_TEXT_LEN] = '/';
    if (!append_u64_padded(out, out_size, LOCKER_STORAGE_IMPORT_CURSOR_LEN + 2u + LOCKER_STORAGE_U64_TEXT_LEN,
                           key->event_seq)) {
        return false;
    }
    out[LOCKER_STORAGE_EVENT_KEY_LEN] = '\0';
    return true;
}

bool locker_storage_import_cursor_key_build(const locker_event_key_t *key, char *out, size_t out_size)
{
    char uuid_text[LOCKER_STORAGE_UUID_TEXT_LEN + 1u];
    if (key == NULL || out == NULL || out_size < LOCKER_STORAGE_IMPORT_CURSOR_KEY_TEXT_MAX) {
        return false;
    }
    if (!device_id_is_valid(key->device_id) || key->device_id[LOCKER_STORAGE_DEVICE_ID_LEN] != '\0') {
        return false;
    }
    if (locker_uuid_format(&key->journal_epoch, uuid_text, sizeof(uuid_text)) != LOCKER_STATUS_OK) {
        return false;
    }
    memcpy(out, key->device_id, LOCKER_STORAGE_DEVICE_ID_LEN);
    out[LOCKER_STORAGE_DEVICE_ID_LEN] = '/';
    memcpy(out + LOCKER_STORAGE_DEVICE_ID_LEN + 1u, uuid_text, LOCKER_STORAGE_UUID_TEXT_LEN);
    out[LOCKER_STORAGE_IMPORT_CURSOR_LEN] = '\0';
    return true;
}

bool locker_storage_event_key_parse(const char *text, locker_event_key_t *out)
{
    char uuid_text[LOCKER_STORAGE_UUID_TEXT_LEN + 1u];
    if (text == NULL || out == NULL) {
        return false;
    }
    if (strlen(text) != LOCKER_STORAGE_EVENT_KEY_LEN) {
        return false;
    }
    if (!device_id_is_valid(text)) {
        return false;
    }
    if (text[LOCKER_STORAGE_DEVICE_ID_LEN] != '/' ||
        text[LOCKER_STORAGE_IMPORT_CURSOR_LEN] != '/' ||
        text[LOCKER_STORAGE_IMPORT_CURSOR_LEN + 1u + LOCKER_STORAGE_U64_TEXT_LEN] != '/') {
        return false;
    }
    memcpy(uuid_text, text + LOCKER_STORAGE_DEVICE_ID_LEN + 1u, LOCKER_STORAGE_UUID_TEXT_LEN);
    uuid_text[LOCKER_STORAGE_UUID_TEXT_LEN] = '\0';
    memset(out, 0, sizeof(*out));
    memcpy(out->device_id, text, LOCKER_STORAGE_DEVICE_ID_LEN);
    out->device_id[LOCKER_STORAGE_DEVICE_ID_LEN] = '\0';
    if (locker_uuid_parse(uuid_text, LOCKER_STORAGE_UUID_TEXT_LEN, &out->journal_epoch) != LOCKER_STATUS_OK) {
        return false;
    }
    if (!parse_u64_padded(text + LOCKER_STORAGE_IMPORT_CURSOR_LEN + 1u, &out->boot_id)) {
        return false;
    }
    if (!parse_u64_padded(text + LOCKER_STORAGE_IMPORT_CURSOR_LEN + 2u + LOCKER_STORAGE_U64_TEXT_LEN,
                          &out->event_seq)) {
        return false;
    }
    return true;
}

int locker_storage_event_key_compare(const locker_event_key_t *left, const locker_event_key_t *right)
{
    if (left == NULL || right == NULL) {
        return (left == right) ? 0 : (left == NULL ? -1 : 1);
    }
    const int device_diff = memcmp(left->device_id, right->device_id, LOCKER_STORAGE_DEVICE_ID_LEN);
    if (device_diff != 0) {
        return device_diff < 0 ? -1 : 1;
    }
    const int epoch_diff = memcmp(left->journal_epoch.bytes, right->journal_epoch.bytes, sizeof(left->journal_epoch.bytes));
    if (epoch_diff != 0) {
        return epoch_diff < 0 ? -1 : 1;
    }
    if (left->boot_id != right->boot_id) {
        return left->boot_id < right->boot_id ? -1 : 1;
    }
    if (left->event_seq != right->event_seq) {
        return left->event_seq < right->event_seq ? -1 : 1;
    }
    return 0;
}

bool locker_storage_event_key_next_seq(const locker_event_key_t *current, locker_event_key_t *next)
{
    if (current == NULL || next == NULL) {
        return false;
    }
    if (current->event_seq == UINT64_MAX) {
        return false;
    }
    *next = *current;
    next->event_seq = current->event_seq + 1u;
    return true;
}
