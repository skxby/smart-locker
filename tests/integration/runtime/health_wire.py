"""Transparent UART observer; field sizes/order come from the wire contract."""
import binascii
import json
from pathlib import Path
import selectors
import socket
import threading
import time


LAYOUT = json.loads((Path(__file__).resolve().parents[3] / "contracts/uart-layouts.json").read_text())
MESSAGES = {message["type"]: message for message in LAYOUT["messages"]}
SIZES = {"uuid": 16, "u8": 1, "u16": 2, "u32": 4, "u64": 8}
HEADER = {}
HEADER_SIZE = 0
for field in LAYOUT["header"]:
    HEADER[field["name"]] = (HEADER_SIZE, field["bytes"])
    HEADER_SIZE += field["bytes"]
MAGIC = bytes.fromhex(LAYOUT["header"][0]["hex"])
HELLO = next(message["type"] for message in LAYOUT["messages"] if message["name"] == "HELLO")
HEALTH = next(message["type"] for message in LAYOUT["messages"] if message["name"] == "HOST_HEALTH")


def header_number(frame, key):
    offset, size = HEADER[key]
    return int.from_bytes(frame[offset:offset + size], "little")


def decode_request(message_type, payload):
    fields = LAYOUT["request_prefix"] + MESSAGES[message_type]["request"]
    decoded, offset = {}, 0
    for field in fields:
        size = SIZES[field["type"]]
        value = payload[offset:offset + size]
        if len(value) != size:
            raise AssertionError("truncated request field " + field["name"])
        decoded[field["name"]] = value if field["type"] == "uuid" else int.from_bytes(value, "little")
        offset += size
    if offset != len(payload):
        raise AssertionError("request payload has extra bytes")
    return decoded


class WireProxy:
    def __init__(self, frontend, backend):
        self.frontend, self.backend = frontend, backend
        self.stop_event = threading.Event()
        self.lock = threading.Lock()
        self.records = []
        self.error = None
        self.listener = socket.socket(socket.AF_UNIX)
        self.listener.bind(str(frontend))
        frontend.chmod(0o600)
        self.listener.listen()
        self.listener.settimeout(0.1)
        self.thread = threading.Thread(target=self._run, daemon=True)
        self.thread.start()

    def snapshot(self):
        if self.error:
            raise AssertionError("wire proxy failed") from self.error
        with self.lock:
            return list(self.records)

    def _capture(self, pending, connection):
        while len(pending) >= HEADER_SIZE:
            if pending[:len(MAGIC)] != MAGIC:
                raise AssertionError("invalid frame magic from production service")
            length = header_number(pending, "payload_len")
            if length > LAYOUT["limits"]["max_payload"]:
                raise AssertionError("oversized production frame")
            total = HEADER_SIZE + length + 2
            if len(pending) < total:
                return
            frame = bytes(pending[:total])
            del pending[:total]
            crc = binascii.crc_hqx(frame[len(MAGIC):-2], int(LAYOUT["crc"]["initial"], 16))
            if crc != int.from_bytes(frame[-2:], LAYOUT["crc"]["wire_endian"]):
                raise AssertionError("invalid production frame CRC")
            if header_number(frame, "version") != LAYOUT["version"]:
                raise AssertionError("invalid production frame version")
            message_type = header_number(frame, "type")
            if message_type in (HELLO, HEALTH):
                record = (time.monotonic(), connection, message_type,
                          decode_request(message_type, frame[HEADER_SIZE:-2]))
                with self.lock:
                    self.records.append(record)

    def _run(self):
        connection = 0
        try:
            while not self.stop_event.is_set():
                try:
                    client, _ = self.listener.accept()
                except socket.timeout:
                    continue
                connection += 1
                with client, socket.socket(socket.AF_UNIX) as backend, selectors.DefaultSelector() as selector:
                    backend.settimeout(2)
                    backend.connect(str(self.backend))
                    client.settimeout(2)
                    selector.register(client, selectors.EVENT_READ, backend)
                    selector.register(backend, selectors.EVENT_READ, client)
                    pending = bytearray()
                    connected = True
                    while connected and not self.stop_event.is_set():
                        for key, _ in selector.select(timeout=0.1):
                            try:
                                chunk = key.fileobj.recv(4096)
                            except ConnectionResetError:
                                connected = False
                                break
                            if not chunk:
                                connected = False
                                break
                            if key.fileobj is client:
                                pending.extend(chunk)
                                self._capture(pending, connection)
                            try:
                                key.data.sendall(chunk)
                            except (BrokenPipeError, ConnectionResetError):
                                connected = False
                                break
        except BaseException as error:
            self.error = error

    def close(self):
        self.stop_event.set()
        self.thread.join(timeout=5)
        self.listener.close()
        self.frontend.unlink(missing_ok=True)
        if self.thread.is_alive():
            raise AssertionError("wire proxy failed to stop")
        self.snapshot()
