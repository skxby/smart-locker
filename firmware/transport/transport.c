#include "transport.h"
#include <string.h>

static bool uuid_equal(const locker_uuid_t *a, const locker_uuid_t *b)
{
    return memcmp(a->bytes, b->bytes, 16u) == 0;
}

static mcu_transport_packet_t *reserve_packet(mcu_transport_t *t)
{
    if (t->tx_count == MCU_TRANSPORT_TX_SLOTS) {
        ++t->stats.tx_full;
        t->fault_latched = true;
        return NULL;
    }
    return &t->tx[(t->tx_head + t->tx_count) % MCU_TRANSPORT_TX_SLOTS];
}

static void error_response(mcu_transport_t *t, const locker_wire_frame_t *f,
    const locker_uuid_t *session, uint16_t result)
{
    mcu_transport_packet_t *packet = reserve_packet(t);
    uint8_t payload[18];
    uint16_t crc;
    if (packet == NULL) return;
    memcpy(payload, session->bytes, 16u);
    payload[16] = (uint8_t)result;
    payload[17] = (uint8_t)(result >> 8);
    /* Unknown request types also receive UNSUPPORTED, with their original type.
     * The shared encoder deliberately accepts only known DTO message types. */
    packet->bytes[0] = 0x4cu; packet->bytes[1] = 0x4bu;
    packet->bytes[2] = 1u; packet->bytes[3] = (uint8_t)(f->type | 0x80u);
    packet->bytes[4] = 18u; packet->bytes[5] = 0u;
    packet->bytes[6] = (uint8_t)f->request_no;
    packet->bytes[7] = (uint8_t)(f->request_no >> 8);
    packet->bytes[8] = (uint8_t)(f->request_no >> 16);
    packet->bytes[9] = (uint8_t)(f->request_no >> 24);
    memcpy(&packet->bytes[10], payload, sizeof(payload));
    crc = locker_wire_crc16(&packet->bytes[2], 26u);
    packet->bytes[28] = (uint8_t)crc;
    packet->bytes[29] = (uint8_t)(crc >> 8);
    packet->length = 30u;
    ++t->tx_count;
}

static locker_status_t handle_frame(void *user, const locker_wire_frame_t *f)
{
    mcu_transport_t *t = user;
    locker_wire_request_union_t request;
    locker_uuid_t session;
    mcu_request_context_t context;
    locker_wire_response_union_t response;
    mcu_transport_packet_t *packet;
    uint16_t result;
    locker_status_t status;
    memset(&session, 0, sizeof(session));
    if (f->payload_len >= 16u) memcpy(session.bytes, f->payload, 16u);
    if (f->request_no == 0u || f->type >= 0x40u || f->type == 0u) {
        ++t->stats.bad_payload;
        return LOCKER_STATUS_OK;
    }
    result = locker_wire_decode_request(f, &request);
    if (result != LOCKER_WIRE_RESULT_OK) {
        if (result == LOCKER_WIRE_RESULT_BAD_VERSION) ++t->stats.bad_version;
        else if (result == LOCKER_WIRE_RESULT_UNSUPPORTED) ++t->stats.unsupported;
        else ++t->stats.bad_payload;
        error_response(t, f, &session, result);
        return LOCKER_STATUS_OK;
    }
    ++t->stats.requests[f->type - 1u];
    if (f->type == 1u) {
        /* A session UUID may not be rebound to another host. */
        if (t->session_valid && uuid_equal(&request.hello.new_session_id, &t->session_id) &&
            !uuid_equal(&request.hello.host_instance_id, &t->host_instance_id)) {
            error_response(t, f, &session, LOCKER_WIRE_RESULT_BAD_SESSION);
            ++t->stats.bad_session;
            return LOCKER_STATUS_OK;
        }
        packet = reserve_packet(t);
        if (packet == NULL) return LOCKER_STATUS_OK;
        memset(&response, 0, sizeof(response));
        response.hello.session_id = request.hello.new_session_id;
        memcpy(response.hello.device_uid, t->device_uid, 12u);
        response.hello.firmware_minor = 2u;
        response.hello.firmware_patch = 1u;
        response.hello.capabilities = t->capabilities;
        response.hello.device_mode = LOCKER_WIRE_WIRE_DEVICE_MODE_DEGRADED;
        response.hello.journal_state = LOCKER_WIRE_WIRE_JOURNAL_STATE_UNINITIALIZED;
        if (locker_wire_encode_response(1u, f->request_no, &response,
                packet->bytes, sizeof(packet->bytes), &packet->length) == LOCKER_STATUS_OK) {
            ++t->tx_count;
            t->session_id = request.hello.new_session_id;
            t->host_instance_id = request.hello.host_instance_id;
            t->session_valid = true;
        }
        return LOCKER_STATUS_OK;
    }
    if (!t->session_valid || !uuid_equal(&session, &t->session_id)) {
        ++t->stats.bad_session;
        error_response(t, f, &session, LOCKER_WIRE_RESULT_BAD_SESSION);
        return LOCKER_STATUS_OK;
    }
    if (f->type == 9u && !uuid_equal(&request.host_health.host_instance_id, &t->host_instance_id)) {
        ++t->stats.bad_session;
        error_response(t, f, &session, LOCKER_WIRE_RESULT_BAD_SESSION);
        return LOCKER_STATUS_OK;
    }
    /* Do not admit work whose response cannot be queued. A sticky transport
     * failure blocks all future EXECUTE requests until explicit reinitialization. */
    if (t->tx_count == MCU_TRANSPORT_TX_SLOTS) {
        (void)reserve_packet(t);
        return LOCKER_STATUS_OK;
    }
    if (t->dispatch == NULL || (f->type == 7u && t->fault_latched)) {
        error_response(t, f, &session, LOCKER_WIRE_RESULT_NOT_READY);
        return LOCKER_STATUS_OK;
    }
    context.session_id = session;
    context.request_no = f->request_no;
    context.message_type = f->type;
    context.received_mono_ms = t->received_mono_ms;
    status = t->dispatch(t->user, &context, &request);
    if (status == LOCKER_STATUS_ACCEPTED || status == LOCKER_STATUS_QUEUED) return LOCKER_STATUS_OK;
    /* OK alone is not evidence of a committed asynchronous operation. */
    result = (status == LOCKER_STATUS_BUSY || status == LOCKER_STATUS_NO_SPACE) ?
        LOCKER_WIRE_RESULT_BUSY : LOCKER_WIRE_RESULT_NOT_READY;
    error_response(t, f, &session, result);
    return LOCKER_STATUS_OK;
}

void mcu_transport_init(mcu_transport_t *t, const uint8_t uid[12],
    uint32_t capabilities, void *user, mcu_transport_dispatch_t dispatch)
{
    memset(t, 0, sizeof(*t));
    if (uid != NULL) memcpy(t->device_uid, uid, 12u);
    /* S2-03 cannot advertise persistent Journal, ADC qualification or pulses. */
    t->capabilities = capabilities & (LOCKER_WIRE_CAP_DOOR_AND_ITEM | LOCKER_WIRE_CAP_CARD_READER);
    t->user = user;
    t->dispatch = dispatch;
    locker_wire_stream_init(&t->stream, t, handle_frame);
}

void mcu_transport_feed(mcu_transport_t *t, const uint8_t *bytes, size_t length, uint64_t mono)
{
    t->received_mono_ms = mono;
    (void)locker_wire_stream_feed(&t->stream, bytes, length);
}

void mcu_transport_rx_lost(mcu_transport_t *t)
{
    ++t->stats.rx_lost;
    t->fault_latched = true;
    t->session_valid = false;
    locker_wire_stream_reset(&t->stream);
}

void mcu_transport_dma_event(mcu_transport_t *t, const uint8_t dma[LOCKER_UART_DMA_BYTES],
    uint16_t size, uint8_t kind, uint64_t mono)
{
    uint16_t previous = t->dma_position;
    if (dma == NULL || size == 0u || size > LOCKER_UART_DMA_BYTES ||
        kind < MCU_DMA_EVENT_HT || kind > MCU_DMA_EVENT_IDLE ||
        (kind == MCU_DMA_EVENT_HT && size != LOCKER_UART_DMA_BYTES / 2u) ||
        (kind == MCU_DMA_EVENT_TC && size != LOCKER_UART_DMA_BYTES)) {
        ++t->stats.dma_invalid;
        mcu_transport_rx_lost(t);
        return;
    }
    if (size == previous || (kind == MCU_DMA_EVENT_HT && previous > size &&
        previous < LOCKER_UART_DMA_BYTES)) {
        ++t->stats.dma_duplicates;
        return;
    }
    if (size < previous) {
        mcu_transport_feed(t, &dma[previous], LOCKER_UART_DMA_BYTES - previous, mono);
        previous = 0u;
    }
    mcu_transport_feed(t, &dma[previous], size - previous, mono);
    t->dma_position = size;
}

const mcu_transport_packet_t *mcu_transport_tx_peek(const mcu_transport_t *t)
{
    return t->tx_count == 0u ? NULL : &t->tx[t->tx_head];
}

void mcu_transport_tx_pop(mcu_transport_t *t)
{
    if (t->tx_count != 0u) {
        t->tx_head = (t->tx_head + 1u) % MCU_TRANSPORT_TX_SLOTS;
        --t->tx_count;
    }
}

locker_status_t mcu_transport_queue_reply(mcu_transport_t *t, const locker_device_reply_t *reply)
{
    locker_wire_response_union_t response;
    mcu_transport_packet_t *packet;
    locker_status_t status;
    if (reply == NULL || reply->message_type < 1u || reply->message_type > 11u ||
        reply->request_no == 0u) return LOCKER_STATUS_INVALID;
    memset(&response, 0, sizeof(response));
#define COPY_REPLY(n, field) case n: response.field = reply->body.field; break
    switch (reply->message_type) {
        COPY_REPLY(1u, hello); COPY_REPLY(2u, get_snapshot); COPY_REPLY(3u, fetch_events);
        COPY_REPLY(4u, ack_persisted); COPY_REPLY(5u, resume_ready); COPY_REPLY(6u, disarm);
        COPY_REPLY(7u, execute_unlock); COPY_REPLY(8u, query_command); COPY_REPLY(9u, host_health);
        COPY_REPLY(10u, time_anchor); COPY_REPLY(11u, release_context);
        default: return LOCKER_STATUS_INVALID;
    }
#undef COPY_REPLY
    if (!t->session_valid || !uuid_equal(&response.hello.session_id, &t->session_id)) {
        ++t->stats.stale_replies;
        return LOCKER_STATUS_CONFLICT;
    }
    packet = reserve_packet(t);
    if (packet == NULL) return LOCKER_STATUS_BUSY;
    status = locker_wire_encode_response(reply->message_type, reply->request_no, &response,
        packet->bytes, sizeof(packet->bytes), &packet->length);
    if (status == LOCKER_STATUS_OK) ++t->tx_count;
    return status;
}

locker_status_t mcu_transport_queue_event(mcu_transport_t *t, const locker_device_event_t *event)
{
    locker_wire_event96_t record;
    mcu_transport_packet_t *packet;
    locker_status_t status;
    if (event == NULL || !t->session_valid || event->key.boot_id == 0u ||
        event->key.event_seq == 0u || locker_wire_uuid_nil(&event->key.journal_epoch))
        return LOCKER_STATUS_NOT_READY;
    memset(&record, 0, sizeof(record));
    record.boot_id = event->key.boot_id; record.event_seq = event->key.event_seq;
    record.mono_ms = event->mono_ms; record.physical_revision = event->physical_revision;
    record.detail = event->detail; record.event_type = event->event_type;
    record.reason = event->reason; record.flags = event->flags;
    record.operation_id = event->operation_id; record.command_id = event->command_id;
    record.door = (uint8_t)event->door; record.item = (uint8_t)event->item;
    record.supply_mv = event->supply_mv; record.card_uid_len = event->card_uid_len;
    memcpy(record.card_uid, event->card_uid, sizeof(record.card_uid));
    packet = reserve_packet(t);
    if (packet == NULL) return LOCKER_STATUS_BUSY;
    status = locker_wire_encode_device_event(&t->session_id, &event->key.journal_epoch,
        &record, packet->bytes, sizeof(packet->bytes), &packet->length);
    if (status == LOCKER_STATUS_OK) ++t->tx_count;
    return status;
}
