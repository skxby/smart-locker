"""S1-07 real-process path: TLS, local IPC, UART socket, and two SQLite files."""
import http.client
import json
import os
from pathlib import Path
import socket
import sqlite3
import ssl
import subprocess
import sys
import tempfile
import time
import uuid


SERVICE, ADMIN, SIM, OPENSSL = sys.argv[1:]
PASSWORD = "stage-gate-private-password"


def ident():
    return str(uuid.uuid4())


def until(predicate, label, seconds=12):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        result = predicate()
        if result:
            return result
        time.sleep(0.05)
    raise AssertionError(label)


def control(path, command, status=False):
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as client:
        client.settimeout(3)
        client.connect(str(path))
        client.sendall((command + "\n").encode())
        result = client.recv(4096).decode()
    if status:
        return dict(line.split("=", 1) for line in result.strip().splitlines())
    assert result.startswith("ok"), (command, result)
    return result


def documents(database, collection):
    with sqlite3.connect("file:" + str(database) + "?mode=ro", uri=True) as connection:
        return [json.loads(value) for (value,) in connection.execute(
            "SELECT value_json FROM documents WHERE collection=?", (collection,))]


def main():
    with tempfile.TemporaryDirectory(prefix="locker-stage-gate-") as directory:
        root = Path(directory).resolve()
        state = root / "state"
        uart, ctl, journal = root / "uart.sock", root / "control.sock", root / "sim.db"
        cert, key = root / "cert.pem", root / "key.pem"
        subprocess.run([OPENSSL, "req", "-x509", "-newkey", "rsa:2048", "-nodes",
                        "-config", "/dev/null", "-keyout", str(key), "-out", str(cert),
                        "-days", "1", "-subj", "/CN=localhost", "-addext",
                        "subjectAltName=DNS:localhost,IP:127.0.0.1"], check=True,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        key.chmod(0o600)
        with socket.socket() as probe:
            probe.bind(("127.0.0.1", 0))
            port = probe.getsockname()[1]
        config = dict(mode="SIMULATOR_ONLY", state_dir=str(state), sim_socket=str(uart),
                      device_id="404142434445464748494a4b", certificate=str(cert),
                      private_key=str(key), bind_address="127.0.0.1", port=port,
                      ui_uid=os.geteuid(), admin_uid=os.geteuid(), cabinet_name="Stage gate")
        config_path = root / "config.json"
        config_path.write_text(json.dumps(config))
        init = subprocess.run([ADMIN, "init", "--config", str(config_path),
                               "--username", "stageadmin", "--tool-name", "Single tool",
                               "--password-stdin"], input=PASSWORD + "\n", text=True,
                              capture_output=True, timeout=15)
        assert init.returncode == 0, init.stderr
        processes = []
        logs = []

        def spawn(args, label):
            log = (root / (label + ".log")).open("w+")
            logs.append(log)
            process = subprocess.Popen(args, stdout=log, stderr=subprocess.STDOUT)
            processes.append(process)
            return process

        context = ssl.create_default_context(cafile=str(cert))

        def request(method, path, token=None, body=None, idem=None, trust=context):
            headers = {"Content-Type": "application/json"}
            if token:
                headers["Authorization"] = "Bearer " + token
            if idem:
                headers["Idempotency-Key"] = idem
            client = http.client.HTTPSConnection("127.0.0.1", port, context=trust, timeout=5)
            try:
                client.request(method, "/api/v1" + path,
                               json.dumps(body) if body is not None else None, headers)
                response = client.getresponse()
                data = response.read()
                return response.status, json.loads(data) if data else None
            finally:
                client.close()

        def rpc(method, token, params, idem=None):
            packet = dict(v=1, request_id=ident(), method=method, session_token=token,
                          params=params, idempotency_key=idem, path_ids={}, query={})
            with socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET) as client:
                client.settimeout(5)
                client.connect(str(state / "service.sock"))
                client.sendall(json.dumps(packet).encode())
                result = json.loads(client.recv(65536))
            assert result["request_id"] == packet["request_id"], result
            return result["status"], result["body"]

        def reconcile(admin_token):
            def attempt():
                code, snapshot = request("GET", "/snapshot", admin_token)
                assert code == 200, snapshot
                physical = snapshot["data"]["device"]["physical"]
                if not physical or not physical.get("journal_epoch"):
                    return False
                params = {key: physical[key] for key in
                          ("journal_epoch", "boot_id", "physical_revision")}
                params["snapshot_revision"] = snapshot["snapshot_revision"]
                params["reason"] = "stage gate physical reconciliation"
                code, _ = rpc("maintenance.reconcile", admin_token, params, ident())
                return code == 200 and control(ctl, "status", True).get("device_mode") == "1"
            until(attempt, "maintenance reconciliation/READY")

        try:
            sim = spawn([SIM, "--uart", str(uart), "--control", str(ctl),
                         "--journal", str(journal)], "sim")
            until(lambda: ctl.exists() and sim.poll() is None, "simulator socket")
            control(ctl, "door closed")
            control(ctl, "item present")
            service = spawn([SERVICE, "--config", str(config_path)], "service")
            until(lambda: (state / "service.sock").exists() and service.poll() is None,
                  "service socket")
            code, login = request("POST", "/sessions", body={"username": "stageadmin",
                                                            "password": PASSWORD})
            assert code == 200, (code, login)
            admin_token = login["data"]["token"]
            time.sleep(1.1)  # The production login endpoint limits attempts.
            wrong_login = request("POST", "/sessions", body={"username": "stageadmin",
                                                             "password": "wrong-password"})
            assert wrong_login[0] == 401, wrong_login
            assert request("GET", "/snapshot")[0] == 401
            try:
                request("GET", "/snapshot", admin_token,
                        trust=ssl.create_default_context())
                raise AssertionError("untrusted TLS certificate accepted")
            except ssl.SSLCertVerificationError:
                pass
            assert request("POST", "/presence", admin_token, {}, ident())[0] == 404
            assert request("POST", "/unlock", admin_token, {}, ident())[0] == 404
            assert rpc("presence.submit", None, {}, ident())[0] in (400, 401, 403)
            reconcile(admin_token)
            assert request("GET", "/device", admin_token)[1]["data"]["simulated"] is True
            code, tool_response = request("GET", "/tools", admin_token)
            assert code == 200, tool_response
            tool = tool_response["data"]["items"][0]
            code, created_user = request("POST", "/users", admin_token,
                                         {"username": "stageuser", "display_name": "Stage user",
                                          "password": PASSWORD, "role": "USER"}, ident())
            assert code == 201, (code, created_user)
            time.sleep(1.1)
            code, user_login = request("POST", "/sessions", body={"username": "stageuser",
                                                                 "password": PASSWORD})
            assert code == 200, (code, user_login)
            user_token = user_login["data"]["token"]
            intent = {"tool_id": tool["id"], "kind": "BORROW",
                      "tool_revision": tool["tool_revision"]}
            idem = ident()
            code, operation_response = request("POST", "/operations", user_token, intent, idem)
            assert code == 201, (code, operation_response)
            assert request("POST", "/operations", user_token, intent, idem) == (code, operation_response)
            assert request("POST", "/operations", user_token,
                           dict(intent, tool_revision="999"), idem)[0] == 409
            operation = operation_response["data"]["id"]
            code, contender = request("POST", "/users", admin_token,
                                      {"username": "contender", "display_name": "Contender",
                                       "password": PASSWORD, "role": "USER"}, ident())
            assert code == 201, (code, contender)
            time.sleep(1.1)
            code, contender_login = request("POST", "/sessions",
                                            body={"username": "contender", "password": PASSWORD})
            assert code == 200, (code, contender_login)
            contender_token = contender_login["data"]["token"]
            assert request("POST", "/operations", contender_token, intent, ident())[0] == 409
            assert request("POST", "/operations/" + operation + "/confirm", user_token,
                           {"operation_revision": operation_response["data"]["operation_revision"]},
                           ident())[0] == 409
            presence = {"operation_id": operation,
                        "operation_revision": operation_response["data"]["operation_revision"]}
            assert rpc("presence.submit", user_token, presence, ident())[0] == 200

            def operation_in(target):
                code, current = request("GET", "/operations/" + operation, user_token)
                assert code == 200, current
                return current["data"] if current["data"]["state"] == target else False

            until(lambda: operation_in("ACCESS_ACTIVE"), "borrow access active")
            until(lambda: int(control(ctl, "status", True)["action_watermark"]) == 1,
                  "exactly one simulator action")
            control(ctl, "door open")
            control(ctl, "item absent")
            control(ctl, "door closed")
            current = until(lambda: operation_in("WAIT_CONFIRM"), "borrow wait confirm")
            confirm = {"operation_revision": current["operation_revision"]}
            confirm_key = ident()
            code, done = request("POST", "/operations/" + operation + "/confirm",
                                 user_token, confirm, confirm_key)
            assert code == 200 and done["data"]["state"] == "COMPLETED", (code, done)
            assert request("POST", "/operations/" + operation + "/confirm",
                           user_token, confirm, confirm_key) == (code, done)
            assert int(control(ctl, "status", True)["action_watermark"]) == 1
            loans = documents(state / "business.db", "loans")
            assert len(loans) == 1 and loans[0]["closed_at"] is None, loans
            until(lambda: control(ctl, "status", True)["has_active"] == "0",
                  "borrow context released")
            until(lambda: request("GET", "/device", admin_token)[1]["data"]["mode"] == "READY",
                  "device ready after borrow")
            code, identity = request("GET", "/sessions/current", user_token)
            assert code == 200, (code, identity)
            control(ctl, "card 11223344")

            def card_observation():
                code, observations = request("GET", "/card-observations", admin_token)
                assert code == 200, observations
                items = observations["data"]["items"]
                return items[0] if items and not items[0]["consumed"] else False

            try:
                observed = until(card_observation, "current card observation")
            except AssertionError:
                print("card diagnostic", request("GET", "/card-observations", admin_token),
                      request("GET", "/device", admin_token), control(ctl, "status", True),
                      file=sys.stderr)
                raise
            code, bound = request("PUT", "/users/" + identity["data"]["id"] + "/card",
                                  admin_token, {"user_revision": "1",
                                                "observation_id": observed["id"]}, ident())
            assert code == 200, (code, bound)
            code, refreshed_tools = request("GET", "/tools", user_token)
            assert code == 200, refreshed_tools
            tool = refreshed_tools["data"]["items"][0]
            intent = {"tool_id": tool["id"], "kind": "RETURN",
                      "tool_revision": tool["tool_revision"]}
            code, response = request("POST", "/operations", user_token, intent, ident())
            assert code == 201, (code, response)
            operation = response["data"]["id"]
            until(lambda: operation_in("REQUESTED"), "return requested")
            time.sleep(0.3)
            control(ctl, "card 55667788")
            time.sleep(0.3)
            assert operation_in("REQUESTED"), "unbound card opened return operation"
            assert int(control(ctl, "status", True)["action_watermark"]) == 1
            control(ctl, "drop_execute_ack on")
            control(ctl, "card 11223344")
            try:
                until(lambda: operation_in("ACCESS_ACTIVE"), "return access active")
            except AssertionError:
                print("return diagnostic", request("GET", "/operations/" + operation, user_token),
                      request("GET", "/device", admin_token), control(ctl, "status", True),
                      file=sys.stderr)
                raise
            until(lambda: int(control(ctl, "status", True)["action_watermark"]) == 2,
                  "exactly two simulator actions")
            control(ctl, "drop_execute_ack off")
            control(ctl, "door open")
            control(ctl, "item present")
            control(ctl, "door closed")
            current = until(lambda: operation_in("WAIT_CONFIRM"), "return wait confirm")
            code, done = request("POST", "/operations/" + operation + "/confirm",
                                 user_token, {"operation_revision": current["operation_revision"]},
                                 ident())
            assert code == 200 and done["data"]["state"] == "COMPLETED", (code, done)
            loans = documents(state / "business.db", "loans")
            assert len(loans) == 1 and loans[0]["closed_at"] is not None, loans
            service.terminate()
            assert service.wait(timeout=15) == 0
            assert not (state / "service.sock").exists()
            service = spawn([SERVICE, "--config", str(config_path)], "service-restart")
            until(lambda: (state / "service.sock").exists() and service.poll() is None,
                  "restarted service socket")
            time.sleep(1.1)
            code, fresh_login = request("POST", "/sessions", body={"username": "stageadmin",
                                                                  "password": PASSWORD})
            assert code == 200, (code, fresh_login)
            fresh_token = fresh_login["data"]["token"]
            assert request("GET", "/device", fresh_token)[1]["data"]["mode"] == "RECONCILING"
            with sqlite3.connect("file:" + str(journal) + "?mode=ro", uri=True) as connection:
                assert connection.execute("SELECT count(*) FROM sim_commands_v2").fetchone()[0] == 2
            assert int(control(ctl, "status", True)["action_watermark"]) == 0
            loans = documents(state / "business.db", "loans")
            assert len(loans) == 1 and loans[0]["closed_at"] is not None, loans
            assert documents(state / "business.db", "device_state")
            assert journal.exists() and journal != state / "business.db"
            reconcile(fresh_token)
            prior_session = control(ctl, "status", True)["session"]
            control(ctl, "drop_link")
            service.terminate()
            assert service.wait(timeout=15) == 0
            service = spawn([SERVICE, "--config", str(config_path)], "service-link-restart")
            until(lambda: (state / "service.sock").exists() and service.poll() is None,
                  "service socket after link loss")
            time.sleep(1.1)
            code, link_login = request("POST", "/sessions", body={"username": "stageadmin",
                                                                  "password": PASSWORD})
            assert code == 200, (code, link_login)
            link_token = link_login["data"]["token"]
            assert control(ctl, "status", True)["session"] != prior_session
            assert request("GET", "/device", link_token)[1]["data"]["mode"] == "RECONCILING"
            reconcile(link_token)
            print("stage gate real-process borrow/return/restart/reconnect: TLS, auth, IPC, UART, reconciliation, idempotency, SQLite passed")
        except BaseException:
            for log in logs:
                log.flush()
                print(Path(log.name).name + ":\n" + Path(log.name).read_text(), file=sys.stderr)
            raise
        finally:
            for process in reversed(processes):
                if process.poll() is None:
                    process.terminate()
                    try:
                        assert process.wait(timeout=15) == 0
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait(timeout=5)
            for log in logs:
                log.close()


if __name__ == "__main__":
    main()
