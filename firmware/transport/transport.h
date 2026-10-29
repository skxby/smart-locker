#ifndef MCU_TRANSPORT_H
#define MCU_TRANSPORT_H

#include "locker/device/codec.h"

#define MCU_TRANSPORT_TX_SLOTS 4u
#define MCU_DMA_EVENT_HT 1u
#define MCU_DMA_EVENT_TC 2u
#define MCU_DMA_EVENT_IDLE 3u

/* Called only by Communication. ACCEPTED/QUEUED means that the receiver has
 * copied both arguments into its bounded queue; it replies asynchronously.
 * No callback is installed until the real Control/Storage owners exist. */
typedef locker_status_t (*mcu_transport_dispatch_t)(void *user,
    const mcu_request_context_t *context, const locker_wire_request_union_t *request);

typedef struct {
    uint8_t bytes[LOCKER_WIRE_MAX_FRAME];
    size_t length;
} mcu_transport_packet_t;

typedef struct {
    uint64_t requests[11], bad_session, bad_payload, bad_version, unsupported;
    uint64_t tx_full, stale_replies, rx_lost, dma_duplicates, dma_invalid;
} mcu_transport_stats_t;

typedef struct {
    locker_wire_stream_t stream;
    locker_uuid_t session_id, host_instance_id;
    uint8_t device_uid[12];
    uint32_t capabilities;
    mcu_transport_dispatch_t dispatch;
    void *user;
    uint64_t received_mono_ms;
    mcu_transport_packet_t tx[MCU_TRANSPORT_TX_SLOTS];
    unsigned tx_head, tx_count;
    bool session_valid, fault_latched;
    uint16_t dma_position;
    mcu_transport_stats_t stats;
} mcu_transport_t;

void mcu_transport_init(mcu_transport_t *transport, const uint8_t device_uid[12],
    uint32_t capabilities, void *user, mcu_transport_dispatch_t dispatch);
void mcu_transport_feed(mcu_transport_t *transport, const uint8_t *bytes,
    size_t length, uint64_t received_mono_ms);
/* Ordered HAL HT/TC/IDLE positions, copied in Communication while DMA remains
 * within the unconsumed half. ISR must detect lost notices/overrun separately.
 * Size is [1,2048], not a byte count. Duplicate same-position notices consume 0.
 * TC must be retained by the ISR even when a subsequent IDLE has the same Size. */
void mcu_transport_dma_event(mcu_transport_t *transport,
    const uint8_t dma[LOCKER_UART_DMA_BYTES], uint16_t size, uint8_t kind,
    uint64_t received_mono_ms);
void mcu_transport_rx_lost(mcu_transport_t *transport);
const mcu_transport_packet_t *mcu_transport_tx_peek(const mcu_transport_t *transport);
void mcu_transport_tx_pop(mcu_transport_t *transport);
/* Communication task only. Other tasks copy native replies/events into its
 * bounded RTOS inbox; these functions never wait or call hardware. */
locker_status_t mcu_transport_queue_reply(mcu_transport_t *transport,
    const locker_device_reply_t *reply);
locker_status_t mcu_transport_queue_event(mcu_transport_t *transport,
    const locker_device_event_t *event);

#endif
