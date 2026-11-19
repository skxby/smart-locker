"""S2-07 operator client. Existing HTTPS API plus two local-only IPC methods.

No sensor injection, GPIO, UART, database writes, TLS bypass or request retries.
Use on the RK for presence/reconcile; run --help for the interactive entry.
"""
import argparse
import getpass
import http.client
import json
import os
from pathlib import Path
import re
import shlex
import socket
import ssl
import stat
import struct
import sys
import uuid
from datetime import datetime, timezone

sys.dont_write_bytecode = True
MAX_REPLY = 65535


def uuid_value(value):
    parsed = uuid.UUID(value)
    if str(parsed) != value or parsed.int == 0:
        raise ValueError("expected a nonzero canonical UUID")
    return value


def revision(value):
    if not re.fullmatch(r"0|[1-9][0-9]*", value) or int(value) > 2**64 - 1:
        raise ValueError("expected decimal uint64 revision")
    return value


def scrub(value, secrets=()):
    if isinstance(value, dict):
        return {k: ("[REDACTED]" if any(x in k.lower() for x in
                ("password", "token", "authorization", "private_key")) else scrub(v, secrets))
                for k, v in value.items()}
    if isinstance(value, list):
        return [scrub(x, secrets) for x in value]
    if isinstance(value, str):
        for secret in secrets:
            if secret:
                value = value.replace(secret, "[REDACTED]")
    return value


def plan(words):
    """Fixed command grammar; caller supplies revisions and idempotency keys.

    Never refresh a revision and silently resubmit a failed business action.
    """
    if words in (["snapshot"], ["device"], ["tools"], ["users"]):
        return "https", "GET", "/" + words[0], None, None
    if len(words) == 5 and words[0] == "user":
        # Password is never part of the command grammar: it is prompted for at
        # send time so it cannot land in argv, shell history or this evidence.
        if words[2] not in ("USER", "ADMIN"):
            raise ValueError("role must be USER or ADMIN")
        if not re.fullmatch(r"[a-zA-Z0-9_.-]{3,32}", words[1]):
            raise ValueError("username must be 3..32 of [A-Za-z0-9_.-]")
        if not 1 <= len(words[4]) <= 64:
            raise ValueError("display name length must be 1..64")
        return ("https", "POST", "/users",
                dict(username=words[1], display_name=words[4], role=words[2]),
                uuid_value(words[3]))
    if len(words) == 2 and words[0] == "operation":
        return "https", "GET", "/operations/" + uuid_value(words[1]), None, None
    if len(words) == 5 and words[0] in ("borrow", "return"):
        body = dict(tool_id=uuid_value(words[1]), tool_revision=revision(words[2]),
                    kind=words[0].upper())
        # The final literal prevents accidentally treating an inspection as a create.
        if words[4] != "submit":
            raise ValueError("create command must end with submit")
        return "https", "POST", "/operations", body, uuid_value(words[3])
    if len(words) == 4 and words[0] in ("presence", "confirm"):
        operation, rev, key = uuid_value(words[1]), revision(words[2]), uuid_value(words[3])
        body = dict(operation_revision=rev)
        if words[0] == "presence":
            body["operation_id"] = operation
            return "ipc", "presence.submit", None, body, key
        return "https", "POST", "/operations/" + operation + "/confirm", body, key
    if len(words) == 5 and words[0] == "cancel":
        body = dict(operation_revision=revision(words[2]), reason=words[4])
        if not 1 <= len(words[4]) <= 256:
            raise ValueError("reason length must be 1..256")
        return "https", "POST", "/operations/" + uuid_value(words[1]) + "/cancel", body, uuid_value(words[3])
    if len(words) == 7 and words[0] == "reconcile":
        if not 5 <= len(words[6]) <= 256:
            raise ValueError("reason length must be 5..256")
        body = dict(journal_epoch=uuid_value(words[1]), boot_id=revision(words[2]),
                    physical_revision=revision(words[3]), snapshot_revision=revision(words[4]),
                    reason=words[6])
        return "ipc", "maintenance.reconcile", None, body, uuid_value(words[5])
    if len(words) == 7 and words[0] == "resolve":
        if words[4] not in ("COMPLETE_BORROW", "COMPLETE_RETURN", "NO_CHANGE") or not 5 <= len(words[6]) <= 256:
            raise ValueError("invalid resolution/reason")
        body = dict(operation_revision=revision(words[2]), physical_revision=revision(words[3]),
                    resolution=words[4], reason=words[6])
        return "https", "POST", "/operations/" + uuid_value(words[1]) + "/resolve", body, uuid_value(words[5])
    raise ValueError("unknown command/arguments; enter help")


HELP = """snapshot | device | tools | users | operation OPERATION_UUID
user USERNAME USER|ADMIN IDEMPOTENCY_UUID "Display Name"  (admin; password prompted, hidden)
borrow TOOL_UUID TOOL_REV IDEMPOTENCY_UUID submit
return TOOL_UUID TOOL_REV IDEMPOTENCY_UUID submit
presence OPERATION_UUID OP_REV IDEMPOTENCY_UUID  (local IPC; may cause a pulse)
confirm OPERATION_UUID OP_REV IDEMPOTENCY_UUID
cancel OPERATION_UUID OP_REV IDEMPOTENCY_UUID \"reason\"
reconcile EPOCH BOOT PHYSICAL_REV SNAPSHOT_REV IDEMPOTENCY_UUID \"reason\" (local admin)
resolve OPERATION_UUID OP_REV PHYSICAL_REV COMPLETE_BORROW|COMPLETE_RETURN|NO_CHANGE IDEMPOTENCY_UUID \"reason\"
key  (generate an idempotency UUID; retain it for the same intent)
help | quit
Read current revisions first. No automatic retries, sensor simulation or fault clearing.
"""


class Client:
    def __init__(self, host, port, ca, device_id, ipc, evidence):
        self.host, self.port = host, port
        self.context = ssl.create_default_context(cafile=str(ca) if ca is not None else None)
        self.device_id, self.ipc, self.evidence = device_id, ipc, evidence
        self.token = None
        self.secrets = []

    def record(self, event):
        event = scrub(dict(time=datetime.now(timezone.utc).isoformat(), **event), self.secrets)
        self.evidence.write(json.dumps(event, ensure_ascii=False) + "\n")
        self.evidence.flush()

    def https(self, method, path, body=None, key=None):
        headers = {"Content-Type": "application/json"}
        if self.token:
            headers["Authorization"] = "Bearer " + self.token
        if key:
            headers["Idempotency-Key"] = key
        conn = http.client.HTTPSConnection(self.host, self.port, context=self.context, timeout=8)
        try:
            conn.request(method, "/api/v1" + path, json.dumps(body) if body is not None else None, headers)
            response = conn.getresponse()
            raw = response.read(MAX_REPLY + 1)
            if len(raw) > MAX_REPLY:
                raise ValueError("response too large")
            return response.status, json.loads(raw) if raw else None
        finally:
            conn.close()

    def local(self, method, body, key=None):
        if self.ipc is None or not hasattr(socket, "SO_PEERCRED"):
            raise ValueError("local IPC requires Linux/RK and --socket")
        path = Path(self.ipc)
        if not path.is_absolute() or str(path.resolve()) != str(path):
            raise ValueError("socket path must be canonical and absolute")
        before = path.lstat()
        if not stat.S_ISSOCK(before.st_mode) or before.st_uid != os.geteuid() or before.st_mode & 0o077:
            raise ValueError("socket must be owned by current UID with mode 0600")
        packet = dict(v=1, request_id=str(uuid.uuid4()), method=method,
                      session_token=self.token, params=body, idempotency_key=key,
                      path_ids={}, query={})
        encoded = json.dumps(packet).encode()
        if len(encoded) > 8192:
            raise ValueError("IPC request too large")
        with socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET) as conn:
            conn.settimeout(8)
            conn.connect(str(path))
            _, uid, _ = struct.unpack("3i", conn.getsockopt(socket.SOL_SOCKET, socket.SO_PEERCRED, 12))
            after = path.lstat()
            if uid != os.geteuid() or (before.st_dev, before.st_ino) != (after.st_dev, after.st_ino):
                raise ValueError("IPC peer/path changed")
            conn.sendall(encoded)
            raw, _, flags, _ = conn.recvmsg(MAX_REPLY)
            if flags & socket.MSG_TRUNC:
                raise ValueError("IPC response truncated")
            result = json.loads(raw)
        if result.get("request_id") != packet["request_id"]:
            raise ValueError("IPC response correlation mismatch")
        return result["status"], result["body"]

    def check_device(self, local=False):
        # 无 body 的 IPC 方法要求 params 为 JSON null；发 {} 会被服务端按
        # VALIDATION_ERROR 拒绝（ipc.c: (b->body ? params->type != OBJECT
        # : params->type != NULL)），而这个预检失败会挡住所有本机动作。
        code, response = self.local("getDevice", None) if local else self.https("GET", "/device")
        data = (response or {}).get("data", {})
        if code != 200 or data.get("device_id") != self.device_id or data.get("simulated") is not False:
            # Quietly refusing to act was right; hiding *why* was not.
            raise ValueError("device identity/backend mismatch; no action sent "
                             "(status=%s body=%s)"
                             % (code, json.dumps(response, ensure_ascii=False)[:200]))

    def execute(self, words):
        transport, method, path, body, key = plan(words)
        self.check_device(local=transport == "ipc")
        if transport == "https" and method == "POST" and path == "/users":
            secret = getpass.getpass("New user password: ")
            if not 12 <= len(secret) <= 128:
                raise ValueError("password must be 12..128 bytes; nothing was sent")
            self.secrets.append(secret)
            body = dict(body, password=secret)
        # Intent precedes sending. A timeout is unknown, never an automatic retry.
        self.record(dict(event="intent", transport=transport, method=method,
                         path=path, body=body, idempotency_key=key))
        try:
            code, response = (self.local(method, body, key) if transport == "ipc" else
                              self.https(method, path, body, key))
        except (OSError, ValueError, http.client.HTTPException):
            self.record(dict(event="outcome_unknown", idempotency_key=key))
            raise
        self.record(dict(event="response", status=code, body=response, idempotency_key=key))
        return code, scrub(response, self.secrets)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", required=True)
    parser.add_argument("--port", type=int, default=8443)
    parser.add_argument("--ca", type=Path, required=True)
    parser.add_argument("--device-id", required=True)
    parser.add_argument("--user", required=True)
    parser.add_argument("--socket", type=Path)
    parser.add_argument("--evidence", type=Path, required=True, help="new private JSONL; never overwrite")
    args = parser.parse_args()
    if not re.fullmatch(r"[0-9a-f]{24}", args.device_id):
        parser.error("device-id must be the expected 24 lowercase hex digits")
    if not sys.stdin.isatty():
        parser.error("interactive terminal required; passwords are not accepted in argv/stdin files")
    fd = os.open(args.evidence, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, "w", encoding="utf-8") as evidence:
        client = Client(args.host, args.port, args.ca, args.device_id, args.socket, evidence)
        password = getpass.getpass("Password: ")
        client.secrets.append(password)
        code, response = client.https("POST", "/sessions", dict(username=args.user, password=password))
        if code != 200:
            raise ValueError("login rejected; no retry")
        client.token = response["data"]["token"]
        client.secrets.append(client.token)
        client.check_device()
        client.record(dict(event="connected", device_id=args.device_id, contract_version="0.2.1"))
        print(HELP)
        while True:
            try:
                words = shlex.split(input("bench> "))
                if not words:
                    continue
                if words == ["quit"]:
                    break
                if words == ["help"]:
                    print(HELP)
                elif words == ["key"]:
                    print(uuid.uuid4())
                else:
                    code, response = client.execute(words)
                    print(json.dumps(dict(status=code, body=response), ensure_ascii=False))
            except (EOFError, KeyboardInterrupt):
                break
            except (ValueError, OSError, http.client.HTTPException) as exc:
                # The one-line summary hid the actual cause (socket mode, IPC
                # correlation, local refusal); the operator needs the detail to
                # decide whether anything was sent at all.
                print("Request failed or outcome unknown; no retry. %s: %s"
                      % (type(exc).__name__, exc), file=sys.stderr)


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, http.client.HTTPException):
        sys.exit("Connection/login failed; no request retry. Check identity, TLS, permissions and evidence.")
