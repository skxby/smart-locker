/* Restricted SWD-only S2-04 diagnostic image. Never linked into the normal
   application. No UART, Control, physical input simulation or lock action. */
#include <string.h>
#include "stm32f4xx_hal.h"
#include "FreeRTOS.h"
#include "task.h"
#include "peripherals.h"
#include "timebase.h"
#include "journal.h"
#include "w25q128.h"
#include "board_validation.h"

volatile uint32_t s204_diag[32];
volatile uint8_t s204_data[8192];
volatile uint32_t s204_command, s204_address, s204_length;
mcu_journal_state_t s204_state;
locker_event_batch_t s204_page;
static w25q128_t flash;
static mcu_journal_t *journal;
static bool in_storage;
static uint32_t next_token;
static locker_status_t receipt;
static const mcu_journal_config_t geometry = {true, 0xef4018UL, 16777216UL, 4096U, 256U, 0U};
typedef char page_fits_mailbox[(sizeof(locker_event_batch_t) <= sizeof(s204_data)) ? 1 : -1];

static locker_status_t spi(void *ctx, const uint8_t *tx, uint8_t *rx, uint16_t n)
{ (void)ctx; return mcu_board_spi_transfer(BOARD_SPI_FLASH, tx, rx, n, 50000U); }
static uint64_t now(void *ctx) { (void)ctx; return mcu_board_monotonic_ms(); }
static void wait_ms(void *ctx, uint32_t ms) { (void)ctx; vTaskDelay(pdMS_TO_TICKS(ms)); }
static void lock(void *ctx) { (void)ctx; taskENTER_CRITICAL(); }
static void unlock(void *ctx) { (void)ctx; taskEXIT_CRITICAL(); }
static void wait(void *ctx) { (void)ctx; vTaskDelay(pdMS_TO_TICKS(1U)); }
/* There is exactly one application task in this diagnostic target. */
static bool owner(void *ctx) { (void)ctx; return in_storage && __get_IPSR() == 0U; }
static uint32_t token(void) { if (++next_token == 0U) ++next_token; return next_token; }
static locker_status_t committed(void *ctx, uint32_t t, locker_status_t result)
{
    (void)ctx; s204_diag[24] = t; s204_diag[25] = result; ++s204_diag[26]; receipt = result;
    return LOCKER_STATUS_OK;
}
static locker_status_t event_committed(void *ctx, const locker_device_event_t *event)
{ (void)ctx; (void)event; ++s204_diag[27]; return LOCKER_STATUS_OK; }
static locker_status_t control_result(void *ctx, uint32_t t, locker_status_t result, const mcu_journal_state_t *state)
{
    (void)ctx; s204_diag[24] = t; s204_diag[25] = result; ++s204_diag[28]; receipt = result;
    if (state != NULL) s204_state = *state;
    return LOCKER_STATUS_OK;
}
static locker_status_t attach(void)
{
    mcu_journal_port_t port = {0};
    port.fs = w25q128_lfs_config(&flash); port.lock = lock; port.unlock = unlock;
    port.wait = wait; port.is_owner = owner;
    return mcu_journal_attach(&port);
}
static locker_status_t run_queued(locker_status_t queued)
{
    if (queued != LOCKER_STATUS_QUEUED) return queued;
    receipt = LOCKER_STATUS_UNKNOWN;
    (void)mcu_journal_process_one(journal);
    return receipt;
}
static locker_status_t recover(void)
{
    mcu_journal_control_request_t request = {0};
    request.kind = MCU_JOURNAL_RECOVER; request.token = token();
    return run_queued(mcu_journal_control(journal, &request));
}
static locker_status_t initialize_flash(void)
{
    locker_status_t result;
    flash.transfer = spi; flash.now_ms = now; flash.wait_ms = wait_ms;
    result = w25q128_init(&flash, &geometry);
    s204_diag[2] = result; s204_diag[3] = flash.jedec_id;
    return result;
}
static void device_id(char id[25])
{
    const volatile uint8_t *uid = (const volatile uint8_t *)0x1fff7a10UL;
    static const char hex[] = "0123456789abcdef";
    unsigned i;
    for (i = 0U; i < 12U; ++i) { id[2U*i] = hex[uid[i] >> 4]; id[2U*i+1U] = hex[uid[i] & 15U]; }
    id[24] = '\0';
}
static locker_status_t provision(void)
{
    locker_uuid_t epoch;
    char id[25];
    unsigned i;
    locker_status_t result;
    if (journal != NULL) return LOCKER_STATUS_BUSY;
    result = initialize_flash(); if (result != LOCKER_STATUS_OK) return result;
    result = attach(); if (result != LOCKER_STATUS_OK) return result;
    for (i = 0U; i < 16U; ++i) epoch.bytes[i] = s204_data[i];
    device_id(id);
    return mcu_journal_provision(&geometry, &epoch, id);
}
static locker_status_t mount_journal(void)
{
    mcu_journal_sink_t sink = {NULL, committed, event_committed, control_result};
    locker_status_t result;
    if (journal != NULL) return LOCKER_STATUS_BUSY;
    result = initialize_flash(); if (result != LOCKER_STATUS_OK) return result;
    result = attach(); if (result != LOCKER_STATUS_OK) return result;
    result = mcu_journal_init(&geometry, &journal);
    if (result == LOCKER_STATUS_OK) result = mcu_journal_bind_sink(journal, &sink);
    if (result == LOCKER_STATUS_OK) result = recover();
    return result;
}
static locker_status_t diagnostic_intent(void)
{
    locker_unlock_intent_t intent = {0};
    locker_status_t result = recover();
    if (result != LOCKER_STATUS_OK) return result;
    if (s204_state.action_watermark == UINT64_MAX) return LOCKER_STATUS_NO_SPACE;
    intent.journal_epoch = s204_state.journal_epoch;
    intent.expected_boot_id = s204_state.boot_id;
    intent.action_no = s204_state.action_watermark + 1U;
    intent.session_id.bytes[0] = 0xd1U; intent.session_id.bytes[15] = 1U;
    intent.operation_id.bytes[0] = 0xd2U; intent.operation_id.bytes[15] = 2U;
    intent.command_id.bytes[0] = 0xd3U; intent.command_id.bytes[15] = 3U;
    intent.slot_id = 1U; intent.kind = LOCKER_OPERATION_KIND_BORROW;
    intent.not_after_mcu_ms = mcu_board_monotonic_ms() + 1000U;
    return run_queued(mcu_journal_commit_intent(journal, &intent, token()));
}
static locker_status_t finish_or_release(bool release)
{
    mcu_journal_control_request_t request = {0};
    locker_status_t result = recover();
    if (result != LOCKER_STATUS_OK) return result;
    if (!s204_state.has_active) return LOCKER_STATUS_NOT_READY;
    request.kind = release ? MCU_JOURNAL_RELEASE : MCU_JOURNAL_FINALIZE;
    request.token = token(); request.journal_epoch = s204_state.journal_epoch;
    request.expected_boot_id = s204_state.boot_id;
    request.session_id = s204_state.active_intent.session_id;
    request.operation_id = s204_state.active_intent.operation_id;
    request.command_id = s204_state.active_intent.command_id;
    request.action_no = s204_state.active_intent.action_no;
    request.terminal_state = LOCKER_COMMAND_STATE_REJECTED;
    return run_queued(mcu_journal_control(journal, &request));
}
static void pair(unsigned index, uint64_t value)
{ s204_diag[index] = (uint32_t)value; s204_diag[index+1U] = (uint32_t)(value >> 32); }
static void publish_state(void)
{
    locker_event_key_t after = {0};
    locker_event_batch_t page;
    unsigned count = 0U, pages;
    if (journal != NULL) {
        /* All commands have drained their one-element FIFO. These page RPCs
           run on Storage and only copy committed RAM state, not native bytes
           to UART. A hard cap prevents a corrupt cursor from looping. */
        after.journal_epoch = s204_state.journal_epoch;
        memcpy(after.device_id, s204_state.last_committed.device_id, 25U);
        for (pages = 0U; pages < 16U; ++pages) {
            if (mcu_journal_read_page(journal, &after, 4U, &page) != LOCKER_STATUS_OK) break;
            count += page.count; after = page.next_after;
            if (!page.has_more) break;
        }
    }
    s204_diag[4] = journal != NULL; pair(5U, s204_state.boot_id); pair(7U, s204_state.action_watermark);
    s204_diag[9] = s204_state.has_active; s204_diag[10] = s204_state.command_state; s204_diag[11] = count;
    pair(12U, s204_state.first_available.boot_id); pair(14U, s204_state.first_available.event_seq);
    pair(16U, s204_state.last_committed.boot_id); pair(18U, s204_state.last_committed.event_seq);
    pair(20U, s204_state.ack_persisted.boot_id); pair(22U, s204_state.ack_persisted.event_seq);
    s204_diag[29] = s204_state.journal_state; s204_diag[30] = s204_state.continuity_ok;
}
static locker_status_t command(uint32_t command_no)
{
    locker_status_t result;
    unsigned i;
    if (command_no == 1U) {
        uint8_t buffer[256];
        uint32_t address = s204_address, length = s204_length, off;
        if (length == 0U || length > sizeof(s204_data) || address >= 16777216UL || length > 16777216UL-address)
            return LOCKER_STATUS_INVALID;
        for (off = 0U; off < length; off += sizeof(buffer)) {
            uint32_t n = length-off < sizeof(buffer) ? length-off : sizeof(buffer);
            result = w25q128_read(&flash, address+off, buffer, n);
            if (result != LOCKER_STATUS_OK) return result;
            for (i = 0U; i < n; ++i) s204_data[off+i] = buffer[i];
        }
        return LOCKER_STATUS_OK;
    }
    if (command_no == 2U) return provision();
    if (command_no == 3U) return mount_journal();
    if (journal == NULL) return LOCKER_STATUS_NOT_READY;
    switch (command_no) {
    case 4U: {
        locker_device_event_t fact = {0};
        fact.event_type = LOCKER_WIRE_DEVICE_EVENT_TYPE_HEALTH_CHANGED;
        fact.detail = 0x53323034UL; fact.mono_ms = mcu_board_monotonic_ms();
        return run_queued(mcu_journal_append(journal, &fact, token()));
    }
    case 5U: return diagnostic_intent();
    case 6U: return finish_or_release(false);
    case 7U: return finish_or_release(true);
    case 8U: return mcu_journal_ack(journal, &s204_state.last_committed);
    case 9U:
        result = mcu_journal_shutdown(journal);
        if (result == LOCKER_STATUS_OK) journal = NULL;
        return result;
    case 10U: {
        locker_event_key_t after = {0};
        after.journal_epoch = s204_state.journal_epoch;
        memcpy(after.device_id, s204_state.last_committed.device_id, 25U);
        result = mcu_journal_read_page(journal, &after, 4U, &s204_page);
        if (result == LOCKER_STATUS_OK)
            for (i = 0U; i < sizeof(s204_page); ++i) s204_data[i] = ((const uint8_t *)&s204_page)[i];
        return result;
    }
    case 11U: return recover();
    default: return LOCKER_STATUS_INVALID;
    }
}
void s204_storage_task(void *argument)
{
    (void)argument; in_storage = true;
    s204_diag[0] = 1U; s204_diag[1] = initialize_flash();
    __DMB(); s204_diag[0] = 2U;
    for (;;) {
        uint32_t requested = s204_command;
        if (requested != 0U) {
            locker_status_t result;
            ++s204_diag[0]; __DMB();
            result = command(requested);
            /* Keep the command result even though RECOVER has its own receipt. */
            if (journal != NULL) (void)recover();
            s204_diag[1] = result; publish_state(); ++s204_diag[31];
            __DMB(); ++s204_diag[0]; __DMB(); s204_command = 0U;
        }
        vTaskDelay(pdMS_TO_TICKS(1U));
    }
}
