#!/usr/bin/env python3
"""S2-03 real UART acceptance. Safe HELLO/GET_SNAPSHOT requests only.

Run on the connected RK: python3 uart_board_check.py --device /dev/ttyS4
JSON (including every raw TX/RX chunk) is written to stdout. Exit 0 requires
all mandatory checks; a burst can legitimately lose replies to bounded TX
backpressure. This script never formats storage or requests an actuator action.
"""
import argparse
import copy
import json
import os
import select
import struct
import sys
import time
try:
    import termios
except ImportError:
    termios = None  # Pure frame self-test also runs on the Windows source host.


def crc16(data):
    crc = 0xffff
    for value in data:
        crc ^= value << 8
        for _ in range(8):
            crc = ((crc << 1) ^ (0x1021 if crc & 0x8000 else 0)) & 0xffff
    return crc


def frame(kind, number, payload):
    checked = struct.pack('<BBHI', 1, kind, len(payload), number) + payload
    return b'LK' + checked + struct.pack('<H', crc16(checked))


class UARTCheck:
    def __init__(self, device, result):
        self.device = device
        self.result = result
        self.fd = None
        self.original = None
        self.pending = bytearray()
        self.frames = []
        self.session = os.urandom(16)
        self.host = os.urandom(16)
        self.start = time.monotonic()
        self.next_number = 1

    def open(self):
        if termios is None:
            raise RuntimeError('Real UART acceptance requires Linux termios')
        self.fd = os.open(self.device, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        self.original = termios.tcgetattr(self.fd)
        settings = copy.deepcopy(self.original)
        settings[0] = 0
        settings[1] = 0
        settings[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
        settings[3] = 0
        settings[4] = termios.B115200
        settings[5] = termios.B115200
        settings[6][termios.VMIN] = 0
        settings[6][termios.VTIME] = 0
        termios.tcsetattr(self.fd, termios.TCSANOW, settings)
        termios.tcflush(self.fd, termios.TCIOFLUSH)
        self.result['session_hex'] = self.session.hex()
        self.result['host_instance_hex'] = self.host.hex()

    def close(self):
        if self.fd is not None:
            try:
                if self.original is not None:
                    termios.tcsetattr(self.fd, termios.TCSANOW, self.original)
                    self.result['termios_restored'] = True
            finally:
                os.close(self.fd)
                self.fd = None

    def record(self, direction, data):
        self.result['raw_io'].append({
            'direction': direction,
            'elapsed_ms': round((time.monotonic() - self.start) * 1000, 3),
            'length': len(data), 'hex': bytes(data).hex()})

    def send(self, data):
        offset = 0
        deadline = time.monotonic() + 5
        while offset < len(data):
            if time.monotonic() >= deadline:
                raise RuntimeError('UART write deadline exceeded')
            if not select.select([], [self.fd], [], 0.1)[1]:
                self.receive(0)
                continue
            try:
                count = os.write(self.fd, data[offset:])
            except BlockingIOError:
                continue
            if not count:
                raise RuntimeError('UART write returned zero')
            self.record('TX', data[offset:offset + count])
            offset += count

    def parse(self):
        while len(self.pending) >= 2:
            if self.pending[:2] != b'LK':
                del self.pending[0]
                self.result['rx_discarded_bytes'] += 1
                continue
            if len(self.pending) < 10:
                return
            version, kind, length, number = struct.unpack('<BBHI', self.pending[2:10])
            if length > 512:
                raise RuntimeError('MCU emitted payload length above 512')
            total = length + 12
            if len(self.pending) < total:
                return
            raw = bytes(self.pending[:total])
            del self.pending[:total]
            actual = struct.unpack('<H', raw[-2:])[0]
            if actual != crc16(raw[2:-2]):
                raise RuntimeError('MCU response CRC mismatch: ' + raw.hex())
            if version != 1:
                raise RuntimeError('MCU response has wrong protocol version')
            decoded = {'type': kind, 'request_no': number,
                       'payload_hex': raw[10:-2].hex(), 'frame_hex': raw.hex()}
            self.frames.append(decoded)
            self.result['frames'].append(decoded)

    def receive(self, seconds):
        deadline = time.monotonic() + seconds
        while True:
            remaining = max(0, deadline - time.monotonic())
            readable = select.select([self.fd], [], [], remaining)[0]
            if readable:
                try:
                    data = os.read(self.fd, 8192)
                except BlockingIOError:
                    data = b''
                if data:
                    self.record('RX', data)
                    self.pending.extend(data)
                    self.parse()
            if time.monotonic() >= deadline:
                return

    def number(self):
        number = self.next_number
        self.next_number += 1
        return number

    def snapshot(self, session=None):
        number = self.number()
        return number, frame(2, number, self.session if session is None else session)

    def expect(self, numbers, result=3, session=None, timeout=2):
        wanted = set(numbers)
        responses = {}
        deadline = time.monotonic() + timeout
        while wanted - set(responses):
            for decoded in list(self.frames):
                number = decoded['request_no']
                if number in wanted:
                    if number in responses:
                        raise RuntimeError('Duplicate response for request ' + str(number))
                    payload = bytes.fromhex(decoded['payload_hex'])
                    if decoded['type'] != 0x82 or len(payload) != 18:
                        raise RuntimeError('Expected fixed-length snapshot error response')
                    if payload[:16] != (self.session if session is None else session):
                        raise RuntimeError('Response session mismatch')
                    if struct.unpack('<H', payload[16:18])[0] != result:
                        raise RuntimeError('Wrong snapshot result: ' + payload.hex())
                    responses[number] = decoded
                    self.frames.remove(decoded)
            if wanted <= set(responses):
                return responses
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise RuntimeError('Missing responses: ' + str(sorted(wanted - set(responses))))
            self.receive(min(remaining, 0.05))

    def check(self, name, details):
        self.result['checks'].append({'name': name, 'status': 'PASS', 'details': details})

    def run(self, burst_count):
        number = self.number()
        self.send(frame(1, number, bytes(16) + self.session + self.host + b'\x01'))
        self.receive(0.4)
        matches = [f for f in self.frames if f['request_no'] == number]
        if len(matches) != 1 or matches[0]['type'] != 0x81:
            raise RuntimeError('Missing or duplicate HELLO response')
        hello = matches[0]
        self.frames.remove(hello)
        payload = bytes.fromhex(hello['payload_hex'])
        if len(payload) != 71 or payload[:16] != self.session or payload[16:18] != bytes(2):
            raise RuntimeError('Invalid HELLO success layout/session/result')
        uid = payload[18:30]
        epoch = payload[30:46]
        boot = struct.unpack('<Q', payload[46:54])[0]
        capabilities = struct.unpack('<I', payload[57:61])[0]
        mode, journal = payload[61:63]
        watermark = struct.unpack('<Q', payload[63:71])[0]
        if uid == bytes(12) or epoch != bytes(16) or boot != 0 or mode != 2 or journal != 0 or watermark != 0:
            raise RuntimeError('HELLO does not report restricted uninitialized diagnostics')
        if capabilities & ~5:
            raise RuntimeError('S2-03 advertises unimplemented actuator/Journal/ADC capability')
        expected_payload = (self.session + bytes(2) + uid + bytes(16) + bytes(8)
                            + bytes([0, 2, 1]) + bytes(4) + bytes([2, 0]) + bytes(8))
        expected_frame = frame(0x81, number, expected_payload)
        if hello['frame_hex'] != expected_frame.hex():
            raise RuntimeError('HELLO differs from exact S203 diagnostic golden response')
        self.check('HELLO_exact_golden_response', {'expected_hex': expected_frame.hex(),
                   'uid_source': 'actual response; separately compare to SWD chip UID'})
        self.check('HELLO_diagnostic', {'uid_hex': uid.hex(), 'epoch_hex': epoch.hex(),
                   'boot': boot, 'firmware': list(payload[54:57]), 'capabilities': capabilities,
                   'device_mode': mode, 'journal_state': journal, 'watermark': watermark})

        number, data = self.snapshot()
        self.send(data)
        self.expect([number])
        self.check('snapshot_NOT_READY', {'request_no': number})

        number, data = self.snapshot()
        self.send(data[:13])
        self.receive(0.1)
        if any(f['request_no'] == number for f in self.frames):
            raise RuntimeError('Incomplete frame produced a response')
        self.send(data[13:])
        self.expect([number])
        self.check('split_frame', {'request_no': number, 'split_offset': 13, 'delay_ms': 100})

        first, first_data = self.snapshot()
        second, second_data = self.snapshot()
        self.send(first_data + second_data)
        self.expect([first, second])
        self.check('two_frames_one_write', {'request_numbers': [first, second]})

        old_session = bytes([self.session[0] ^ 1]) + self.session[1:]
        number, data = self.snapshot(old_session)
        self.send(data)
        self.expect([number], result=2, session=old_session)
        self.check('old_session_BAD_SESSION', {'request_no': number, 'old_session_hex': old_session.hex()})

        bad_number, bad = self.snapshot()
        good_number, good = self.snapshot()
        bad = bad[:-1] + bytes([bad[-1] ^ 1])
        self.send(bad + good)
        self.expect([good_number])
        self.receive(0.1)
        if any(f['request_no'] == bad_number for f in self.frames):
            raise RuntimeError('CRC-corrupt request received a response')
        self.check('bad_crc_then_recovery', {'bad_request_no': bad_number, 'good_request_no': good_number})

        bad_number = self.number()
        oversize = b'LK' + struct.pack('<BBHI', 1, 2, 513, bad_number)
        good_number, good = self.snapshot()
        self.send(oversize + good)
        self.expect([good_number])
        self.receive(0.1)
        if any(f['request_no'] == bad_number for f in self.frames):
            raise RuntimeError('Oversize header received a response')
        self.check('oversize_then_recovery', {'bad_request_no': bad_number, 'payload_len': 513,
                   'good_request_no': good_number})

        number, data = self.snapshot()
        self.send(bytes(7168) + data)
        self.expect([number], timeout=3)
        self.check('multiple_DMA_wraps', {'garbage_bytes': 7168, 'ring_bytes': 2048,
                   'request_no': number})

        packets = [self.snapshot() for _ in range(burst_count)]
        self.send(b''.join(data for _, data in packets))
        self.receive(2)
        wanted = {number for number, _ in packets}
        seen = set()
        for decoded in list(self.frames):
            if decoded['request_no'] not in wanted:
                continue
            number = decoded['request_no']
            if number in seen:
                raise RuntimeError('Burst duplicated a response')
            seen.add(number)
            self.expect([number], timeout=0.01)
        if not seen:
            raise RuntimeError('Burst produced no valid diagnostic response')
        self.check('bounded_TX_burst', {'sent_count': burst_count, 'response_count': len(seen),
                   'missing_request_numbers': sorted(wanted - seen),
                   'observed_reply_loss': bool(wanted - seen),
                   'backpressure_claim': 'reply loss alone does not prove MCU TX queue saturation'})
        # Recovery is mandatory even when some overload requests were dropped.
        number, data = self.snapshot()
        self.send(data)
        self.expect([number])
        self.check('post_burst_recovery', {'request_no': number})
        self.receive(0.05)
        if self.frames:
            raise RuntimeError('Unexpected/unmatched MCU frames remain: ' + str(self.frames))
        if self.pending or self.result['rx_discarded_bytes']:
            raise RuntimeError('MCU emitted trailing partial or unframed response bytes')


def self_test():
    assert crc16(b'123456789') == 0x29b1
    session = bytes.fromhex('0000000000004000800000000000000a')
    host = bytes.fromhex('0000000000004000800000000000000b')
    assert frame(1, 1, bytes(16) + session + host + b'\x01').hex() == (
        '4c4b0101310001000000000000000000000000000000000000000000000000004000800000000000000a'
        '0000000000004000800000000000000b01d8bd')
    assert frame(2, 2, session).hex() == '4c4b01021000020000000000000000004000800000000000000a6eea'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--device', default='/dev/ttyS4')
    parser.add_argument('--burst-count', type=int, default=128)
    parser.add_argument('--self-test', action='store_true')
    args = parser.parse_args()
    result = {'card': 'S2-03', 'device': args.device, 'baud': 115200, 'format': '8N1',
              'checks': [], 'frames': [], 'raw_io': [], 'rx_discarded_bytes': 0,
              'termios_restored': False, 'status': 'FAIL'}
    checker = None
    try:
        self_test()
        if args.self_test:
            result['status'] = 'PASS'
            result['scope'] = 'pure frame self-test only; UART not opened'
        else:
            if not 8 <= args.burst_count <= 1024:
                raise ValueError('--burst-count must be in [8,1024]')
            checker = UARTCheck(args.device, result)
            checker.open()
            checker.run(args.burst_count)
            result['status'] = 'PASS'
    except Exception as exc:
        result['error'] = type(exc).__name__ + ': ' + str(exc)
    finally:
        if checker is not None:
            try:
                checker.close()
            except Exception as exc:
                result['status'] = 'FAIL'
                result['restore_error'] = type(exc).__name__ + ': ' + str(exc)
        print(json.dumps(result, ensure_ascii=False, indent=2))
    return 0 if result['status'] == 'PASS' else 1


if __name__ == '__main__':
    sys.exit(main())
