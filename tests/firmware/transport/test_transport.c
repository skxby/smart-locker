#include "transport.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static mcu_transport_t transport;
static uint8_t uid[12] = {1u};
static locker_uuid_t session = {{1u}}, host = {{2u}}, epoch = {{3u}};
static unsigned calls;
static mcu_request_context_t received[4];

static void init(void)
{
    mcu_transport_init(&transport, uid, 0xffffffffu, NULL, NULL);
}

static void build_request(uint8_t type, locker_wire_request_union_t *request)
{
    memset(request, 0, sizeof(*request));
    request->hello.session_id = session;
    switch (type) {
    case 1u:
        memset(&request->hello.session_id, 0, sizeof(session));
        request->hello.new_session_id = session;
        request->hello.host_instance_id = host;
        request->hello.requested_version = 1u;
        break;
    case 3u: request->fetch_events.journal_epoch = epoch; request->fetch_events.max_count = 4u; break;
    case 4u: request->ack_persisted.journal_epoch = epoch; break;
    case 5u: request->resume_ready.journal_epoch = epoch; break;
    case 7u:
        request->execute_unlock.journal_epoch = epoch;
        request->execute_unlock.operation_id = epoch;
        request->execute_unlock.command_id = host;
        request->execute_unlock.action_no = 1u;
        request->execute_unlock.slot_id = 1u;
        request->execute_unlock.kind = 1u;
        break;
    case 8u: request->query_command.journal_epoch = epoch; request->query_command.command_id = host; break;
    case 9u:
        request->host_health.host_instance_id = host;
        request->host_health.ui_observed_age_ms = 0xffffffffu;
        break;
    case 11u:
        request->release_context.journal_epoch = epoch;
        request->release_context.operation_id = epoch;
        request->release_context.command_id = host;
        break;
    default: break;
    }
}

static size_t encode(uint8_t type, uint32_t number, uint8_t *out)
{
    locker_wire_request_union_t request;
    size_t n = 0u;
    build_request(type, &request);
    assert(locker_wire_encode_request(type, number, &request, out, 524u, &n) == LOCKER_STATUS_OK);
    return n;
}

static void send_request(uint8_t type, uint32_t number)
{
    uint8_t bytes[524];
    size_t n = encode(type, number, bytes);
    mcu_transport_feed(&transport, bytes, n, 1234u);
}

static uint16_t reply_result(uint8_t type, uint32_t number)
{
    const mcu_transport_packet_t *p = mcu_transport_tx_peek(&transport);
    locker_wire_frame_t frame;
    locker_wire_response_union_t response;
    assert(p != NULL && p->bytes[3] == (uint8_t)(type | 0x80u));
    assert(p->bytes[6] == (uint8_t)number);
    assert(p->bytes[7] == (uint8_t)(number >> 8));
    assert(p->bytes[8] == (uint8_t)(number >> 16));
    assert(p->bytes[9] == (uint8_t)(number >> 24));
    frame.version = p->bytes[2]; frame.type = p->bytes[3];
    frame.request_no = number;
    frame.payload_len = (uint16_t)((uint16_t)p->bytes[4] | (uint16_t)((uint16_t)p->bytes[5] << 8));
    assert(p->length == frame.payload_len + 12u);
    memcpy(frame.payload, &p->bytes[10], frame.payload_len);
    assert(locker_wire_decode_response(&frame, &response) == LOCKER_WIRE_RESULT_OK);
    assert(locker_wire_crc16(&p->bytes[2], p->length - 4u) ==
        (uint16_t)((uint16_t)p->bytes[p->length - 2u] |
        (uint16_t)((uint16_t)p->bytes[p->length - 1u] << 8)));
    mcu_transport_tx_pop(&transport);
    return response.hello.result;
}

static void hello(void)
{
    send_request(1u, 1u);
    assert(reply_result(1u, 1u) == LOCKER_WIRE_RESULT_OK);
    assert(transport.session_valid && !memcmp(transport.session_id.bytes, session.bytes, 16u));
}

static void test_all_requests(void)
{
    uint8_t type;
    init(); hello();
    assert(transport.capabilities == 5u);
    for (type = 2u; type <= 11u; ++type) {
        send_request(type, 0x12340000u + type);
        assert(reply_result(type, 0x12340000u + type) == LOCKER_WIRE_RESULT_NOT_READY);
        assert(transport.stats.requests[type - 1u] == 1u);
    }
    assert(transport.stats.requests[0] == 1u);
}

static void test_splits_and_corruption(void)
{
    uint8_t bytes[524], concatenated[1048];
    size_t n, i;
    init();
    n = encode(1u, 1u, bytes);
    /* Every possible split of the golden-compatible HELLO layout. */
    for (i = 0u; i <= n; ++i) {
        init();
        mcu_transport_feed(&transport, bytes, i, 10u);
        if (i < n) assert(transport.tx_count == 0u);
        mcu_transport_feed(&transport, bytes + i, n - i, 11u);
        assert(reply_result(1u, 1u) == LOCKER_WIRE_RESULT_OK);
    }
    init(); hello();
    n = encode(2u, 2u, bytes);
    memcpy(concatenated, bytes, n); memcpy(concatenated + n, bytes, n);
    mcu_transport_feed(&transport, concatenated, n * 2u, 12u);
    assert(transport.tx_count == 2u);
    assert(reply_result(2u, 2u) == 3u); assert(reply_result(2u, 2u) == 3u);
    bytes[n-1u] ^= 1u;
    mcu_transport_feed(&transport, bytes, n, 12u);
    assert(transport.tx_count == 0u && transport.stream.stats.crc_errors == 1u);
    /* Oversize header followed immediately by a valid frame resynchronizes. */
    memcpy(concatenated, "LK\1\2\1\2\3\0\0\0", 10u);
    n = encode(2u, 3u, concatenated + 10u);
    mcu_transport_feed(&transport, concatenated, 10u + n, 12u);
    assert(transport.stream.stats.length_errors == 1u);
    assert(reply_result(2u, 3u) == 3u);
    /* CRC-valid bad version and fixed-layout mismatch get protocol errors. */
    n = encode(2u, 4u, bytes); bytes[2] = 2u;
    { uint16_t crc = locker_wire_crc16(bytes + 2u, n - 4u);
      bytes[n-2u] = (uint8_t)crc; bytes[n-1u] = (uint8_t)(crc >> 8); }
    mcu_transport_feed(&transport, bytes, n, 12u);
    assert(reply_result(2u, 4u) == 1u);
    assert(locker_wire_encode_frame(2u, 5u, bytes, 17u, concatenated, sizeof(concatenated), &n) == 0);
    mcu_transport_feed(&transport, concatenated, n, 12u);
    assert(reply_result(2u, 5u) == 10u);
}

static locker_status_t dispatch(void *user, const mcu_request_context_t *context,
    const locker_wire_request_union_t *request)
{
    (void)user; (void)request;
    assert(calls < 4u);
    received[calls++] = *context;
    return LOCKER_STATUS_QUEUED;
}

static void test_sessions_and_async(void)
{
    locker_device_reply_t reply;
    locker_uuid_t saved = session;
    init(); hello(); calls = 0u; transport.dispatch = dispatch;
    send_request(2u, 20u); send_request(2u, 21u);
    assert(calls == 2u && received[0].request_no == 20u && received[1].request_no == 21u);
    assert(received[0].message_type == 2u && received[0].received_mono_ms == 1234u);
    memset(&reply, 0, sizeof(reply));
    reply.message_type = 2u; reply.request_no = 21u;
    reply.body.get_snapshot.session_id = session; reply.body.get_snapshot.result = 3u;
    assert(mcu_transport_queue_reply(&transport, &reply) == 0);
    reply.request_no = 20u;
    assert(mcu_transport_queue_reply(&transport, &reply) == 0);
    assert(reply_result(2u, 21u) == 3u); assert(reply_result(2u, 20u) == 3u);
    session.bytes[0] = 9u; hello();
    assert(mcu_transport_queue_reply(&transport, &reply) == LOCKER_STATUS_CONFLICT);
    session = saved;
    send_request(7u, 22u);
    assert(reply_result(7u, 22u) == 2u && calls == 2u);
    assert(transport.stats.bad_session == 1u && transport.stats.stale_replies == 1u);
    /* Same session cannot switch host identity through HELLO. */
    session.bytes[0] = 9u; host.bytes[0] = 6u;
    send_request(1u, 23u); assert(reply_result(1u, 23u) == 2u);
    send_request(9u, 24u); assert(reply_result(9u, 24u) == 2u && calls == 2u);
    session = saved; host.bytes[0] = 2u;
}

static void test_backpressure(void)
{
    unsigned i;
    init(); hello(); calls = 0u;
    for (i = 0u; i < 8u; ++i) send_request(2u, 30u + i);
    assert(transport.tx_count == 4u && transport.stats.tx_full == 4u && transport.fault_latched);
    transport.dispatch = dispatch;
    send_request(7u, 40u); assert(calls == 0u);
    for (i = 0u; i < 4u; ++i) assert(reply_result(2u, 30u + i) == 3u);
    send_request(7u, 41u); assert(reply_result(7u, 41u) == 3u && calls == 0u);
    mcu_transport_feed(&transport, (const uint8_t *)"LK", 2u, 10u);
    mcu_transport_rx_lost(&transport);
    assert(!transport.session_valid && transport.stream.length == 0u);
    hello(); /* HELLO itself cannot restore action qualification. */
    send_request(7u, 42u); assert(reply_result(7u, 42u) == 3u && calls == 0u);
}

static void test_dma_positions(void)
{
    struct { uint8_t before[16], dma[2048], after[16]; } guarded;
    uint8_t bytes[524];
    size_t n;
    unsigned i;
    memset(&guarded, 0xa5, sizeof(guarded));
    memset(guarded.dma, 0, sizeof(guarded.dma));
    init(); hello(); n = encode(2u, 50u, bytes);
    memcpy(guarded.dma + 1000u, bytes, n);
    mcu_transport_dma_event(&transport, guarded.dma, 1024u, MCU_DMA_EVENT_HT, 1u);
    assert(transport.tx_count == 0u); /* half a frame */
    mcu_transport_dma_event(&transport, guarded.dma, 1024u, MCU_DMA_EVENT_IDLE, 2u);
    mcu_transport_dma_event(&transport, guarded.dma, 1100u, MCU_DMA_EVENT_IDLE, 3u);
    assert(reply_result(2u, 50u) == 3u);
    mcu_transport_dma_event(&transport, guarded.dma, 1024u, MCU_DMA_EVENT_HT, 4u);
    assert(transport.tx_count == 0u); /* late HT cannot replay bytes */
    memcpy(guarded.dma + 2040u, bytes, 8u);
    mcu_transport_dma_event(&transport, guarded.dma, 2048u, MCU_DMA_EVENT_TC, 5u);
    mcu_transport_dma_event(&transport, guarded.dma, 2048u, MCU_DMA_EVENT_IDLE, 6u);
    memcpy(guarded.dma, bytes + 8u, n - 8u);
    mcu_transport_dma_event(&transport, guarded.dma, (uint16_t)(n - 8u), MCU_DMA_EVENT_IDLE, 7u);
    assert(reply_result(2u, 50u) == 3u);
    assert(transport.stats.dma_duplicates == 3u);
    mcu_transport_dma_event(&transport, guarded.dma, 2049u, MCU_DMA_EVENT_IDLE, 8u);
    assert(transport.stats.rx_lost == 1u && !transport.session_valid);
    for (i = 0u; i < 16u; ++i) assert(guarded.before[i] == 0xa5u && guarded.after[i] == 0xa5u);
}

static void test_max_payload_and_unknown_type(void)
{
    uint8_t payload[512], bytes[524];
    const mcu_transport_packet_t *p;
    size_t n;
    uint16_t crc;
    init(); hello(); memset(payload, 0xff, sizeof(payload));
    memcpy(payload, session.bytes, 16u);
    assert(locker_wire_encode_frame(2u, 60u, payload, sizeof(payload), bytes, sizeof(bytes), &n) == 0);
    assert(n == sizeof(bytes));
    mcu_transport_feed(&transport, bytes, n, 1u);
    assert(reply_result(2u, 60u) == 10u && transport.stream.length == 0u);
    n = encode(2u, 61u, bytes); bytes[3] = 12u;
    crc = locker_wire_crc16(bytes + 2u, n - 4u);
    bytes[n-2u] = (uint8_t)crc; bytes[n-1u] = (uint8_t)(crc >> 8);
    mcu_transport_feed(&transport, bytes, n, 1u);
    p = mcu_transport_tx_peek(&transport);
    assert(p != NULL && p->bytes[3] == 0x8cu && p->length == 30u);
    assert(p->bytes[26] == LOCKER_WIRE_RESULT_UNSUPPORTED && p->bytes[27] == 0u);
    assert(!memcmp(&p->bytes[10], session.bytes, 16u));
    mcu_transport_tx_pop(&transport);
    n = encode(2u, 62u, bytes);
    memset(bytes + 6u, 0, 4u); /* zero request number must never enter a backend */
    crc = locker_wire_crc16(bytes + 2u, n - 4u);
    bytes[n-2u] = (uint8_t)crc; bytes[n-1u] = (uint8_t)(crc >> 8);
    mcu_transport_feed(&transport, bytes, n, 1u);
    assert(transport.tx_count == 0u);
}

static void test_committed_event_encoding(void)
{
    locker_device_event_t event;
    locker_wire_frame_t frame;
    locker_wire_event96_t record;
    locker_uuid_t actual_session, actual_epoch;
    const mcu_transport_packet_t *p;
    unsigned i;
    init(); memset(&event, 0, sizeof(event));
    assert(mcu_transport_queue_event(&transport, &event) == LOCKER_STATUS_NOT_READY);
    hello();
    assert(mcu_transport_queue_event(&transport, &event) == LOCKER_STATUS_NOT_READY);
    event.key.journal_epoch = epoch; event.key.boot_id = 5u; event.key.event_seq = 7u;
    event.event_type = LOCKER_WIRE_DEVICE_EVENT_TYPE_CARD_OBSERVED;
    event.door = LOCKER_DOOR_STATE_CLOSED; event.item = LOCKER_ITEM_STATE_PRESENT;
    event.supply_mv = 0xffffffffu; event.card_uid_len = 4u;
    event.card_uid[0] = 0x63u; event.card_uid[1] = 0xa9u;
    event.card_uid[2] = 0xfbu; event.card_uid[3] = 0x06u;
    assert(mcu_transport_queue_event(&transport, &event) == 0);
    p = mcu_transport_tx_peek(&transport);
    assert(p != NULL && p->length == 140u && p->bytes[3] == 0x40u);
    for (i = 6u; i < 10u; ++i) assert(p->bytes[i] == 0u);
    frame.type = 0x40u; frame.version = 1u; frame.request_no = 0u; frame.payload_len = 128u;
    memcpy(frame.payload, p->bytes + 10u, 128u);
    assert(locker_wire_decode_device_event(&frame, &actual_session, &actual_epoch, &record) == 0u);
    assert(record.boot_id == 5u && record.event_seq == 7u && record.card_uid_len == 4u);
    assert(!memcmp(actual_session.bytes, session.bytes, 16u));
    assert(!memcmp(actual_epoch.bytes, epoch.bytes, 16u));
    for (i = 4u; i < 10u; ++i) assert(record.card_uid[i] == 0u);
    mcu_transport_tx_pop(&transport);
}

static unsigned nibble(char c)
{
    return (unsigned)(c >= 'a' ? c - 'a' + 10 : c - '0');
}

static void test_contract_golden(void)
{
    /* contracts/examples/uart-golden.json, independent fixed bytes. */
    static const char hello_hex[] =
        "4c4b0101310001000000000000000000000000000000000000000000000000004000800000000000000a"
        "0000000000004000800000000000000b01d8bd";
    uint8_t frame[61];
    const mcu_transport_packet_t *p;
    unsigned i;
    assert(strlen(hello_hex) == sizeof(frame) * 2u);
    for (i = 0u; i < sizeof(frame); ++i)
        frame[i] = (uint8_t)((nibble(hello_hex[i*2u]) << 4) | nibble(hello_hex[i*2u+1u]));
    init();
    for (i = 0u; i < sizeof(frame); ++i) mcu_transport_feed(&transport, &frame[i], 1u, i);
    p = mcu_transport_tx_peek(&transport);
    assert(p != NULL && p->length == 83u);
    assert(!memcmp(p->bytes + 10u, frame + 26u, 16u));
    assert(p->bytes[26] == 0u && p->bytes[27] == 0u);
    assert(p->bytes[71] == LOCKER_WIRE_WIRE_DEVICE_MODE_DEGRADED);
    assert(p->bytes[72] == LOCKER_WIRE_WIRE_JOURNAL_STATE_UNINITIALIZED);
    assert(reply_result(1u, 1u) == 0u);
}

static void test_backend_hello(void)
{
    locker_device_reply_t reply={0};
    init(); calls=0u; transport.dispatch=dispatch; transport.backend_hello=true;
    send_request(1u,901u);
    assert(calls==1u && received[0].message_type==1u && received[0].request_no==901u);
    assert(!memcmp(received[0].session_id.bytes,session.bytes,16u) && transport.tx_count==0u);
    reply.message_type=1u; reply.request_no=901u; reply.body.hello.session_id=session;
    reply.body.hello.journal_epoch=epoch; reply.body.hello.boot_id=42u;
    reply.body.hello.journal_state=LOCKER_WIRE_WIRE_JOURNAL_STATE_HEALTHY;
    assert(mcu_transport_queue_reply(&transport,&reply)==LOCKER_STATUS_OK);
    assert(reply_result(1u,901u)==0u);
}

int main(void)
{
    assert(locker_wire_crc16((const uint8_t *)"123456789", 9u) == 0x29b1u);
    test_all_requests(); test_splits_and_corruption(); test_sessions_and_async();
    test_backpressure(); test_dma_positions(); test_max_payload_and_unknown_type();
    test_committed_event_encoding();
    test_contract_golden();
    test_backend_hello();
    puts("S2-03 stream/session/11 dispatch/DMA/backpressure passed");
    return 0;
}
