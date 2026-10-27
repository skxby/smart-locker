#ifndef LOCKER_DEVICE_WIRE_H
#define LOCKER_DEVICE_WIRE_H

/*
 * S1-03 UART v1 framing (docs/architecture/uart-v1.md:5-23,
 * contracts/uart-layouts.json). This header owns the byte-level frame only:
 * magic, version, type, payload_len, request_no and the trailing CRC.
 *
 * Frame layout (uart-v1.md:9-17):
 *   offset 0  magic        2  fixed 0x4c 0x4b ("LK")
 *   offset 2  version      1  fixed 1
 *   offset 3  type         1  request 1..11, response type|0x80, DEVICE_EVENT 0x40
 *   offset 4  payload_len  2  little-endian, 0..512
 *   offset 6  request_no   4  little-endian, non-zero for requests, echoed by replies, 0 for events
 *   offset 10 payload      payload_len
 *   offset 10+len crc16    2  little-endian CRC-16/CCITT-FALSE over version..payload
 *
 * A C struct is never memcpy'd onto the wire; every field is written explicitly
 * by the codec. The parser never allocates and never trusts a peer-supplied
 * length: payload_len above LOCKER_WIRE_MAX_PAYLOAD triggers a resync instead.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "locker_contracts.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LOCKER_WIRE_MAGIC_FIRST 0x4cu
#define LOCKER_WIRE_MAGIC_SECOND 0x4bu
#define LOCKER_WIRE_VERSION 1u
#define LOCKER_WIRE_HEADER_SIZE 10u
#define LOCKER_WIRE_CRC_SIZE 2u
#define LOCKER_WIRE_MAX_PAYLOAD 512u
#define LOCKER_WIRE_MAX_FRAME \
    (LOCKER_WIRE_HEADER_SIZE + LOCKER_WIRE_MAX_PAYLOAD + LOCKER_WIRE_CRC_SIZE)
#define LOCKER_WIRE_REQUEST_TYPE_FIRST 1u
#define LOCKER_WIRE_REQUEST_TYPE_LAST 11u
#define LOCKER_WIRE_RESPONSE_FLAG 0x80u
#define LOCKER_WIRE_DEVICE_EVENT 0x40u
#define LOCKER_WIRE_EVENT_RECORD_SIZE 96u
#define LOCKER_WIRE_DEVICE_EVENT_PAYLOAD_SIZE \
    (16u + 16u + LOCKER_WIRE_EVENT_RECORD_SIZE)

/* One decoded frame. payload is a bounded copy so the parser can reuse its
 * accumulation buffer immediately after the handler returns. */
typedef struct locker_wire_frame {
    uint8_t version;
    uint8_t type;
    uint16_t payload_len;
    uint32_t request_no;
    uint8_t payload[LOCKER_WIRE_MAX_PAYLOAD];
} locker_wire_frame_t;

/* Handler returns OK to continue parsing; any other status aborts the feed call
 * so a backpressure/fatal condition is visible to the caller. */
typedef locker_status_t (*locker_wire_frame_handler_t)(void *user,
                                                       const locker_wire_frame_t *frame);

typedef struct locker_wire_stream_stats {
    uint64_t frames_accepted;
    uint64_t resyncs;
    uint64_t crc_errors;
    uint64_t length_errors;
    uint64_t handler_errors;
} locker_wire_stream_stats_t;

typedef struct locker_wire_stream {
    uint8_t buffer[LOCKER_WIRE_MAX_FRAME];
    size_t length;
    void *handler_user;
    locker_wire_frame_handler_t handler;
    locker_wire_stream_stats_t stats;
} locker_wire_stream_t;

/* CRC-16/CCITT-FALSE: poly 0x1021, init 0xffff, no reflection, xorout 0.
 * "123456789" must yield 0x29b1 (uart-v1.md:19). */
uint16_t locker_wire_crc16(const uint8_t *data, size_t length);

bool locker_wire_request_type_valid(uint8_t type);
/* Total wire length for a payload of payload_length bytes (header+crc). */
size_t locker_wire_frame_size(size_t payload_length);

/* Writes one complete frame. Returns NO_SPACE when out_size is too small,
 * INVALID for a bad type/length argument. */
locker_status_t locker_wire_encode_frame(uint8_t type, uint32_t request_no,
                                         const uint8_t *payload, size_t payload_length,
                                         uint8_t *out, size_t out_size, size_t *out_length);

/* Incremental parser. feed() may be called with any split of the byte stream,
 * including one byte at a time; it never buffers more than one maximum frame. */
void locker_wire_stream_init(locker_wire_stream_t *stream, void *handler_user,
                             locker_wire_frame_handler_t handler);
void locker_wire_stream_reset(locker_wire_stream_t *stream);
locker_status_t locker_wire_stream_feed(locker_wire_stream_t *stream, const uint8_t *data,
                                        size_t length);
/* Bytes currently buffered as an incomplete frame. */
size_t locker_wire_stream_pending(const locker_wire_stream_t *stream);

#ifdef __cplusplus
}
#endif

#endif /* LOCKER_DEVICE_WIRE_H */
