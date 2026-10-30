"""S1-06 process acceptance: real executables, SQLite, AF_UNIX and trusted TLS.

No production fixture or credentials are retained. Run on Linux; argv is
locker-service, locker-admin, locker-device-sim, pinned openssl respectively.
"""
import fcntl
import errno
import http.client
import json
import os
import pty
from pathlib import Path
import socket
import sqlite3
import ssl
import stat
import signal
import subprocess
import sys
import tempfile
import termios
import threading
import time

from health_wire import HEALTH, HELLO, WireProxy


SERVICE, ADMIN, SIMULATOR, OPENSSL = sys.argv[1:]
PASSWORD = "s106-private-stdin-password"
USERNAME = "s106admin"
TOOL = 'Single tool "quoted"'


def check(condition, message):
    if not condition:
        raise AssertionError(message)


def wait_for(predicate, process, message, timeout=15):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            return
        check(process.poll() is None, message + ": process exited")
        time.sleep(0.05)
    raise AssertionError(message + ": timeout")


def run(args, success=False, password=None):
    result = subprocess.run(args, input=password, text=True, capture_output=True, timeout=20)
    check((result.returncode == 0) == success,
          "unexpected exit code %s: %s" % (result.returncode, result.stdout + result.stderr))
    check(PASSWORD not in result.stdout + result.stderr, "password leaked in output")
    if not success:
        check(result.returncode > 0 and bool(result.stderr.strip()), "failure lacks diagnostic or crashed")
    return result


def request(config, context, method, path, body=None, token=None, timeout=4):
    connection = http.client.HTTPSConnection("127.0.0.1", config["port"], context=context, timeout=timeout)
    headers = {"Content-Type": "application/json"}
    if token:
        headers["Authorization"] = "Bearer " + token
    try:
        connection.request(method, "/api/v1" + path,
                           json.dumps(body) if body is not None else None, headers)
        response = connection.getresponse()
        return response.status, json.loads(response.read())
    finally:
        connection.close()


def documents(database, collection):
    with sqlite3.connect("file:" + str(database) + "?mode=ro", uri=True) as connection:
        return [json.loads(row[0]) for row in connection.execute(
            "SELECT value_json FROM documents WHERE collection=? ORDER BY key", (collection,))]


def observed_health(proxy, start):
    records = proxy.snapshot()[start:]
    hellos = [record[3] for record in records if record[2] == HELLO]
    health = [record for record in records if record[2] == HEALTH]
    if health:
        check(bool(hellos), "HOST_HEALTH preceded HELLO")
        host = hellos[0]["host_instance_id"]
        check(host != bytes(16) and all(hello["host_instance_id"] == host for hello in hellos),
              "HELLO host identity changed within service lifetime")
        for _, _, _, sample in health:
            check(sample["host_instance_id"] == host, "HOST_HEALTH identity differs from HELLO")
            check(sample["session_id"] in [hello["new_session_id"] for hello in hellos],
                  "HOST_HEALTH session differs from HELLO")
            check(sample["ui_instance_id"] == bytes(16) and sample["ui_progress"] == 0
                  and sample["ui_observed_age_ms"] == 0xffffffff and sample["supervisor_state"] == 0,
                  "wire health invents UI or supervisor liveness")
        for previous, current in zip(health, health[1:]):
            check(current[0] - previous[0] >= 0.7,
                  "HOST_HEALTH sent too frequently: possible duplicate sender")
    return health


def verify_stalled_core(proxy, start, server, config, context, database):
    wait_for(lambda: len(observed_health(proxy, start)) >= 4, server, "normal wire health")
    normal = observed_health(proxy, start)
    check(normal[-1][3]["core_progress"] > normal[0][3]["core_progress"] > 0,
          "normal Core did not advance its actual wire progress")
    check(all(current[0] - previous[0] <= 2.5 for previous, current in zip(normal, normal[1:])),
          "normal HOST_HEALTH missed per-second cadence")
    outcomes = []

    def login():
        try:
            outcomes.append(request(config, context, "POST", "/sessions",
                                    {"username": USERNAME, "password": PASSWORD}, timeout=12)[0])
        except BaseException as error:
            outcomes.append(error)

    connection = sqlite3.connect(str(database), timeout=6)
    worker = threading.Thread(target=login, daemon=True)
    try:
        connection.execute("BEGIN IMMEDIATE")
        began = time.monotonic()
        worker.start()
        deadline = began + 4.5
        while time.monotonic() < deadline:
            check(server.poll() is None, "service exited during real SQLite lock contention")
            proxy.snapshot()
            time.sleep(0.05)
        stalled = [record for record in observed_health(proxy, start) if record[0] >= began + 0.5]
        groups = {}
        for record in stalled:
            groups.setdefault(record[3]["core_progress"], []).append(record)
        same = max(groups.values(), key=len) if groups else []
        check(len(same) >= 3 and same[0][3]["core_progress"] > 0,
              "real Core did not stall while login contended for SQLite writer lock")
        ages = [record[3]["core_observed_age_ms"] for record in same]
        check(all(age != 0xffffffff for age in ages) and ages[-1] - ages[0] >= 1500
              and all(right > left for left, right in zip(ages, ages[1:])),
              "stopped Core counter was falsely renewed by runtime health sender")
        print("wire stalled Core evidence: " + json.dumps([
            dict(elapsed_ms=round((record[0] - began) * 1000),
                 progress=record[3]["core_progress"], age_ms=record[3]["core_observed_age_ms"])
            for record in same]))
    finally:
        connection.rollback()
        connection.close()
        if worker.ident is not None:
            worker.join(timeout=15)
    check(not worker.is_alive() and len(outcomes) == 1 and outcomes[0] in (200, 503),
          "contended real HTTPS login failed unexpectedly: " + str(outcomes))
    released = time.monotonic()
    frozen = same[-1][3]["core_progress"]
    wait_for(lambda: any(record[0] >= released and record[3]["core_progress"] > frozen
                         for record in observed_health(proxy, start)), server, "Core recovery after writer lock")
    print("wire health: HELLO identity, unknown UI, single sender cadence, real Core stall/recovery passed")


def terminal_input(args, config_file, config, state, interrupt):
    terminal_config = dict(config, state_dir=str(state))
    config_file.write_text(json.dumps(terminal_config), encoding="utf-8")
    master, slave = pty.openpty()
    original = termios.tcgetattr(slave)
    process = subprocess.Popen(args, stdin=slave, stdout=slave, stderr=slave)
    output = bytearray()
    try:
        wait_for(lambda: not (termios.tcgetattr(slave)[3] & termios.ECHO),
                 process, "terminal password echo was not disabled")
        check(not (termios.tcgetattr(slave)[3] & termios.ECHONL), "terminal newline echo remains enabled")
        check(PASSWORD.encode() not in Path("/proc/%s/cmdline" % process.pid).read_bytes(),
              "terminal password leaked in argv")
        if interrupt:
            os.write(master, PASSWORD[:8].encode())
            process.send_signal(signal.SIGINT)
            check(process.wait(timeout=3) != 0, "SIGINT accepted incomplete terminal initialization")
            check(not (state / "business.db").exists(), "cancelled terminal input created DB")
        else:
            os.write(master, (PASSWORD + "\n").encode())
            check(process.wait(timeout=20) == 0, "terminal password initialization failed")
            check(len(documents(state / "business.db", "users")) == 1, "terminal init did not persist admin")
        check(termios.tcgetattr(slave)[3] & (termios.ECHO | termios.ECHONL)
              == original[3] & (termios.ECHO | termios.ECHONL), "terminal echo was not restored")
        os.set_blocking(master, False)
        while True:
            try:
                chunk = os.read(master, 4096)
                if not chunk:
                    break
                output.extend(chunk)
            except BlockingIOError:
                break
            except OSError as error:
                if error.errno != errno.EIO:
                    raise
                break
        check(PASSWORD.encode() not in output and PASSWORD[:8].encode() not in output,
              "terminal password was echoed or logged")
    finally:
        if process.poll() is None:
            process.kill()
            process.wait(timeout=5)
        os.close(master)
        os.close(slave)
        config_file.write_text(json.dumps(config), encoding="utf-8")


def main():
    with tempfile.TemporaryDirectory(prefix="locker-runtime-") as directory:
        root = Path(directory).resolve()
        cert, key = root / "cert.pem", root / "key.pem"
        subprocess.run([OPENSSL, "req", "-x509", "-newkey", "rsa:2048", "-nodes",
                        "-config", "/dev/null", "-keyout", str(key), "-out", str(cert),
                        "-days", "1", "-subj", "/CN=localhost", "-addext",
                        "subjectAltName=DNS:localhost,IP:127.0.0.1"], check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=20)
        key.chmod(0o600)
        with socket.socket() as probe:
            probe.bind(("127.0.0.1", 0))
            port = probe.getsockname()[1]
        state = root / "state"
        config = dict(mode="SIMULATOR_ONLY", state_dir=str(state),
                      device_path=str(root / "proxy.sock"), device_id="404142434445464748494a4b",
                      certificate=str(cert), private_key=str(key), bind_address="127.0.0.1",
                      port=port, ui_uid=os.geteuid(), admin_uid=os.geteuid(), cabinet_name="S1-06")
        config_file = root / "config.json"

        def write_config(value=config):
            config_file.write_text(json.dumps(value), encoding="utf-8")

        def admin_args():
            return [ADMIN, "init", "--config", str(config_file), "--username", USERNAME,
                    "--tool-name", TOOL, "--password-stdin"]

        def service_args():
            return [SERVICE, "--config", str(config_file)]

        write_config()
        run(service_args())
        check(not (state / "business.db").exists(), "service auto-created a database")
        state.mkdir(mode=0o700, exist_ok=True)
        run(service_args())
        check(not (state / "business.db").exists(), "service created default credentials")

        # Each invalid config fails before initialization creates a DB.
        variants = [dict(config, mode="HARDWARE"), dict(config, mode="SIMULATOR_ONLY\x00HARDWARE"),
                    dict(config, port=0),
                    dict(config, port=1.5), dict(config, state_dir="relative"),
                    dict(config, device_id="bad"), dict(config, unexpected=True)]
        missing = dict(config)
        missing.pop("admin_uid")
        variants.append(missing)
        for invalid in variants:
            write_config(invalid)
            run(admin_args(), password=PASSWORD + "\n")
            run(service_args())
            check(not (state / "business.db").exists(), "invalid config created database")
        config_file.write_text(json.dumps(config)[:-1] + ',"port":12345}', encoding="utf-8")
        run(admin_args(), password=PASSWORD + "\n")
        run(service_args())
        write_config(dict(config, state_dir=str(state) + "/../state"))
        run(admin_args(), password=PASSWORD + "\n")
        write_config()

        # Permission-sensitive input is refused, leaving no partial database.
        public_input = root / "public-input"
        public_input.write_text(PASSWORD + "\n", encoding="ascii")
        public_input.chmod(0o644)
        with public_input.open() as source:
            denied = subprocess.run(admin_args(), stdin=source, text=True, capture_output=True, timeout=10)
        check(denied.returncode > 0 and "password input rejected" in denied.stderr,
              "world-readable stdin accepted")
        check(PASSWORD not in denied.stdout + denied.stderr, "password leaked on refusal")
        public_input.unlink()
        fifo = root / "public-fifo"
        os.mkfifo(fifo, 0o600)
        fifo.chmod(0o644)
        fifo_fd = os.open(fifo, os.O_RDWR | os.O_NONBLOCK)
        try:
            os.write(fifo_fd, (PASSWORD + "\n").encode())
            denied = subprocess.run(admin_args(), stdin=fifo_fd, text=True, capture_output=True, timeout=10)
            check(denied.returncode > 0 and "password input rejected" in denied.stderr,
                  "public named FIFO accepted as restricted stdin")
        finally:
            os.close(fifo_fd)
            fifo.unlink()
        run(admin_args(), password="too-short\n")
        check(not (state / "business.db").exists(), "rejected password created database")

        # A missing main DB with an existing journal is recovery evidence, not empty state.
        for suffix in ("-wal", "-shm", "-journal"):
            orphan = state / ("business.db" + suffix)
            sentinel = ("orphan recovery evidence " + suffix).encode()
            orphan.write_bytes(sentinel)
            orphan.chmod(0o600)
            run(admin_args(), password=PASSWORD + "\n")
            check(not (state / "business.db").exists() and orphan.read_bytes() == sentinel,
                  "init changed orphan sidecar recovery evidence")
            orphan.unlink()

        terminal_input(admin_args(), config_file, config, root / "terminal-ok", False)
        terminal_input(admin_args(), config_file, config, root / "terminal-cancel", True)
        print("terminal input: hidden echo, success restoration and prompt SIGINT restoration passed")

        # The successful init itself must create its previously nonexistent directory.
        state = root / "initialized"
        config["state_dir"] = str(state)
        write_config()

        # Leave init waiting on its anonymous stdin pipe to observe argv and its lock.
        initializer = subprocess.Popen(admin_args(), stdin=subprocess.PIPE,
                                       stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            def init_holds_lock():
                if not (state / "instance.lock").exists():
                    return False
                with (state / "instance.lock").open("rb") as lock:
                    try:
                        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
                    except BlockingIOError:
                        return True
                    return False
            wait_for(init_holds_lock, initializer, "init did not acquire maintenance lock")
            command_line = Path("/proc/%s/cmdline" % initializer.pid).read_bytes()
            check(PASSWORD.encode() not in command_line, "password appears in argv")
            run(service_args())
            check(not (state / "business.db").exists(), "service opened DB during init")
            output, error = initializer.communicate(PASSWORD + "\n", timeout=20)
            check(initializer.returncode == 0, "init failed: " + output + error)
            check(PASSWORD not in output + error and "SIMULATOR_ONLY" in output,
                  "init leaked password or omitted simulation marker")
        finally:
            if initializer.poll() is None:
                initializer.kill()
                initializer.communicate()

        database = state / "business.db"
        users, tools = documents(database, "users"), documents(database, "tools")
        check(len(users) == 1 and users[0]["user"]["username"] == USERNAME
              and users[0]["user"]["role"] == "ADMIN", "explicit single administrator missing")
        check(users[0]["password_hash"].startswith("$argon2id$")
              and PASSWORD not in database.read_bytes().decode("latin1"), "password storage unsafe")
        check(len(tools) == 1 and tools[0]["slot_id"] == 1 and tools[0]["name"] == TOOL,
              "explicit single tool missing")
        before = database.read_bytes()
        run(admin_args(), password=PASSWORD + "\n")
        check(database.read_bytes() == before, "duplicate init modified DB")
        check(stat.S_IMODE(state.stat().st_mode) == 0o700, "state directory not private")
        check(stat.S_IMODE(database.stat().st_mode) == 0o600, "business database not private")

        processes, logs = [], []
        proxy = None

        def spawn(args, name):
            log = (root / name).open("w+")
            logs.append(log)
            process = subprocess.Popen(args, stdout=log, stderr=subprocess.STDOUT, text=True)
            processes.append(process)
            return process, log

        def stop(process):
            process.terminate()
            check(process.wait(timeout=15) == 0, "graceful shutdown failed")

        try:
            simulator, _ = spawn([SIMULATOR, "--uart", str(root / "uart.sock"), "--control",
                                  str(root / "control.sock"), "--journal", str(root / "sim.db")], "sim.log")
            wait_for(lambda: (root / "control.sock").exists(), simulator, "simulator startup")
            for path in [root / "control.sock", root / "uart.sock"]:
                check(stat.S_IMODE(path.stat().st_mode) == 0o600, "simulator socket permissions")
            proxy = WireProxy(root / "proxy.sock", root / "uart.sock")
            context = ssl.create_default_context(cafile=str(cert))
            for iteration in range(2):
                wire_start = len(proxy.snapshot())
                server, log = spawn(service_args(), "service-%s.log" % iteration)
                sock = state / "service.sock"
                wait_for(lambda: sock.exists(), server, "service startup")
                check(stat.S_ISSOCK(sock.stat().st_mode) and stat.S_IMODE(sock.stat().st_mode) == 0o600,
                      "IPC socket not private")
                status, data = request(config, context, "POST", "/sessions",
                                       {"username": USERNAME, "password": PASSWORD})
                check(status == 200 and bool(data["data"]["token"]), "real HTTPS login failed")
                status, account = request(config, context, "GET", "/sessions/current",
                                           token=data["data"]["token"])
                check(status == 200 and account["data"]["username"] == USERNAME,
                      "initialized administrator identity did not survive restart")
                status, device = request(config, context, "GET", "/device", token=data["data"]["token"])
                check(status == 200 and device["data"]["simulated"] is True,
                      "service device projection omitted simulation flag")
                check(device["data"]["mode"] == "RECONCILING",
                      "device became business-ready without explicit maintenance reconciliation")
                if iteration == 0:
                    verify_stalled_core(proxy, wire_start, server, config, context, database)
                else:
                    wait_for(lambda: len(observed_health(proxy, wire_start)) >= 2,
                             server, "restarted service wire health")
                # Service owns the same inode used by explicit init and maintenance.
                result = run(admin_args(), password=PASSWORD + "\n")
                check("lock busy" in result.stderr, "init did not respect running service lock")
                run(service_args())
                stop(server)
                check(not sock.exists(), "service socket remained after graceful exit")
                persisted = documents(database, "device_state")
                check(persisted and all(row["device"]["simulated"] is True for row in persisted),
                      "persistent device state lost trusted simulation provenance")
                log.flush()
                text = Path(log.name).read_text()
                check("startup RECONCILING" in text and "SIMULATOR_ONLY" in text,
                      "startup/restart omitted reconciliation or simulation marker")
                check("queue=64 http_workers=4 ws_buffer=16" in text, "resource defaults marker missing")

            with socket.socket() as occupied:
                occupied.bind(("127.0.0.1", port))
                occupied.listen()
                result = run(service_args())
                check("HTTPS start failed" in result.stderr, "port conflict lacks specific diagnostic")
            check(not (state / "service.sock").exists(), "failed startup leaked IPC socket")
            check(documents(database, "tools") == tools and documents(database, "users") == users,
                  "restart or port failure changed initialized identities")

            # Corrupt the disposable DB only after all successful restart checks.
            damaged = b"not a sqlite database: S1-06 preservation sentinel\n"
            database.write_bytes(damaged)
            run(service_args())
            run(admin_args(), password=PASSWORD + "\n")
            check(database.read_bytes() == damaged, "failure cleared or repaired corrupt DB")
            stop(simulator)
            check(not (root / "control.sock").exists() and not (root / "uart.sock").exists(),
                  "simulator sockets remained after exit")
        except BaseException:
            for log in logs:
                log.flush()
                print(Path(log.name).name + ":\n" +
                      Path(log.name).read_text().replace(PASSWORD, "[REDACTED]"), file=sys.stderr)
            raise
        finally:
            for process in reversed(processes):
                if process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=15)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait(timeout=5)
            for log in logs:
                log.flush()
                check(PASSWORD not in Path(log.name).read_text(), "password leaked in process log")
                log.close()
            if proxy:
                proxy.close()
        print("runtime lifecycle: initialization, trusted HTTPS, restart, exclusion, failures, privacy and cleanup passed")


if __name__ == "__main__":
    main()
