#!/usr/bin/env python3
"""Drive the real locker-device-sim process: sockets, framing, control surface.

The C tests deliberately do not link the daemon binary (see
tests/contract/device/test_device_port.c:5-8): they own an in-process socket
pair. This case is what proves sim_main.c itself: two 0600 AF_UNIX sockets,
UART-v1 bytes in and out, the private control surface, and the independent
device journal.

It is not a firmware check: no MCU, no UART, no GPIO, no littlefs.
"""

import os
import socket
import sqlite3
import stat
import struct
import subprocess
import sys
import tempfile
import time

NIL_UUID = "00000000-0000-0000-0000-000000000000"

# Verbatim from contracts/examples/uart-golden.json: hello_request and
# snapshot_request. The frames are assembled from the published payload_hex so a
# transcription slip cannot hide in a hand-typed header.
GOLDEN_HELLO_PAYLOAD = bytes.fromhex(
    "00000000000000000000000000000000"
    "0000000000004000800000000000000a"
    "0000000000004000800000000000000b"
    "01")
GOLDEN_SNAPSHOT_PAYLOAD = bytes.fromhex(
    "0000000000004000800000000000000a")
GOLDEN_HELLO = bytes.fromhex("4c4b0101310001000000") + GOLDEN_HELLO_PAYLOAD + bytes.fromhex("d8bd")
GOLDEN_SNAPSHOT = (bytes.fromhex("4c4b0102100002000000") + GOLDEN_SNAPSHOT_PAYLOAD
                   + bytes.fromhex("6eea"))

FAILURES = []
CHECKS = [0]


def check(condition, label):
    CHECKS[0] += 1
    if not condition:
        FAILURES.append(label)
        print("FAIL " + label)


def crc16(data):
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


def build_frame(message_type, request_no, payload):
    header = struct.pack("<HBBHI", 0x4B4C, 1, message_type, len(payload), request_no)
    body = header + payload
    return body + struct.pack("<H", crc16(body[2:]))


def uuid_text_to_bytes(text):
    return bytes.fromhex(text.replace("-", ""))


class UartReader:
    """Buffered UART-v1 frame reader.

    The socket hands over arbitrary byte counts, so a single recv() may hold one
    and a half frames. Buffering here keeps a frame that arrived together with
    its predecessor from being thrown away.
    """

    def __init__(self, sock):
        self.sock = sock
        self.buffer = b""

    def read(self, timeout):
        self.sock.settimeout(timeout)
        deadline = time.time() + timeout
        while time.time() < deadline:
            if len(self.buffer) >= 12:
                payload_len = struct.unpack_from("<H", self.buffer, 4)[0]
                total = 12 + payload_len
                if len(self.buffer) >= total:
                    frame = self.buffer[:total]
                    self.buffer = self.buffer[total:]
                    return frame
            try:
                chunk = self.sock.recv(4096)
            except socket.timeout:
                break
            if not chunk:
                break
            self.buffer += chunk
        return None


def parse_frame(frame):
    magic, version, ftype, payload_len, request_no = struct.unpack_from("<HBBHI", frame, 0)
    payload = frame[10:10 + payload_len]
    wire_crc = struct.unpack_from("<H", frame, 10 + payload_len)[0]
    return {
        "magic": magic, "version": version, "type": ftype,
        "payload_len": payload_len, "request_no": request_no,
        "payload": payload, "crc": wire_crc,
        "crc_ok": crc16(frame[2:10 + payload_len]) == wire_crc,
    }


def control_send(control, line):
    control.sendall((line + "\n").encode())
    control.settimeout(5.0)
    data = b""
    while not data.endswith(b"\n"):
        chunk = control.recv(4096)
        if not chunk:
            break
        data += chunk
    return data.decode("utf-8", "replace").strip()


def control_status(control):
    text = control_send(control, "status")
    values = {}
    for line in text.splitlines():
        if "=" in line:
            key, _, value = line.partition("=")
            values[key] = value
    return values


def test_daemon_execute_loss(binary, parent):
    """Real daemon: strict dispatch, recovery, independent lost EXECUTE ACK."""
    path = tempfile.mkdtemp(prefix="x", dir=parent)
    uart_path, control_path, journal = (os.path.join(path, name)
                                        for name in ("u", "c", "j"))
    # Never reuse a journal from an earlier acceptance invocation.
    if os.path.exists(journal):
        raise RuntimeError("execute acceptance requires a fresh workspace")
    invalid_uid = subprocess.run([binary, "--uart", uart_path, "--control", control_path,
                                  "--journal", journal, "--device-uid", "0g" * 12],
                                 capture_output=True, timeout=5)
    check(invalid_uid.returncode == 2, "CLI rejects partially parsed hex UID")
    check(not os.path.exists(journal), "invalid UID cannot create a journal")
    process = subprocess.Popen([binary, "--uart", uart_path, "--control", control_path,
                                "--journal", journal], stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE)
    control = uart = None
    try:
        ready = process.stdout.readline().decode().strip()
        check(ready.startswith("ready device backend: simulator "), "execute daemon marker")
        control = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        control.connect(control_path)
        uart = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        uart.connect(uart_path)
        reader = UartReader(uart)
        check(control_send(control, "quit\x00") == "error bad_argument",
              "embedded NUL discards entire control command without executing quit")
        session = GOLDEN_HELLO_PAYLOAD[16:32]
        host = GOLDEN_HELLO_PAYLOAD[32:48]
        request_number = [100]
        received_events = []

        def exchange(kind, payload):
            request_number[0] += 1
            number = request_number[0]
            uart.sendall(build_frame(kind, number, payload))
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                raw = reader.read(1)
                if raw is None:
                    continue
                reply = parse_frame(raw)
                check(reply["crc_ok"], "execute scenario response CRC")
                if reply["type"] == 0x40:
                    received_events.append(reply)
                    continue
                check(reply["request_no"] == number, "scenario response correlation")
                check(reply["type"] == (kind | 0x80), "scenario response type")
                return reply["payload"]
            raise AssertionError("daemon response timed out")

        def result(payload):
            return struct.unpack_from("<H", payload, 16)[0]

        # Zero request numbers and codec-only HELLO structural failures cannot
        # establish a session or append a journal row (m39 negative case).
        uart.sendall(build_frame(1, 0, GOLDEN_HELLO_PAYLOAD))
        check(reader.read(0.2) is None, "request zero discarded before response")
        check(control_status(control).get("session") == NIL_UUID,
              "request zero has no session side effect")
        malformed = b"\x01" + GOLDEN_HELLO_PAYLOAD[1:]
        check(result(exchange(1, malformed)) == 10, "nonzero HELLO prefix BAD_PAYLOAD")
        check(control_status(control).get("session") == NIL_UUID,
              "malformed HELLO leaves session absent")
        check(control_status(control).get("event_count") == "1", "bad requests append no events")
        check(result(exchange(0x20, bytes(16))) == 14, "unknown request type UNSUPPORTED")

        # Corrupt claimed length covers the start of a complete valid HELLO.
        # Repeated byte splits must retain wire.c's byte-by-byte CRC recovery.
        false_header = struct.pack("<HBBHI", 0x4B4C, 1, 1, 49, 71)
        stream = false_header + GOLDEN_HELLO
        uart.sendall(stream[:19])
        uart.sendall(stream[19:])
        reply = reader.read(5)
        check(reply is not None and result(parse_frame(reply)["payload"]) == 0,
              "corrupt length resynchronizes to embedded complete HELLO")
        check(control_status(control).get("session") != NIL_UUID, "resync HELLO establishes session")
        # Two frames in one send are independently decoded and correlated.
        uart.sendall(build_frame(2, 80, session) + build_frame(2, 81, session))
        replies = [parse_frame(reader.read(5)), parse_frame(reader.read(5))]
        check([r["request_no"] for r in replies] == [80, 81], "coalesced UART frames decoded")

        epoch = uuid_text_to_bytes(control_status(control)["journal_epoch"])
        progress = [0]

        def prepare():
            snap = exchange(2, session)
            check(result(snap) == 0 and len(snap) == 189, "snapshot for execution")
            boot, sampled, revision = struct.unpack_from("<QQQ", snap, 34)
            boundary = snap[90:106]
            after = bytes(16)
            while True:
                page = exchange(3, session + epoch + after + b"\x04")
                check(result(page) == 0 and page[84] == 0, "FETCH has complete history")
                after = page[66:82]
                if page[83] == 0:
                    break
            check(result(exchange(4, session + epoch + boundary)) == 0, "ACK snapshot boundary")
            progress[0] += 1
            health = session + host + bytes(16) + struct.pack("<QQIIB", progress[0], 0,
                                                                           0, 0xFFFFFFFF, 0)
            check(result(exchange(9, health)) == 0, "fresh core health accepted")
            guard = session + epoch + struct.pack("<QQ", boot, revision) + boundary
            check(result(exchange(5, guard)) == 0, "recovery barrier reaches READY")
            return boot, sampled, revision

        # snapshot/zero ACK cannot by itself grant READY, and no action is admitted.
        snap = exchange(2, session)
        boot, sampled, revision = struct.unpack_from("<QQQ", snap, 34)
        check(result(exchange(5, session + epoch + struct.pack("<QQ", boot, revision) + bytes(16))) != 0,
              "RESUME cannot skip BOOT barrier or health")
        premature = session + epoch + struct.pack("<QQQQ", boot, revision, 1, sampled + 4000)
        premature += bytes([0x61]) * 16 + bytes([0x71]) * 16 + struct.pack("<HB", 1, 1)
        check(result(exchange(7, premature)) == 3, "EXECUTE requires READY and fresh health")
        check(control_status(control).get("action_watermark") == "0",
              "unready EXECUTE does not consume action watermark")
        pulse_count = 0
        for action, suppress in ((1, True), (2, False)):
            check(control_send(control, "suppress_pulse " + ("on" if suppress else "off")) == "ok",
                  "suppress_pulse independent control")
            check(control_send(control, "drop_execute_ack on") == "ok", "drop_execute_ack enabled")
            boot, sampled, revision = prepare()
            operation = bytes([0x60 + action]) * 16
            command = bytes([0x70 + action]) * 16
            intent = session + epoch + struct.pack("<QQQQ", boot, revision, action, sampled + 4000)
            intent += operation + command + struct.pack("<HB", 1, 1)
            if action == 1:
                uart.sendall(build_frame(7, 0, intent))
                check(reader.read(0.2) is None, "zero-number EXECUTE rejected before admission")
                check(control_status(control).get("action_watermark") == "0",
                      "zero-number EXECUTE leaves watermark unchanged")
                check(control_status(control).get("has_active") == "0",
                      "zero-number EXECUTE leaves context absent")
            # Exactly one valid EXECUTE submission. QUERY resolves uncertainty.
            uart.sendall(build_frame(7, 200 + action, intent))
            deadline = time.monotonic() + 0.5
            unexpected = []
            while time.monotonic() < deadline:
                raw = reader.read(0.1)
                if raw:
                    frame = parse_frame(raw)
                    if frame["type"] == 0x40:
                        received_events.append(frame)
                    else:
                        unexpected.append(frame)
            check(not unexpected, "accepted EXECUTE reply deliberately absent")
            query = exchange(8, session + epoch + command)
            check(result(query) == 0, "QUERY resolves lost EXECUTE acceptance response")
            check(query[18:34] == operation and query[34:50] == command,
                  "QUERY preserves original command association")
            check(query[50] == (1 if suppress else 3),
                  "suppressed pulse never starts; normal pulse finishes deterministically")
            with sqlite3.connect(journal) as db:
                starts = db.execute("SELECT count(*) FROM sim_events WHERE event_type=2").fetchone()[0]
                finishes = db.execute("SELECT count(*) FROM sim_events WHERE event_type=3").fetchone()[0]
            pulse_count += 0 if suppress else 1
            check(starts == pulse_count and finishes == pulse_count,
                  "one submission produces at most one pulse and durable finish")
            check(control_send(control, "drop_execute_ack off") == "ok", "drop_execute_ack disabled")
            snap = exchange(2, session)
            boot, _, revision = struct.unpack_from("<QQQ", snap, 34)
            release = session + epoch + struct.pack("<QQ", boot, revision) + operation + command
            check(result(exchange(11, release)) == 0, "matching finished/suppressed context releases")
            check(control_status(control).get("has_active") == "0", "release checkpoint clears context")
        check(control_send(control, "quit") == "ok", "execute daemon quits")
        check(process.wait(timeout=5) == 0, "execute daemon exits successfully")
        check(process.stderr.read() == b"", "execute daemon stderr empty")
    finally:
        if uart is not None:
            uart.close()
        if control is not None:
            control.close()
        if process.poll() is None:
            process.terminate()
            process.wait(timeout=5)


def test_daemon_journal_path_limits(binary, workdir):
    """R6-B-1: a default journal path must never be silently truncated.

    The journal lives in a 108-byte field and AF_UNIX sun_path is 108 bytes, so a
    control path of 107 bytes makes "<control>.journal" overflow the field. The
    old code discarded the snprintf result: the truncation landed exactly on the
    control path, the daemon deleted its own fresh database and replaced it with
    the socket, and still printed ready.
    """

    # The requested workdir is usually already longer than these limits, so the
    # exact-length cases get their own short base directory.
    bases = [workdir]

    def base_for(length, tag):
        # The probe must use the same tag prefix as exact_path: a shorter probe
        # (e.g. a fixed "x_") lets a directory look usable and then yields a
        # negative pad, which the caller can only report as a missing case.
        for base in bases:
            if len(os.path.join(base, tag + "_")) + len(".sock") <= length:
                return base
        created = tempfile.mkdtemp(prefix="locker-sim-lim-")
        bases.append(created)
        return created

    def exact_path(length, tag):
        base = base_for(length, tag)
        prefix = os.path.join(base, tag + "_")
        pad = length - len(prefix) - len(".sock")
        if pad < 0:
            return None
        return prefix + "p" * pad + ".sock"

    def clean(paths):
        for path in paths:
            for suffix in ("", "-wal", "-shm"):
                if os.path.exists(path + suffix):
                    os.unlink(path + suffix)

    def spawn(control_path, journal_arg, uart_path):
        argv = [binary, "--uart", uart_path, "--control", control_path]
        if journal_arg is not None:
            argv += ["--journal", journal_arg]
        return subprocess.Popen(argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE)

    def finish(process, label):
        """Wait for a daemon that should have exited; a survivor is a red check.

        A regression here does not fail an assertion inside the daemon: the
        process simply starts up and blocks in accept(). Letting that raise
        TimeoutExpired would turn a behavioural failure into a traceback, so the
        survivor is killed and reported as a named check instead.
        """
        try:
            code = process.wait(timeout=10)
            timed_out = False
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=10)
            code = -1
            timed_out = True
        stderr = process.stderr.read().decode("utf-8", "replace").strip()
        check(not timed_out, label + " exits instead of running as a daemon")
        return code, stderr

    # ---- 99-byte control path, default journal is 107 bytes: accepted ----
    control99 = exact_path(99, "c99")
    check(control99 is not None and len(control99) == 99, "built a 99-byte control path")
    if control99 is not None:
        uart99 = os.path.join(workdir, "uart99.sock")
        journal99 = control99 + ".journal"
        clean((uart99, control99, journal99))
        process = spawn(control99, None, uart99)
        ready = process.stdout.readline().decode("utf-8", "replace").strip()
        check(ready.startswith("ready device backend: simulator"),
              "a 99-byte control path with a default journal still starts")
        check(os.path.exists(journal99),
              "the default journal is <control>.journal, not a truncation")
        check(len(journal99) == 107, "the default journal path is 107 bytes, inside the field")
        control = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        control.connect(control99)
        check(control_send(control, "restart") == "ok",
              "the 99-byte case restarts the same journal")
        check(control_send(control, "quit") == "ok", "the 99-byte case quits")
        control.close()
        code, _ = finish(process, "the 99-byte case")
        check(code == 0, "the 99-byte case exits 0 after quit")
        clean((uart99, control99, journal99))

    # ---- 100 and 107 byte control paths: rejected before anything is created ----
    for length in (100, 107):
        control_path = exact_path(length, "rej%d" % length)
        uart_path = os.path.join(workdir, "uartrej%d.sock" % length)
        if control_path is None:
            check(False, "could not build a %d-byte control path" % length)
            continue
        check(len(control_path) == length, "built a %d-byte control path" % length)
        clean((uart_path, control_path, control_path + ".journal"))
        process = spawn(control_path, None, uart_path)
        code, stderr = finish(process, "the %d-byte case" % length)
        print("journal_limit_%d_exit=%d stderr=%s" % (length, code, stderr))
        check(code == 2, "%d-byte control path is rejected before opening a journal" % length)
        check("journal path too long" in stderr,
              "%d-byte control path reports the journal length" % length)
        check(not os.path.exists(control_path),
              "%d-byte case creates no control socket" % length)
        check(not os.path.exists(uart_path), "%d-byte case creates no uart socket" % length)
        check(not os.path.exists(control_path + ".journal"),
              "%d-byte case creates no full journal" % length)
        check(not os.path.exists(control_path + ".journa"),
              "%d-byte case creates no truncated journal" % length)

    # ---- 107-byte control path with an explicit short journal: accepted ----
    control107 = exact_path(107, "c107")
    check(control107 is not None and len(control107) == 107, "built a 107-byte control path")
    if control107 is not None:
        uart107 = os.path.join(workdir, "uart107.sock")
        short_journal = os.path.join(workdir, "short107.db")
        clean((uart107, control107, short_journal))
        process = spawn(control107, short_journal, uart107)
        ready = process.stdout.readline().decode("utf-8", "replace").strip()
        check(ready.startswith("ready device backend: simulator"),
              "a 107-byte control path with an explicit journal still starts")
        check(os.path.exists(short_journal), "the explicit journal is a separate file")
        control = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        control.connect(control107)
        check(control_send(control, "restart") == "ok",
              "the 107-byte control path restarts its explicit journal")
        check(control_send(control, "quit") == "ok", "the 107-byte case quits")
        control.close()
        code, _ = finish(process, "the 107-byte case")
        check(code == 0, "the 107-byte case exits 0 after quit")
        clean((uart107, control107, short_journal))

    # ---- an explicit journal that equals the control path is rejected too ----
    collision = exact_path(60, "coll")
    check(collision is not None and len(collision) == 60, "built a 60-byte control path")
    if collision is not None:
        uart_coll = os.path.join(workdir, "uartcoll.sock")
        clean((uart_coll, collision))
        process = spawn(collision, collision, uart_coll)
        code, stderr = finish(process, "the journal/socket collision case")
        print("journal_collision_exit=%d stderr=%s" % (code, stderr))
        check(code == 2, "an explicit journal equal to the control path is rejected")
        check("distinct filesystem entries" in stderr,
              "the collision is reported as a path-identity clash")
        check(not os.path.exists(collision),
              "the collision case creates nothing at the shared path")

    test_daemon_path_aliasing(binary, workdir)
    test_daemon_dangling_links(binary, workdir)


def test_daemon_dangling_links(binary, workdir):
    """R8-B-1: a dangling final symlink must not hide an aliased entry.

    realpath() answers ENOENT when a link's target does not exist yet, so the
    round-7 guard fell back to the parent directory and kept the link name.
    SQLite then created the link target while the socket bind unlinked that very
    entry: the daemon printed ready with a journal it had already deleted, and a
    restart answered io_error. Links are followed to their target here, and a
    configuration that cannot be resolved is refused before anything exists.
    """

    root = tempfile.mkdtemp(prefix="locker-sim-dangling-")

    def rejected(label, uart, control, journal, directory, links):
        """Refusal must leave every pre-existing link exactly as it was."""
        process = subprocess.Popen([binary, "--uart", uart, "--control", control,
                                    "--journal", journal],
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            code = process.wait(timeout=10)
            timed_out = False
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=10)
            code = -1
            timed_out = True
        stderr = process.stderr.read().decode("utf-8", "replace").strip()
        print("%s_exit=%d stderr=%s" % (label, code, stderr))
        check(not timed_out, label + " exits instead of serving a broken setup")
        check(code == 2, label + " is rejected before any resource exists")
        check("distinct filesystem entries" in stderr,
              label + " reports the path-identity clash")
        found = sorted(os.listdir(directory))
        check(found == sorted(links),
              label + " creates no file and removes none (found %r)" % (found,))
        for name, target in links.items():
            full = os.path.join(directory, name)
            check(os.path.islink(full) and os.readlink(full) == target,
                  label + " keeps the symbolic link " + name + " unchanged")

    def case(label):
        path = os.path.join(root, label)
        os.makedirs(path, exist_ok=True)
        return path

    def linked(label, uart_name, control_name, journal_name, target, extra=()):
        """Creates the links, then expects the daemon to refuse the setup."""
        directory = case(label)
        links = {journal_name: target}
        links.update(dict(extra))
        for name, link_target in links.items():
            os.symlink(link_target, os.path.join(directory, name))
        rejected(label,
                 os.path.join(directory, uart_name),
                 os.path.join(directory, control_name),
                 os.path.join(directory, journal_name),
                 directory, links)

    # ---- a dangling journal link that points at either socket entry ----
    linked("link_to_control", "u.sock", "c.sock", "j.db", "c.sock")
    linked("link_to_uart", "u.sock", "c.sock", "j.db", "u.sock")

    # ---- the same alias written as an absolute target ----
    directory = case("link_absolute")
    absolute_target = os.path.join(directory, "c.sock")
    os.symlink(absolute_target, os.path.join(directory, "j.db"))
    rejected("link_absolute", os.path.join(directory, "u.sock"),
             os.path.join(directory, "c.sock"), os.path.join(directory, "j.db"),
             directory, {"j.db": absolute_target})

    # ---- two hops, so a single-level check would still miss it ----
    linked("link_chain", "u.sock", "c.sock", "j.db", "mid", extra=(("mid", "c.sock"),))

    # ---- a symlinked parent directory that collapses onto the socket ----
    directory = case("link_parent")
    os.symlink(".", os.path.join(directory, "here"))
    rejected("link_parent", os.path.join(directory, "u.sock"),
             os.path.join(directory, "c.sock"),
             os.path.join(directory, "here", "c.sock"), directory, {"here": "."})

    # ---- a socket that sits on the journal sidecar of the link target ----
    linked("link_wal", "u.sock", "real.db-wal", "j.db", "real.db")
    linked("link_shm", "u.sock", "real.db-shm", "j.db", "real.db")

    # ---- a link loop cannot be resolved, so the setup is refused ----
    directory = case("link_loop")
    os.symlink("j2", os.path.join(directory, "j.db"))
    os.symlink("j.db", os.path.join(directory, "j2"))
    rejected("link_loop", os.path.join(directory, "u.sock"),
             os.path.join(directory, "c.sock"), os.path.join(directory, "j.db"),
             directory, {"j.db": "j2", "j2": "j.db"})

    # ---- a socket on the sidecar of the journal spelling (not its target) ----
    # The spelling half is this module's own conservative rule, not a SQLite
    # requirement (the VFS resolves the link); the case keeps it observable.
    linked("link_spelling_wal", "u.sock", "j.db-wal", "j.db", "real.db")
    linked("link_spelling_shm", "u.sock", "j.db-shm", "j.db", "real.db")

    # ---- a chain longer than the hop budget is refused on purpose ----
    directory = case("link_hops")
    links = {}
    for index in range(9):
        name = "h%d" % index
        target = "h%d" % (index + 1)
        os.symlink(target, os.path.join(directory, name))
        links[name] = target
    os.symlink("nowhere", os.path.join(directory, "h9"))
    links["h9"] = "nowhere"
    os.symlink("h0", os.path.join(directory, "j.db"))
    links["j.db"] = "h0"
    rejected("link_hops", os.path.join(directory, "u.sock"),
             os.path.join(directory, "c.sock"), os.path.join(directory, "j.db"),
             directory, links)

    # ---- positive control: a link to a genuinely separate file still works ----
    directory = case("link_ok")
    uart = os.path.join(directory, "u.sock")
    control = os.path.join(directory, "c.sock")
    journal = os.path.join(directory, "j.db")
    os.symlink("real.db", journal)
    process = subprocess.Popen([binary, "--uart", uart, "--control", control,
                                "--journal", journal],
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        ready = process.stdout.readline().decode("utf-8", "replace").strip()
        check(ready.startswith("ready device backend: simulator"),
              "a journal link to a separate file still starts")
        client = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        client.connect(control)
        check(control_send(client, "restart") == "ok",
              "the linked journal survives a restart")
        check(control_send(client, "quit") == "ok", "the linked journal quits")
        client.close()
        check(process.wait(timeout=5) == 0, "the linked journal exits 0")
        check(os.path.exists(os.path.join(directory, "real.db")),
              "the journal is created at the link target")
        check(os.path.islink(journal), "the link itself is left in place")
    finally:
        if process.poll() is None:
            process.kill()
            process.wait(timeout=5)

    for name in os.listdir(root):
        directory = os.path.join(root, name)
        for entry in os.listdir(directory):
            full = os.path.join(directory, entry)
            try:
                os.unlink(full)
            except OSError:
                pass
        try:
            os.rmdir(directory)
        except OSError:
            pass
    try:
        os.rmdir(root)
    except OSError:
        pass


def test_daemon_path_aliasing(binary, workdir):
    """R7-B-1 / R7-B-2: distinct spellings must not reach one filesystem entry.

    The round-6 guard compared raw strings, so "D/./c" and "D/c" passed it. The
    daemon then opened its journal, unlinked that same entry while binding the
    control socket, and still printed ready; a restart failed and only -wal/-shm
    survived. A socket placed on "<journal>-wal" was replaced on the next open,
    so the control entry silently stopped being a socket. Two identical socket
    paths also both bound, with the second unlink routing UART clients into the
    text control parser.
    """

    root = tempfile.mkdtemp(prefix="locker-sim-alias-")

    def rejected(label, argv_paths, case_dir):
        """A conflicting configuration must exit 2 and create nothing at all."""
        process = subprocess.Popen([binary] + argv_paths,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            code = process.wait(timeout=10)
            timed_out = False
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=10)
            code = -1
            timed_out = True
        stderr = process.stderr.read().decode("utf-8", "replace").strip()
        print("%s_exit=%d stderr=%s" % (label, code, stderr))
        check(not timed_out, label + " exits instead of serving a broken setup")
        check(code == 2, label + " is rejected before any resource exists")
        check("distinct filesystem entries" in stderr,
              label + " reports the path-identity clash")
        leftovers = sorted(os.listdir(case_dir))
        check(not leftovers, label + " creates no file or socket (found %r)" % (leftovers,))

    def case(label):
        path = os.path.join(root, label)
        os.makedirs(path, exist_ok=True)
        return path

    def run_rejected(label, uart_name, control_name, journal_name):
        """Each case owns a directory, so "created nothing" is exact."""
        directory = case(label)
        uart = os.path.join(directory, uart_name)
        control = os.path.join(directory, control_name)
        journal = os.path.join(directory, journal_name)
        rejected(label, ["--uart", uart, "--control", control, "--journal", journal],
                 directory)

    # ---- a journal that aliases a socket entry through a dot segment ----
    run_rejected("alias_control", "u.sock", "c.sock", "./c.sock")
    run_rejected("alias_uart", "u.sock", "c.sock", "./u.sock")

    # ---- a socket sitting on the journal's -wal / -shm names ----
    run_rejected("wal_collision", "u.sock", "j.db-wal", "j.db")
    run_rejected("shm_collision", "u.sock", "j.db-shm", "j.db")

    # ---- the two protocol entries must stay separately reachable ----
    run_rejected("same_sockets", "c.sock", "c.sock", "j.db")
    run_rejected("same_alias", "c.sock", "c.sock", "./c.sock")
    run_rejected("same_alias_sockets", "u.sock", "./u.sock", "j.db")

    # ---- positive control: a dot segment that names a different entry is fine ----
    directory = case("ok")
    uart = os.path.join(directory, "u.sock")
    control = os.path.join(directory, "c.sock")
    journal = os.path.join(directory, ".", "j.db")
    process = subprocess.Popen(
        [binary, "--uart", uart, "--control", control, "--journal", journal],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    try:
        ready = process.stdout.readline().decode("utf-8", "replace").strip()
        check(ready.startswith("ready device backend: simulator"),
              "distinct entries still start even when spelled with a dot segment")
        client = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        client.connect(control)
        check(control_send(client, "restart") == "ok",
              "the accepted configuration restarts its journal")
        check(control_send(client, "quit") == "ok", "the accepted configuration quits")
        client.close()
        check(process.wait(timeout=5) == 0, "the accepted configuration exits 0")
    finally:
        if process.poll() is None:
            process.kill()
            process.wait(timeout=5)

    for name in os.listdir(root):
        directory = os.path.join(root, name)
        for entry in os.listdir(directory):
            full = os.path.join(directory, entry)
            try:
                os.unlink(full)
            except OSError:
                pass
        try:
            os.rmdir(directory)
        except OSError:
            pass
    try:
        os.rmdir(root)
    except OSError:
        pass

def main():
    if len(sys.argv) != 3:
        print("usage: daemon_smoke.py <locker-device-sim> <workdir>")
        return 2
    binary, requested = sys.argv[1], sys.argv[2]
    # Two fixed limits force a short directory: the journal lives in a 108-byte
    # field (LOCKER_SIM_PATH_SIZE) and AF_UNIX sun_path is 108 bytes with the
    # terminator. Check the longest name we build, not just the journal.
    names = ("control.sock", "uart.sock", "device.db", "device.db-wal", "device.db-shm")
    longest = max(len(os.path.join(requested, name)) for name in names)
    workdir = requested
    if longest >= 108:
        workdir = tempfile.mkdtemp(prefix="locker-sim-")
        print("workdir_fallback=%s (requested path was %d bytes)" % (workdir, longest))
    os.makedirs(workdir, exist_ok=True)
    uart_path = os.path.join(workdir, "uart.sock")
    control_path = os.path.join(workdir, "control.sock")
    journal_path = os.path.join(workdir, "device.db")
    for path in (uart_path, control_path, journal_path,
                 journal_path + "-wal", journal_path + "-shm"):
        if os.path.exists(path):
            os.unlink(path)

    process = subprocess.Popen(
        [binary, "--uart", uart_path, "--control", control_path,
         "--journal", journal_path],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    ready = process.stdout.readline().decode("utf-8", "replace").strip()
    print("ready_line=" + ready)
    if not ready:
        print("stderr=" + process.stderr.read().decode("utf-8", "replace").strip())
        print("DAEMON_CHECKS=0")
        print("DAEMON_FAILURES=1")
        print("FAILED daemon prints the ready marker")
        return 1
    check(ready.startswith("ready device backend: simulator uart="),
          "daemon prints the explicit simulator backend ready marker")

    for path, label in ((uart_path, "uart"), (control_path, "control")):
        mode = stat.S_IMODE(os.stat(path).st_mode)
        print("socket_mode_%s=0%o" % (label, mode))
        check(mode == 0o600, label + " socket is mode 0600")

    control = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    control.connect(control_path)
    uart = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    uart.connect(uart_path)
    reader = UartReader(uart)

    # ---- fresh open: boot 1, no session, one BOOT event, private journal ----
    status = control_status(control)
    print("status_before=" + repr(sorted(status.items())))
    check(status.get("boot_id") == "1", "first open uses boot_id 1")
    check(status.get("session") == NIL_UUID, "no session before HELLO")
    check(status.get("journal_state") == "1", "journal is HEALTHY after open")
    check(status.get("device_mode") == "0", "device is RECONCILING after open")
    check(status.get("event_count") == "1", "only the BOOT event is journaled")
    check(status.get("door") == "1", "door defaults to closed")
    check(status.get("item") == "2", "item defaults to present")
    check(status.get("ack_persisted") == "0:0", "no ACK before the first page")
    check(status.get("first_available") == "1:1", "BOOT is the first record")
    check(status.get("last_committed") == "1:1", "BOOT is the last record")
    first_epoch = status.get("journal_epoch", "")
    check(len(first_epoch) == 36, "journal epoch is a UUID")

    header = open(journal_path, "rb").read(16)
    check(header == b"SQLite format 3\x00", "device journal is a SQLite file")
    check(os.path.dirname(os.path.realpath(journal_path)) == os.path.realpath(workdir),
          "device journal lives in its own directory, not a business database")
    check(os.path.exists(journal_path + "-wal"), "device journal runs in WAL mode")

    # ---- golden HELLO, byte for byte ----
    uart.sendall(GOLDEN_HELLO)
    frame = reader.read(5.0)
    check(frame is not None, "HELLO gets a reply")
    if frame is not None:
        reply = parse_frame(frame)
        check(reply["magic"] == 0x4B4C, "reply magic is 4c4b")
        check(reply["version"] == 1, "reply version is 1")
        check(reply["type"] == 0x81, "reply type is HELLO|0x80")
        check(reply["request_no"] == 1, "reply echoes request_no 1")
        check(reply["payload_len"] == 71, "HELLO reply payload is 71 bytes")
        check(reply["crc_ok"], "HELLO reply CRC-16/CCITT-FALSE matches")
        check(reply["payload"][0:16] == GOLDEN_HELLO[26:42],
              "reply session echoes new_session_id")
        check(struct.unpack_from("<H", reply["payload"], 16)[0] == 0, "HELLO result is OK")
        check(reply["payload"][18:30] == bytes(range(0x40, 0x4C)),
              "HELLO reports the default device uid 0x40..0x4b")
        check(struct.unpack_from("<Q", reply["payload"], 46)[0] == 1, "HELLO reports boot 1")

    status = control_status(control)
    print("status_after_hello=" + repr(sorted(status.items())))
    check(status.get("session") == "00000000-0000-4000-8000-00000000000a",
          "control socket shows the handshaken session")
    check(status.get("device_mode") == "0", "mode stays RECONCILING until RESUME_READY")

    # ---- private control surface: door, item, card ----
    check(control_send(control, "door open") == "ok", "door open accepted")
    check(control_send(control, "door bogus") == "error bad_argument", "bad door rejected")
    check(control_send(control, "nonsense") == "error missing_argument",
          "a bare command without an argument is reported as missing_argument")
    check(control_send(control, "nonsense x") == "error unknown_command",
          "unknown command rejected")
    check(control_send(control, "card 01020304") == "ok", "card injection accepted")
    check(control_send(control, "card 010203") == "error invalid",
          "card uid length must be 0/4/7/10")
    check(control_send(control, "card 010") == "error bad_argument",
          "an odd-length hex uid is a syntax error")
    check(control_send(control, "door") == "error missing_argument", "missing argument rejected")
    for bad in ("card 0g0g0g0g", "inject_gap -1", "inject_gap +1", "inject_gap 1junk",
                "inject_gap 18446744073709551616", "cursor 1:2junk", "cursor -1:2",
                "cursor 1:0", "status extra", "restart extra", "quit extra"):
        check(control_send(control, bad) == "error bad_argument", "strict control rejects " + bad)
    # A valid-looking suffix of an overlong line must be discarded across reads.
    control.sendall(b"x" * 256)
    control.settimeout(5.0)
    rejected_line = b""
    while not rejected_line.endswith(b"\n"):
        rejected_line += control.recv(256)
    check(rejected_line.strip() == b"error line_too_long", "overlong line rejected once")
    control.sendall(b"quit\n")
    check(control_send(control, "cursor 1:2") == "ok 1:2",
          "overlong suffix cannot execute quit and next line is parsed")
    control.sendall(b"cursor 2:3\ncursor 3:4\n")
    data = b""
    while data.count(b"\n") < 2:
        data += control.recv(256)
    check(data == b"ok 2:3\nok 3:4\n", "coalesced complete control lines parsed")

    status = control_status(control)
    check(status.get("door") == "2", "door state is open")
    check(status.get("event_count") == "3", "door and card events are journaled")

    # Control events must arrive without a UART request to trigger delivery.
    events = []
    for _ in range(2):
        event = reader.read(2.0)
        if event is not None:
            events.append(parse_frame(event))
    check(len(events) == 2, "control produces two immediate asynchronous events")

    # ---- golden GET_SNAPSHOT: its reply follows the already delivered events ----
    uart.sendall(GOLDEN_SNAPSHOT)
    frames = []
    deadline = time.time() + 5.0
    while time.time() < deadline and len(frames) < 1:
        frame = reader.read(1.0)
        if frame is None:
            break
        frames.append(parse_frame(frame))
    print("snapshot_frame_types=" + repr([(f["type"], f["payload_len"], f["request_no"])
                                          for f in frames]))
    check(len(frames) == 1, "snapshot returns exactly one response")
    if frames:
        check(frames[0]["type"] == 0x82, "first frame is the snapshot reply")
        check(frames[0]["request_no"] == 2, "snapshot reply echoes request_no 2")
        check(frames[0]["payload_len"] == 189, "snapshot reply payload is 189 bytes")
        check(frames[0]["crc_ok"], "snapshot reply CRC matches")
        check(frames[0]["payload"][0:16] == GOLDEN_HELLO[26:42], "snapshot session matches")
        check(struct.unpack_from("<H", frames[0]["payload"], 16)[0] == 0,
              "snapshot result is OK")
    for event in events:
        check(event["type"] == 0x40, "async frame type is DEVICE_EVENT")
        check(event["request_no"] == 0, "async frame request_no is 0")
        check(event["payload_len"] == 128, "async payload is 128 bytes")
        check(event["crc_ok"], "async frame CRC matches")
        check(event["payload"][0:16] == GOLDEN_HELLO[26:42],
              "async frame carries the current session")
        check(event["payload"][16:32] == uuid_text_to_bytes(first_epoch),
              "async frame carries the journal epoch")

    # ---- ACK_PERSISTED, including the deliberately dropped reply ----
    ack_payload = (GOLDEN_HELLO[26:42] + uuid_text_to_bytes(first_epoch)
                   + struct.pack("<QQ", 1, 3))
    ack_frame = build_frame(4, 9, ack_payload)
    fetch = (GOLDEN_HELLO[26:42] + uuid_text_to_bytes(first_epoch) + bytes(16) + b"\x04")
    uart.sendall(build_frame(3, 8, fetch))
    fetched = reader.read(5.0)
    check(fetched is not None, "FETCH before persisted ACK gets a page")
    if fetched is not None:
        page = parse_frame(fetched)
        check(page["type"] == 0x83 and page["crc_ok"], "FETCH response type and CRC")
        check(struct.unpack_from("<H", page["payload"], 16)[0] == 0,
              "FETCH before ACK succeeds")
        check(page["payload"][66:82] == struct.pack("<QQ", 1, 3),
              "FETCH reaches all three committed records")
    check(control_send(control, "drop_ack on") == "ok", "drop_ack on accepted")
    uart.sendall(ack_frame)
    frame = reader.read(1.0)
    check(frame is None, "drop_ack withholds the ACK reply frame")
    status = control_status(control)
    print("status_after_ack=" + repr(sorted(status.items())))
    check(status.get("ack_persisted") == "1:3", "the ACK is applied even when the reply is dropped")
    check(control_send(control, "drop_ack off") == "ok", "drop_ack off accepted")
    uart.sendall(ack_frame)
    frame = reader.read(5.0)
    check(frame is not None, "a repeated ACK gets a reply once drop_ack is off")
    if frame is not None:
        reply = parse_frame(frame)
        check(reply["type"] == 0x84, "ACK reply type is ACK|0x80")
        check(reply["payload_len"] == 34, "ACK reply payload is 34 bytes")
        check(reply["crc_ok"], "ACK reply CRC matches")

    # ---- gap injection latches HISTORY_INCOMPLETE and keeps the journal ----
    check(control_send(control, "inject_gap 3") == "ok", "gap injection accepted")
    check(control_send(control, "inject_gap 0") == "error invalid", "zero gap rejected")
    check(control_send(control, "inject_gap x") == "error bad_argument", "non-numeric gap rejected")
    status = control_status(control)
    print("status_after_gap=" + repr(sorted(status.items())))
    check(status.get("dropped_events") == "3", "dropped_events counts the gap")
    check(status.get("continuity_ok") == "0", "gap latches the continuity fault")
    check(status.get("event_count") == "4", "the gap itself is journaled as CONTINUITY_LOST")
    # The CONTINUITY_LOST record is a live DEVICE_EVENT, not just a journal row:
    # uart-v1.md:353-356 delivers it while a session is alive, so it must be read
    # off the link here or it becomes a stale frame later in the sequence.
    gap_frame = reader.read(2.0)
    check(gap_frame is not None, "the gap is pushed as a DEVICE_EVENT")
    if gap_frame is not None:
        gap_event = parse_frame(gap_frame)
        check(gap_event["type"] == 0x40 and gap_event["crc_ok"],
              "the gap event is a well-formed DEVICE_EVENT")
        check(gap_event["payload_len"] == 128, "the gap event carries one 96-byte record")
        # payload = session(16) + journal_epoch(16) + event96; event_type sits at
        # event96 offset 24 (docs/architecture/uart-v1.md:315-339).
        event_type = struct.unpack_from("<H", gap_event["payload"], 16 + 16 + 24)[0]
        check(event_type == 14, "the gap event is CONTINUITY_LOST")

    # ---- restart: boot bumps, epoch and watermarks survive, session dies ----
    check(control_send(control, "restart") == "ok", "restart accepted")
    status = control_status(control)
    print("status_after_restart=" + repr(sorted(status.items())))
    check(status.get("boot_id") == "2", "restart bumps boot_id")
    check(status.get("journal_epoch") == first_epoch, "restart keeps the journal epoch")
    check(status.get("session") == NIL_UUID, "restart drops the session")
    check(status.get("action_watermark") == "0", "no action was accepted before restart")
    check(status.get("dropped_events") == "3", "restart keeps dropped_events")
    check(status.get("continuity_ok") == "0", "restart keeps the continuity latch")
    check(status.get("ack_persisted") == "1:3", "restart keeps the persisted ACK")
    check(status.get("event_count") == "5", "restart journals a second BOOT event")
    check(status.get("first_available") == "1:1", "the first record survives restart")
    check(status.get("last_committed") == "2:1", "the new BOOT is last committed")

    # ---- the pre-restart session is dead ----
    uart.sendall(GOLDEN_SNAPSHOT)
    frame = reader.read(5.0)
    check(frame is not None, "a stale snapshot still gets a reply")
    if frame is not None:
        reply = parse_frame(frame)
        check(reply["type"] == 0x82, "stale snapshot reply type is SNAPSHOT|0x80")
        check(reply["payload_len"] == 18, "a rejection carries no body")
        # uart-v1.md:29: a stale-session rejection must echo the session carried by
        # the rejected request, not the daemon's current session. The golden
        # snapshot payload starts with that (dead) session id.
        check(reply["payload"][0:16] == GOLDEN_SNAPSHOT_PAYLOAD[0:16],
              "the stale-session rejection echoes the request session")
        check(struct.unpack_from("<H", reply["payload"], 16)[0] == 2,
              "stale session is rejected with BAD_SESSION")
        check(reply["crc_ok"], "rejection CRC matches")

    # ---- a frame with a broken CRC must not be answered or crash the daemon ----
    corrupt = bytearray(GOLDEN_HELLO)
    corrupt[-1] ^= 0xFF
    uart.sendall(bytes(corrupt))
    frame = reader.read(1.0)
    check(frame is None, "a bad CRC frame is dropped, not answered")
    status = control_status(control)
    check(status.get("boot_id") == "2", "the daemon is still alive after the bad frame")

    # ---- the daemon's own frame splitter resyncs on garbage instead of dying ----
    # This is sim_main.c's splitter, not wire.c's: a real UART link can deliver
    # noise after a line glitch, and the service must find the next magic.
    uart.sendall(b"\x00\xff\x00\x11")
    uart.sendall(GOLDEN_HELLO)
    frame = reader.read(5.0)
    check(frame is not None, "the daemon resyncs after leading garbage")
    if frame is not None:
        reply = parse_frame(frame)
        check(reply["type"] == 0x81, "the resynced frame is answered as HELLO|0x80")
        check(struct.unpack_from("<H", reply["payload"], 16)[0] == 0,
              "the resynced HELLO succeeds")
        check(reply["crc_ok"], "the resynced reply CRC matches")
    status = control_status(control)
    check(status.get("event_count") == "5", "resync does not journal an event")

    # ---- the control surface can inject a link break (card requirement) ----
    check(control_send(control, "drop_link") == "ok", "drop_link accepted")
    uart.settimeout(2.0)
    try:
        tail = uart.recv(4096)
    except socket.timeout:
        tail = b"<timeout>"
    except OSError:
        tail = b""
    check(tail == b"", "drop_link closes the accepted uart socket")
    status = control_status(control)
    check(status.get("boot_id") == "2", "drop_link leaves the device journal intact")
    check(status.get("event_count") == "5", "drop_link does not journal an event")

    # A reconnect is a fresh HELLO served by the same journal: the injected break
    # is a transport fault, not a device reset.
    uart = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    uart.settimeout(5.0)
    uart.connect(uart_path)
    reader = UartReader(uart)
    uart.sendall(GOLDEN_HELLO)
    frame = reader.read(5.0)
    check(frame is not None, "the daemon accepts a reconnect after drop_link")
    if frame is not None:
        reply = parse_frame(frame)
        check(reply["type"] == 0x81, "reconnect reply is HELLO|0x80")
        check(struct.unpack_from("<H", reply["payload"], 16)[0] == 0, "reconnect HELLO succeeds")
        check(struct.unpack_from("<Q", reply["payload"], 46)[0] == 2,
              "reconnect reports the same boot_id, so no reset happened")
    status = control_status(control)
    check(status.get("event_count") == "5", "reconnect does not journal an event")

    check(control_send(control, "quit") == "ok", "quit acknowledged")
    try:
        code = process.wait(timeout=10)
    except subprocess.TimeoutExpired:
        process.kill()
        code = -1
    print("daemon_exit=%d" % code)
    check(code == 0, "daemon exits 0 after quit")
    check(not os.path.exists(uart_path), "uart socket removed on exit")
    check(not os.path.exists(control_path), "control socket removed on exit")

    stderr = process.stderr.read().decode("utf-8", "replace").strip()
    if stderr:
        print("stderr=" + stderr)
    check(stderr == "", "daemon writes nothing to stderr")

    test_daemon_execute_loss(binary, workdir)
    test_daemon_journal_path_limits(binary, workdir)
    print("DAEMON_CHECKS=%d" % CHECKS[0])
    print("DAEMON_FAILURES=%d" % len(FAILURES))
    if FAILURES:
        print("FAILED " + "; ".join(FAILURES))
        return 1
    print("PASS locker-device-sim smoke (%d checks)" % CHECKS[0])
    return 0


if __name__ == "__main__":
    sys.exit(main())