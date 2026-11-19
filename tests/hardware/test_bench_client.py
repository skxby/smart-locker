"""Client boundary regressions; these do not qualify real hardware."""
import io
import json
from pathlib import Path
import ssl
import sys
import unittest
from unittest.mock import Mock, patch

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
import bench_client as bench

ID = "10111213-1415-4617-9819-202122232425"
KEY = "20212223-2425-4627-9829-303132333435"
DEVICE = "000000000000000000000000"


class ClientBoundary(unittest.TestCase):
    def client(self, device=None):
        client = bench.Client("localhost", 8443, None, DEVICE, None, io.StringIO())
        data = device if device is not None else dict(device_id=DEVICE, simulated=False)
        client.https = Mock(return_value=(200, dict(data=data)))
        return client

    def test_tls_verification_and_hostname_required(self):
        client = self.client()
        self.assertTrue(client.context.check_hostname)
        self.assertEqual(client.context.verify_mode, ssl.CERT_REQUIRED)

    def test_no_unsafe_network_commands_or_path_injection(self):
        for words in (["unlock"], ["presence"], ["operation", "../../unlock"],
                      ["sensor", "door", "closed"], ["format"], ["shell", "reboot"]):
            with self.subTest(words=words), self.assertRaises((ValueError, AttributeError)):
                bench.plan(words)

    def test_versions_and_idempotency_not_regenerated(self):
        action = bench.plan(["borrow", ID, "18446744073709551615", KEY, "submit"])
        self.assertEqual(action[3]["tool_revision"], "18446744073709551615")
        self.assertEqual(action[4], KEY)
        for rev in ("-1", "01", "1.0", "18446744073709551616"):
            with self.subTest(rev=rev), self.assertRaises(ValueError):
                bench.plan(["confirm", ID, rev, KEY])

    def test_private_methods_never_map_to_https(self):
        self.assertEqual(bench.plan(["presence", ID, "1", KEY])[:2], ("ipc", "presence.submit"))
        self.assertEqual(bench.plan(["reconcile", ID, "1", "2", "3", KEY, "bench check"])[:2],
                         ("ipc", "maintenance.reconcile"))

    def test_mismatched_or_simulated_device_prevents_mutation(self):
        for device in (dict(device_id="0" * 24, simulated=False), dict(device_id=DEVICE, simulated=True)):
            client = self.client(device)
            with self.assertRaises(ValueError):
                client.execute(["borrow", ID, "1", KEY, "submit"])
            self.assertEqual(client.https.call_count, 1)
            self.assertEqual(client.https.call_args.args, ("GET", "/device"))

    def test_timeout_records_intent_without_retry(self):
        client = self.client()
        client.https.side_effect = [(200, dict(data=dict(device_id=DEVICE, simulated=False))), TimeoutError()]
        with self.assertRaises(TimeoutError):
            client.execute(["borrow", ID, "1", KEY, "submit"])
        self.assertEqual(client.https.call_count, 2)  # one identity read, exactly one POST
        events = [json.loads(x) for x in client.evidence.getvalue().splitlines()]
        self.assertEqual([x["event"] for x in events], ["intent", "outcome_unknown"])
        self.assertTrue(all(x["idempotency_key"] == KEY for x in events))

    def test_credentials_are_removed_from_nested_evidence(self):
        client = self.client()
        client.secrets = ["supersecret"]
        client.record(dict(body=dict(token="arbitrary", nested=[dict(password="pw")],
                                     message="echo supersecret")))
        text = client.evidence.getvalue()
        self.assertNotIn("supersecret", text)
        self.assertNotIn("arbitrary", text)
        self.assertNotIn('"pw"', text)

    def test_ipc_identity_precheck_sends_null_params(self):
        # Body-less IPC methods must send params:null; {} is rejected as
        # VALIDATION_ERROR and used to block every local action.
        client = self.client()
        client.local = Mock(return_value=(200, dict(data=dict(device_id=DEVICE, simulated=False))))
        client.check_device(local=True)
        self.assertIsNone(client.local.call_args.args[1])

    def test_missing_local_socket_never_uses_http_fallback(self):
        client = self.client()
        with self.assertRaises(ValueError):
            client.execute(["presence", ID, "1", KEY])
        client.https.assert_not_called()

    def test_user_provisioning_returns_create_user_route(self):
        root = Path(__file__).resolve().parents[2]
        source = json.loads((root / "contracts/contract-source.json").read_text(encoding="utf-8"))
        route = next(r for r in source["public_routes"] if r["operation_id"] == "createUser")
        transport, method, path, body, key = bench.plan(["user", "bench01", "USER", KEY, "Bench One"])
        self.assertEqual((transport, method, path, key), ("https", "POST", "/users", KEY))
        schema = source["schemas"][route["input_schema"]]
        # The password is added by the hidden prompt, never by the command grammar.
        self.assertEqual(set(body) | {"password"}, set(schema["required"]))

    def test_user_password_comes_from_hidden_prompt_only(self):
        client = self.client()
        client.https.side_effect = [(200, dict(data=dict(device_id=DEVICE, simulated=False))),
                                    (201, dict(data=dict(username="bench01")))]
        secret = "benchpassword01"
        with patch.object(bench.getpass, "getpass", return_value=secret):
            client.execute(["user", "bench01", "USER", KEY, "Bench One"])
        sent = client.https.call_args_list[1].args
        self.assertEqual(sent[0], "POST")
        self.assertEqual(sent[2]["password"], secret)
        text = client.evidence.getvalue()
        self.assertNotIn(secret, text)
        self.assertIn("[REDACTED]", text)
        for words in (["user", "bench01", "USER", KEY, "Bench One", secret],
                      ["user", "bench01", "NORMAL", KEY, "Bench One"]):
            with self.subTest(words=words), self.assertRaises(ValueError):
                bench.plan(words)

    def test_short_password_is_refused_before_sending(self):
        client = self.client()
        client.https.side_effect = [(200, dict(data=dict(device_id=DEVICE, simulated=False)))]
        with patch.object(bench.getpass, "getpass", return_value="short"):
            with self.assertRaises(ValueError):
                client.execute(["user", "bench01", "USER", KEY, "Bench One"])
        self.assertEqual(client.https.call_count, 1)  # identity read only, no POST
        self.assertEqual(client.evidence.getvalue(), "")

    def test_commands_match_machine_contract_routes_and_schemas(self):
        root = Path(__file__).resolve().parents[2]
        source = json.loads((root / "contracts/contract-source.json").read_text(encoding="utf-8"))
        cases = [("createOperation", ["borrow", ID, "1", KEY, "submit"]),
                 ("confirmOperation", ["confirm", ID, "2", KEY]),
                 ("cancelOperation", ["cancel", ID, "2", KEY, "operator cancel"]),
                 ("resolveOperation", ["resolve", ID, "2", "3", "NO_CHANGE", KEY, "bench review"])]
        for name, words in cases:
            with self.subTest(name=name):
                route = next(r for r in source["public_routes"] if r["operation_id"] == name)
                transport, method, path, body, _ = bench.plan(words)
                self.assertEqual(transport, "https")
                self.assertEqual(method.lower(), route["method"])
                self.assertEqual(path, route["path"].replace("{operation_id}", ID))
                schema = source["schemas"][route["input_schema"]]
                self.assertEqual(set(body), set(schema["required"]))


if __name__ == "__main__":
    unittest.main()
