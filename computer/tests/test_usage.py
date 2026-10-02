"""Unit tests for provider selection and credential snapshot handling."""

from __future__ import annotations

import importlib.util
from argparse import Namespace
from io import BytesIO, StringIO
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch


MODULE = Path(__file__).parents[1] / "usage.py"
sys.path.insert(0, str(MODULE.parent))
SPEC = importlib.util.spec_from_file_location("usage", MODULE)
assert SPEC and SPEC.loader
usage = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = usage
SPEC.loader.exec_module(usage)


class ProviderTests(unittest.TestCase):
    def test_selected_providers_defaults_and_rejects_invalid_values(self) -> None:
        self.assertEqual(usage.selected_providers("codex,cursor"), ["codex", "cursor"])
        for value in ("", "codex,codex", "unknown"):
            with self.assertRaises(Exception):
                usage.selected_providers(value)

    def test_load_credentials_keeps_healthy_provider(self) -> None:
        args = Namespace(providers=["codex", "cursor"], codex_auth=Path("x"), cursor_db=Path("y"))
        with patch.object(usage.PROVIDERS["codex"], "read_credentials", return_value={"access_token": "token", "account_id": "account"}), patch.object(usage.PROVIDERS["cursor"], "read_credentials", side_effect=usage.UsageError("secret path")):
            credentials, errors = usage.load_credentials(args)
        self.assertEqual(set(credentials), {"codex"})
        self.assertEqual(errors, {"cursor": "credentials unavailable"})

    def test_collect_usage_reports_sanitized_partial_failure(self) -> None:
        args = Namespace(providers=["codex", "cursor"], codex_auth=Path("x"), cursor_db=Path("y"))
        with patch.object(usage.PROVIDERS["codex"], "read_credentials", return_value={"access_token": "token"}), patch.object(usage.PROVIDERS["cursor"], "read_credentials", return_value={"cookie": "cookie"}), patch.object(usage.PROVIDERS["codex"], "fetch_usage", return_value={"plan_type": "plus"}), patch.object(usage.PROVIDERS["cursor"], "fetch_usage", side_effect=usage.UsageError("request failed with HTTP 401")):
            result = usage.collect_usage(args)
        self.assertEqual(result["codex"], {"plan_type": "plus"})
        self.assertEqual(result["errors"], {"cursor": "request failed with HTTP 401"})
        self.assertNotIn("cookie", str(result))

    def test_snapshot_has_only_supplied_provider_and_no_errors(self) -> None:
        payload = usage.credential_payload({"codex": {"access_token": "a", "account_id": "id"}})
        self.assertEqual(payload["version"], 1)
        self.assertEqual(payload["codex"]["access_token"], "a")
        self.assertNotIn("cursor", payload)
        self.assertNotIn("errors", payload)

    def test_http_error_does_not_include_response_body(self) -> None:
        from urllib.error import HTTPError

        error = HTTPError("https://provider.invalid", 403, "denied", {}, BytesIO(b"token=do-not-leak"))
        with patch.object(usage, "open_request", side_effect=error), self.assertRaisesRegex(usage.UsageError, "HTTP 403") as raised:
            usage.request_json("https://provider.invalid")
        self.assertNotIn("do-not-leak", str(raised.exception))

    def test_malformed_codex_auth_is_usage_error(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "auth.json"
            path.write_text(json.dumps({"tokens": {"access_token": 1, "account_id": "id"}}))
            with self.assertRaisesRegex(usage.UsageError, "malformed"):
                usage.read_codex_credentials(path)

    def test_request_json_rejects_non_object_and_large_response(self) -> None:
        class Response:
            def __init__(self, body: bytes) -> None:
                self.body = body
            def __enter__(self):
                return self
            def __exit__(self, *args):
                return False
            def read(self, _size: int) -> bytes:
                return self.body
        with patch.object(usage, "open_request", return_value=Response(b"[]")), self.assertRaisesRegex(usage.UsageError, "JSON object"):
            usage.request_json("https://provider.invalid")
        with patch.object(usage, "open_request", return_value=Response(b"x" * (usage.MAX_RESPONSE_BYTES + 1))), self.assertRaisesRegex(usage.UsageError, "size limit"):
            usage.request_json("https://provider.invalid")

    def test_no_redirect_handler_is_used(self) -> None:
        opener = type("Opener", (), {"open": lambda self, request, timeout: None})()
        with patch("urllib.request.build_opener", return_value=opener) as build:
            usage.open_request(usage.urllib.request.Request("https://provider.invalid"), 1)
        self.assertIsInstance(build.call_args.args[0], usage.NoRedirect)

    def test_positive_interval(self) -> None:
        self.assertEqual(usage.positive_interval("0.5"), 0.5)
        for value in ("0", "-1", "301", "nan", "inf", "abc"):
            with self.assertRaises(Exception):
                usage.positive_interval(value)

    def test_claude_keychain_credentials_and_usage_headers(self) -> None:
        credentials = usage.read_claude_credentials(
            None,
            keychain_reader=lambda _: '{"claudeAiOauth":{"accessToken":"claude-token","expiresAt":12}}',
        )
        self.assertEqual(credentials["access_token"], "claude-token")
        with patch.object(usage, "request_json", return_value={"five_hour": {"utilization": 12, "resets_at": "soon"}, "seven_day": {"utilization": 34, "resets_at": "later"}}) as request:
            result = usage.fetch_claude_usage(credentials)
        self.assertEqual(result["five_hour"]["utilization"], 12)
        headers = request.call_args.kwargs["headers"]
        self.assertEqual(headers["anthropic-beta"], "oauth-2025-04-20")
        self.assertEqual(headers["Authorization"], "Bearer claude-token")

    def test_claude_malformed_auth_is_sanitized(self) -> None:
        with self.assertRaisesRegex(usage.UsageError, "unavailable") as raised:
            usage.read_claude_credentials(None, keychain_reader=lambda _: "not-json")
        self.assertNotIn("not-json", str(raised.exception))

    def test_push_parses_transport_flags_and_uses_mapping_payload(self) -> None:
        args = usage.build_parser().parse_args([
            "push", "--providers", "claude", "--url", "https://device.local/api/credentials",
            "--pin", "1234", "--device-token-env", "ALT", "--allow-insecure-http",
        ])
        with patch.object(usage, "load_credentials", return_value=({"claude": {"access_token": "secret"}}, {})), patch.object(usage, "push_http") as pushed:
            usage.run_push(args)
        payload = pushed.call_args.args[1]
        self.assertEqual(payload["claude"]["access_token"], "secret")
        self.assertNotIn("codex", payload)
        self.assertEqual(pushed.call_args.args[2], "1234")

    def test_push_warns_about_unavailable_selected_provider(self) -> None:
        args = Namespace(pin="1234", device_token_env=None, serial=None, url="https://device", baud=115200, allow_insecure_http=False)
        stderr = StringIO()
        with patch.object(usage, "load_credentials", return_value=({"codex": {"access_token": "token", "account_id": "id"}}, {"claude": "credentials unavailable"})), patch.object(usage, "push_http"), patch.object(sys, "stderr", stderr):
            usage.run_push(args)
        self.assertIn("claude: credentials unavailable", stderr.getvalue())

    def test_watch_renews_lease_and_survives_transient_error(self) -> None:
        args = Namespace(providers=["codex"], always=False, interval=1, pin="1234", device_token_env=None, serial=None, url="https://device", baud=115200, allow_insecure_http=False)
        credentials = {"codex": {"access_token": "token", "account_id": "id"}}
        with patch.object(usage, "load_credentials", side_effect=[(credentials, {}), usage.UsageError("request failed")]), patch.object(usage, "provision") as provision, patch.object(usage.time, "monotonic", return_value=0), patch.object(usage.time, "sleep", side_effect=KeyboardInterrupt):
            with self.assertRaises(KeyboardInterrupt):
                usage.run_watch(args)
        provision.assert_called_once()

    def test_watch_stops_when_device_rejects_credentials(self) -> None:
        args = Namespace(providers=["codex"], always=False, interval=1, pin="1234", device_token_env=None, serial=None, url="https://device", baud=115200, allow_insecure_http=False)
        credentials = {"codex": {"access_token": "token", "account_id": "id"}}
        with patch.object(usage, "load_credentials", return_value=(credentials, {})), patch.object(usage, "provision", side_effect=usage.UsageError("request failed with HTTP 423")):
            with self.assertRaisesRegex(usage.UsageError, "HTTP 423"):
                usage.run_watch(args)

    def test_watch_renews_unchanged_credentials_before_one_hour(self) -> None:
        args = Namespace(providers=["codex"], always=False, interval=1, pin="1234", device_token_env=None, serial=None, url="https://device", baud=115200, allow_insecure_http=False)
        credentials = {"codex": {"access_token": "token", "account_id": "id"}}
        with patch.object(usage, "load_credentials", return_value=(credentials, {})), patch.object(usage, "provision") as provision, patch.object(usage.time, "monotonic", side_effect=[0, usage.LEASE_REFRESH_SECONDS]), patch.object(usage.time, "sleep", side_effect=[None, KeyboardInterrupt]):
            with self.assertRaises(KeyboardInterrupt):
                usage.run_watch(args)
        self.assertEqual(provision.call_count, 2)

    def test_render_plain_has_no_unselected_provider(self) -> None:
        rendered = usage.render_plain({"codex": {"rate_limit": {}}})
        self.assertIn("Codex", rendered)
        self.assertNotIn("Cursor", rendered)
        self.assertNotIn("Claude", rendered)

    def test_configure_posts_screen_payload_only(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "device.json"
            path.write_text(json.dumps({"version": 1, "device": {"base_url": "https://wroom.local"}, "screens": [{"type": "gc9a01_240", "provider": "claude"}], "allow_insecure_http": False}))
            args = usage.build_parser().parse_args(["configure", "--config", str(path), "--pin", "1234"])
            with patch.object(usage, "request_json", return_value={"ok": True, "reboot_required": False}) as request:
                usage.run_configure(args)
        self.assertEqual(request.call_args.args[0], "https://wroom.local/api/config")
        self.assertEqual(request.call_args.kwargs["body"], {"version": 1, "screens": [{"type": "gc9a01_240", "provider": "claude"}]})

    def test_config_push_selects_mapped_provider_and_reboot_blocks_credentials(self) -> None:
        config = {"version": 1, "device": {"base_url": "https://wroom.local"}, "screens": [{"type": "gc9a01_240", "provider": "codex"}, {"type": "gc9a01_240", "provider": "codex"}], "allow_insecure_http": False}
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "device.json"
            path.write_text(json.dumps(config))
            args = usage.build_parser().parse_args(["push", "--config", str(path), "--pin", "1234"])
            codex_read = patch.object(usage.PROVIDERS["codex"], "read_credentials", return_value={"access_token": "token", "account_id": "id"})
            cursor_read = patch.object(usage.PROVIDERS["cursor"], "read_credentials")
            with patch.object(usage, "request_json", return_value={"ok": True, "reboot_required": False}), codex_read as codex, cursor_read as cursor, patch.object(usage, "push_http"):
                usage.run_push(args)
            codex.assert_called_once()
            cursor.assert_not_called()

            reboot_args = usage.build_parser().parse_args(["push", "--config", str(path), "--pin", "1234"])
            with patch.object(usage, "request_json", return_value={"ok": True, "reboot_required": True}), patch.object(usage, "load_credentials") as credentials:
                with self.assertRaisesRegex(usage.UsageError, "reboot the device"):
                    usage.run_push(reboot_args)
            credentials.assert_not_called()

    def test_config_watch_posts_before_credential_loading(self) -> None:
        config = {"version": 1, "device": {"base_url": "https://wroom.local"}, "screens": [{"type": "gc9a01_240", "provider": "codex"}], "allow_insecure_http": False}
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "device.json"
            path.write_text(json.dumps(config))
            args = usage.build_parser().parse_args(["watch", "--config", str(path), "--pin", "1234", "--interval", "1"])
            with patch.object(usage, "request_json", return_value={"ok": True, "reboot_required": True}), patch.object(usage, "load_credentials") as credentials:
                with self.assertRaisesRegex(usage.UsageError, "reboot the device"):
                    usage.run_watch(args)
            credentials.assert_not_called()


if __name__ == "__main__":
    unittest.main()
