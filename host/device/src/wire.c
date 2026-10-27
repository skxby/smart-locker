/*
 * S1-03 UART v1 frame encoder, CRC and incremental parser.
 * See include/locker/device/wire.h for the layout rules.
 */

#include "locker/device/wire.h"

#include <string.h>

#define LOCKER_WIRE_CRC_POLY 0x1021u
#define LOCKER_WIRE_CRC_INIT 0xffffu

uint16_t locker_wire_crc16(const uint8_t *data, size_t length)
{
    uint16_t crc = LOCKER_WIRE_CRC_INIT;
    size_t index;

    if (data == NULL && length != 0u) {
        return crc;
    }
    for (index = 0u; index < length; ++index) {
        unsigned bit;
        crc ^= (uint16_t)((uint16_t)data[index] << 8);
        for (bit = 0u; bit < 8u; ++bit) {
            if ((crc & 0x8000u) != 0u) {
                crc = (uint16_t)((uint16_t)(crc << 1) ^ LOCKER_WIRE_CRC_POLY);
            } else {
                crc = (uint16_t)(crc << 1);
            }
        }
    }
    return crc;
}

bool locker_wire_request_type_valid(uint8_t type)
{
    return type >= LOCKER_WIRE_REQUEST_TYPE_FIRST && type <= LOCKER_WIRE_REQUEST_TYPE_LAST;
}

size_t locker_wire_frame_size(size_t payload_length)
{
    return LOCKER_WIRE_HEADER_SIZE + payload_length + LOCKER_WIRE_CRC_SIZE;
}

static void wire_put_u16(uint8_t *out, uint16_t value)
{
    out[0] = (uint8_t)(value & 0xffu);
    out[1] = (uint8_t)((value >> 8) & 0xffu);
}

static void wire_put_u32(uint8_t *out, uint32_t value)
{
    out[0] = (uint8_t)(value & 0xffu);
    out[1] = (uint8_t)((value >> 8) & 0xffu);
    out[2] = (uint8_t)((value >> 16) & 0xffu);
    out[3] = (uint8_t)((value >> 24) & 0xffu);
}

static uint16_t wire_get_u16(const uint8_t *in)
{
    return (uint16_t)((uint16_t)in[0] | (uint16_t)((uint16_t)in[1] << 8));
}

static uint32_t wire_get_u32(const uint8_t *in)
{
    return (uint32_t)in[0] | ((uint32_t)in[1] << 8) | ((uint32_t)in[2] << 16) |
           ((uint32_t)in[3] << 24);
}

locker_status_t locker_wire_encode_frame(uint8_t type, uint32_t request_no,
                                         const uint8_t *payload, size_t payload_length,
                                         uint8_t *out, size_t out_size, size_t *out_length)
{
    size_t total;
    uint16_t crc;

    if (out == NULL || out_length == NULL) {
        return LOCKER_STATUS_INVALID;
    }
    if (payload == NULL && payload_length != 0u) {
        return LOCKER_STATUS_INVALID;
    }
    if (payload_length > LOCKER_WIRE_MAX_PAYLOAD) {
        return LOCKER_STATUS_INVALID;
    }
    if (type == 0u || (type & LOCKER_WIRE_RESPONSE_FLAG) == 0u) {
        if (!locker_wire_request_type_valid(type) && type != LOCKER_WIRE_DEVICE_EVENT) {
            return LOCKER_STATUS_INVALID;
        }
    } else if (!locker_wire_request_type_valid((uint8_t)(type & (uint8_t)~LOCKER_WIRE_RESPONSE_FLAG))) {
        /* A response is always request_type|RESPONSE_FLAG; DEVICE_EVENT has no
         * response form, so DEVICE_EVENT|RESPONSE_FLAG must not encode. */
        return LOCKER_STATUS_INVALID;
    }
    if (type == LOCKER_WIRE_DEVICE_EVENT && request_no != 0u) {
        return LOCKER_STATUS_INVALID;
    }
    if (type != LOCKER_WIRE_DEVICE_EVENT && request_no == 0u) {
        return LOCKER_STATUS_INVALID;
    }

    total = locker_wire_frame_size(payload_length);
    if (out_size < total) {
        return LOCKER_STATUS_NO_SPACE;
    }

    out[0] = LOCKER_WIRE_MAGIC_FIRST;
    out[1] = LOCKER_WIRE_MAGIC_SECOND;
    out[2] = LOCKER_WIRE_VERSION;
    out[3] = type;
    wire_put_u16(&out[4], (uint16_t)payload_length);
    wire_put_u32(&out[6], request_no);
    if (payload_length != 0u) {
        memcpy(&out[LOCKER_WIRE_HEADER_SIZE], payload, payload_length);
    }
    crc = locker_wire_crc16(&out[2], LOCKER_WIRE_HEADER_SIZE - 2u + payload_length);
    wire_put_u16(&out[LOCKER_WIRE_HEADER_SIZE + payload_length], crc);

    *out_length = total;
    return LOCKER_STATUS_OK;
}

void locker_wire_stream_init(locker_wire_stream_t *stream, void *handler_user,
                             locker_wire_frame_handler_t handler)
{
    if (stream == NULL) {
        return;
    }
    memset(stream, 0, sizeof(*stream));
    stream->handler_user = handler_user;
    stream->handler = handler;
}

void locker_wire_stream_reset(locker_wire_stream_t *stream)
{
    if (stream == NULL) {
        return;
    }
    stream->length = 0u;
}

size_t locker_wire_stream_pending(const locker_wire_stream_t *stream)
{
    return stream == NULL ? 0u : stream->length;
}

/* Drop count bytes from the front of the accumulation buffer. */
static void wire_stream_discard(locker_wire_stream_t *stream, size_t count)
{
    if (count >= stream->length) {
        stream->length = 0u;
        return;
    }
    memmove(stream->buffer, &stream->buffer[count], stream->length - count);
    stream->length -= count;
}

locker_status_t locker_wire_stream_feed(locker_wire_stream_t *stream, const uint8_t *data,
                                        size_t length)
{
    if (stream == NULL || (data == NULL && length != 0u)) {
        return LOCKER_STATUS_INVALID;
    }
    if (stream->handler == NULL) {
        return LOCKER_STATUS_INVALID;
    }

    while (length > 0u) {
        size_t room = sizeof(stream->buffer) - stream->length;
        size_t take = length < room ? length : room;

        if (take == 0u) {
            /* Buffer holds a full maximum frame that still did not parse: that
             * cannot happen because a maximum frame is consumed below. Treat it
             * as a framing fault and resync rather than spin. */
            wire_stream_discard(stream, 1u);
            ++stream->stats.resyncs;
            continue;
        }
        memcpy(&stream->buffer[stream->length], data, take);
        stream->length += take;
        data += take;
        length -= take;

        for (;;) {
            size_t payload_length;
            size_t frame_size;
            uint16_t expected_crc;
            uint16_t actual_crc;
            locker_wire_frame_t frame;
            locker_status_t status;

            if (stream->length < 2u) {
                break;
            }
            if (stream->buffer[0] != LOCKER_WIRE_MAGIC_FIRST ||
                stream->buffer[1] != LOCKER_WIRE_MAGIC_SECOND) {
                wire_stream_discard(stream, 1u);
                ++stream->stats.resyncs;
                continue;
            }
            if (stream->length < LOCKER_WIRE_HEADER_SIZE) {
                break;
            }
            payload_length = wire_get_u16(&stream->buffer[4]);
            if (payload_length > LOCKER_WIRE_MAX_PAYLOAD) {
                wire_stream_discard(stream, 1u);
                ++stream->stats.length_errors;
                ++stream->stats.resyncs;
                continue;
            }
            frame_size = locker_wire_frame_size(payload_length);
            if (stream->length < frame_size) {
                break;
            }
            expected_crc = wire_get_u16(&stream->buffer[LOCKER_WIRE_HEADER_SIZE + payload_length]);
            actual_crc = locker_wire_crc16(&stream->buffer[2],
                                           LOCKER_WIRE_HEADER_SIZE - 2u + payload_length);
            if (actual_crc != expected_crc) {
                wire_stream_discard(stream, 1u);
                ++stream->stats.crc_errors;
                ++stream->stats.resyncs;
                continue;
            }

            frame.version = stream->buffer[2];
            frame.type = stream->buffer[3];
            frame.payload_len = (uint16_t)payload_length;
            frame.request_no = wire_get_u32(&stream->buffer[6]);
            if (payload_length != 0u) {
                memcpy(frame.payload, &stream->buffer[LOCKER_WIRE_HEADER_SIZE], payload_length);
            }
            wire_stream_discard(stream, frame_size);
            ++stream->stats.frames_accepted;
            status = stream->handler(stream->handler_user, &frame);
            if (status != LOCKER_STATUS_OK) {
                ++stream->stats.handler_errors;
                return status;
            }
        }
    }
    return LOCKER_STATUS_OK;
}
