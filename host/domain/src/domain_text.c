#define _POSIX_C_SOURCE 200809L

#include "domain_internal.h"

#include <string.h>

size_t domain_strnlen(const char *text, size_t max_length)
{
    if (text == NULL) {
        return 0u;
    }
    size_t length = 0u;
    while (length < max_length && text[length] != '\0') {
        length += 1u;
    }
    return length;
}

bool domain_text_copy(char *destination, size_t destination_size, const char *source)
{
    if (destination == NULL || destination_size == 0u || source == NULL) {
        return false;
    }
    size_t length = strlen(source);
    if (length + 1u > destination_size) {
        return false;
    }
    memcpy(destination, source, length + 1u);
    return true;
}

bool domain_text_equal(const char *left, const char *right)
{
    if (left == NULL || right == NULL) {
        return false;
    }
    return strcmp(left, right) == 0;
}

bool domain_uuid_to_text(const locker_uuid_t *uuid, char *out, size_t out_size)
{
    return locker_uuid_format(uuid, out, out_size) == LOCKER_STATUS_OK;
}

bool domain_uuid_from_text(const char *text, size_t length, locker_uuid_t *out)
{
    return locker_uuid_parse(text, length, out) == LOCKER_STATUS_OK;
}

bool domain_uuid_text_valid(const char *text, size_t length)
{
    locker_uuid_t parsed;
    return domain_uuid_from_text(text, length, &parsed);
}

static const char *const k_state_names[] = {"REQUESTED",      "ACCESS_PENDING", "ACCESS_ACTIVE",
                                            "WAIT_CONFIRM",   "REVIEW_REQUIRED", "COMPLETED",
                                            "CANCELLED",      "NO_CHANGE",       "FAILED"};

const char *domain_state_name(uint8_t state)
{
    if (state >= sizeof(k_state_names) / sizeof(k_state_names[0])) {
        return NULL;
    }
    return k_state_names[state];
}

const char *domain_kind_name(uint8_t kind)
{
    switch (kind) {
    case LOCKER_OPERATION_KIND_BORROW:
        return "BORROW";
    case LOCKER_OPERATION_KIND_RETURN:
        return "RETURN";
    default:
        return NULL;
    }
}

const char *domain_role_name(uint8_t role)
{
    switch (role) {
    case LOCKER_ROLE_USER:
        return "USER";
    case LOCKER_ROLE_ADMIN:
        return "ADMIN";
    default:
        return NULL;
    }
}

const char *domain_door_name(uint8_t door)
{
    switch (door) {
    case LOCKER_DOOR_STATE_UNKNOWN:
        return "UNKNOWN";
    case LOCKER_DOOR_STATE_CLOSED:
        return "CLOSED";
    case LOCKER_DOOR_STATE_OPEN:
        return "OPEN";
    default:
        return NULL;
    }
}

const char *domain_item_name(uint8_t item)
{
    switch (item) {
    case LOCKER_ITEM_STATE_UNKNOWN:
        return "UNKNOWN";
    case LOCKER_ITEM_STATE_ABSENT:
        return "ABSENT";
    case LOCKER_ITEM_STATE_PRESENT:
        return "PRESENT";
    default:
        return NULL;
    }
}

const char *domain_mode_name(uint8_t mode)
{
    switch (mode) {
    case LOCKER_DEVICE_MODE_OFFLINE:
        return "OFFLINE";
    case LOCKER_DEVICE_MODE_RECONCILING:
        return "RECONCILING";
    case LOCKER_DEVICE_MODE_READY:
        return "READY";
    case LOCKER_DEVICE_MODE_DEGRADED:
        return "DEGRADED";
    default:
        return NULL;
    }
}

const char *domain_time_quality_name(uint8_t quality)
{
    switch (quality) {
    case LOCKER_TIME_QUALITY_UNSYNCED:
        return "UNSYNCED";
    case LOCKER_TIME_QUALITY_ESTIMATED:
        return "ESTIMATED";
    case LOCKER_TIME_QUALITY_HOST_SYNCED:
        return "HOST_SYNCED";
    default:
        return NULL;
    }
}

const char *domain_action_name(uint8_t action)
{
    switch (action) {
    case LOCKER_ALLOWED_ACTION_PRESENT_AT_CABINET:
        return "PRESENT_AT_CABINET";
    case LOCKER_ALLOWED_ACTION_WAIT:
        return "WAIT";
    case LOCKER_ALLOWED_ACTION_CONFIRM:
        return "CONFIRM";
    case LOCKER_ALLOWED_ACTION_CANCEL:
        return "CANCEL";
    case LOCKER_ALLOWED_ACTION_ADMIN_REVIEW:
        return "ADMIN_REVIEW";
    default:
        return NULL;
    }
}

const char *domain_proof_name(uint8_t proof)
{
    switch (proof) {
    case 1u:
        return "CARD";
    case 2u:
        return "LOCAL_UI";
    default:
        return NULL;
    }
}

const char *domain_outbox_state_name(uint8_t state)
{
    switch (state) {
    case 1u:
        return "PREPARED";
    case 2u:
        return "SEND_ATTEMPTED";
    case 3u:
        return "ACCEPTED";
    case 4u:
        return "UNRESOLVED";
    case 5u:
        return "FINISHED";
    case 6u:
        return "REJECTED";
    default:
        return NULL;
    }
}

const char *domain_availability_name(uint8_t availability)
{
    switch (availability) {
    case LOCKER_TOOL_AVAILABILITY_AVAILABLE:
        return "AVAILABLE";
    case LOCKER_TOOL_AVAILABILITY_RESERVED:
        return "RESERVED";
    case LOCKER_TOOL_AVAILABILITY_BORROWED:
        return "BORROWED";
    case LOCKER_TOOL_AVAILABILITY_REVIEW:
        return "REVIEW";
    case LOCKER_TOOL_AVAILABILITY_UNAVAILABLE:
        return "UNAVAILABLE";
    default:
        return NULL;
    }
}

static bool name_matches(const char *text, size_t length, const char *name)
{
    if (name == NULL) {
        return false;
    }
    size_t name_length = strlen(name);
    return name_length == length && memcmp(text, name, length) == 0;
}

bool domain_state_from_name(const char *text, size_t length, uint8_t *out_state)
{
    if (text == NULL || out_state == NULL) {
        return false;
    }
    for (uint8_t i = 0u; i < (uint8_t)(sizeof(k_state_names) / sizeof(k_state_names[0])); ++i) {
        if (name_matches(text, length, k_state_names[i])) {
            *out_state = i;
            return true;
        }
    }
    return false;
}

bool domain_kind_from_name(const char *text, size_t length, uint8_t *out_kind)
{
    if (text == NULL || out_kind == NULL) {
        return false;
    }
    if (name_matches(text, length, "BORROW")) {
        *out_kind = LOCKER_OPERATION_KIND_BORROW;
        return true;
    }
    if (name_matches(text, length, "RETURN")) {
        *out_kind = LOCKER_OPERATION_KIND_RETURN;
        return true;
    }
    return false;
}

bool domain_role_from_name(const char *text, size_t length, uint8_t *out_role)
{
    if (text == NULL || out_role == NULL) {
        return false;
    }
    if (name_matches(text, length, "USER")) {
        *out_role = LOCKER_ROLE_USER;
        return true;
    }
    if (name_matches(text, length, "ADMIN")) {
        *out_role = LOCKER_ROLE_ADMIN;
        return true;
    }
    return false;
}

bool domain_resolution_from_name(const char *text, size_t length, uint8_t *out_resolution)
{
    if (text == NULL || out_resolution == NULL) {
        return false;
    }
    if (name_matches(text, length, "COMPLETE_BORROW")) {
        *out_resolution = LOCKER_RESOLUTION_COMPLETE_BORROW;
        return true;
    }
    if (name_matches(text, length, "COMPLETE_RETURN")) {
        *out_resolution = LOCKER_RESOLUTION_COMPLETE_RETURN;
        return true;
    }
    if (name_matches(text, length, "NO_CHANGE")) {
        *out_resolution = LOCKER_RESOLUTION_NO_CHANGE;
        return true;
    }
    return false;
}

bool domain_terminal_state(uint8_t state)
{
    return state == LOCKER_OPERATION_STATE_COMPLETED || state == LOCKER_OPERATION_STATE_CANCELLED ||
           state == LOCKER_OPERATION_STATE_NO_CHANGE || state == LOCKER_OPERATION_STATE_FAILED;
}

/* 状态优先级：物理事实只能把操作推到「更强的已发生事实」，不能被更弱的事实回退。
   例如已经见过 PULSE_STARTED（ACCESS_ACTIVE）之后迟到的 COMMAND_ACCEPTED 不能把
   状态拉回 ACCESS_PENDING。数值越大越强。 */
bool domain_state_rank(uint8_t state, uint8_t *out_rank)
{
    if (out_rank == NULL) {
        return false;
    }
    switch (state) {
    case LOCKER_OPERATION_STATE_REQUESTED:
        *out_rank = 1u;
        return true;
    case LOCKER_OPERATION_STATE_ACCESS_PENDING:
        *out_rank = 2u;
        return true;
    case LOCKER_OPERATION_STATE_ACCESS_ACTIVE:
        *out_rank = 3u;
        return true;
    case LOCKER_OPERATION_STATE_WAIT_CONFIRM:
        *out_rank = 4u;
        return true;
    case LOCKER_OPERATION_STATE_REVIEW_REQUIRED:
        *out_rank = 5u;
        return true;
    case LOCKER_OPERATION_STATE_COMPLETED:
    case LOCKER_OPERATION_STATE_CANCELLED:
    case LOCKER_OPERATION_STATE_NO_CHANGE:
    case LOCKER_OPERATION_STATE_FAILED:
        *out_rank = 6u;
        return true;
    default:
        return false;
    }
}

uint8_t domain_state_from_wire_rank(uint8_t rank)
{
    switch (rank) {
    case 1u:
        return LOCKER_OPERATION_STATE_REQUESTED;
    case 2u:
        return LOCKER_OPERATION_STATE_ACCESS_PENDING;
    case 3u:
        return LOCKER_OPERATION_STATE_ACCESS_ACTIVE;
    case 4u:
        return LOCKER_OPERATION_STATE_WAIT_CONFIRM;
    case 5u:
        return LOCKER_OPERATION_STATE_REVIEW_REQUIRED;
    default:
        return LOCKER_OPERATION_STATE_REVIEW_REQUIRED;
    }
}

uint64_t domain_now(locker_core_t *core)
{
    return core->now_ms;
}

uint64_t domain_now_advance(locker_core_t *core, uint64_t candidate)
{
    if (candidate > core->now_ms) {
        core->now_ms = candidate;
    }
    return core->now_ms;
}
