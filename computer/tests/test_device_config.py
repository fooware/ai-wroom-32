from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest


MODULE = Path(__file__).parents[1] / "device_config.py"
SPEC = importlib.util.spec_from_file_location("device_config", MODULE)
assert SPEC and SPEC.loader
device_config = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = device_config
SPEC.loader.exec_module(device_config)


def valid_config() -> dict:
    return {
        "version": 1,
        "device": {"base_url": "https://wroom.local/api/credentials"},
        "screens": [{"type": "gc9a01_240", "provider": "codex"}],
        "allow_insecure_http": False,
    }


class DeviceConfigTests(unittest.TestCase):
    def test_mixed_screen_types_survive_saved_configuration(self) -> None:
        config = valid_config()
        config["screens"].append({"type": "gc9b72_360", "provider": "claude"})
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "mixed.json"
            device_config.save_config(path, config)
            self.assertEqual(device_config.load_config(path)["screens"], config["screens"])
            self.assertEqual(device_config.config_payload(config)["screens"], config["screens"])

    def test_load_normalizes_known_api_path(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "device.json"
            path.write_text(json.dumps(valid_config()))
            self.assertEqual(device_config.load_config(path)["device"]["base_url"], "https://wroom.local")

    def test_rejects_secrets_and_unknown_fields(self) -> None:
        config = valid_config()
        config["pin"] = "1234"
        with self.assertRaisesRegex(device_config.ConfigError, "unsupported key"):
            device_config.validate_config(config)

    def test_version_requires_integer_not_boolean(self) -> None:
        config = valid_config()
        config["version"] = True
        with self.assertRaisesRegex(device_config.ConfigError, "integer"):
            device_config.validate_config(config)

    def test_payload_strips_local_transport_options(self) -> None:
        config = valid_config()
        payload = device_config.config_payload(config)
        self.assertEqual(payload, {"version": 1, "screens": config["screens"]})

    def test_save_refuses_overwrite(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "device.json"
            device_config.save_config(path, valid_config())
            with self.assertRaisesRegex(device_config.ConfigError, "overwrite"):
                device_config.save_config(path, valid_config())

    def test_http_needs_explicit_opt_in(self) -> None:
        config = valid_config()
        config["device"]["base_url"] = "http://wroom.local"
        with self.assertRaisesRegex(device_config.ConfigError, "allow_insecure"):
            device_config.validate_config(config)

    def test_wizard_stores_no_pin(self) -> None:
        answers = iter(["http://wroom.local", "2", "gc9a01_240", "codex", "gc9a01_240", "claude", "yes"])
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "device.json"
            config = device_config.wizard(path, input_fn=lambda _: next(answers))
            self.assertTrue(config["allow_insecure_http"])
            self.assertEqual([screen["provider"] for screen in config["screens"]], ["codex", "claude"])
            self.assertNotIn("pin", path.read_text())

    def test_screen_count_boundary_and_duplicate_provider(self) -> None:
        config = valid_config()
        config["screens"] *= 3
        self.assertEqual(len(device_config.validate_config(config)["screens"]), 3)
        for count in (0, 4):
            config["screens"] = [{"type": "gc9a01_240", "provider": "codex"}] * count
            with self.assertRaisesRegex(device_config.ConfigError, "between 1 and 3"):
                device_config.validate_config(config)

    def test_rejects_unknown_provider_and_bad_device(self) -> None:
        config = valid_config()
        config["screens"][0]["provider"] = "profile"
        with self.assertRaisesRegex(device_config.ConfigError, "unsupported provider"):
            device_config.validate_config(config)
        for value in (1, "https://wroom.local:bad", "https://wroom .local"):
            config = valid_config()
            config["device"]["base_url"] = value
            with self.assertRaises(device_config.ConfigError):
                device_config.validate_config(config)


if __name__ == "__main__":
    unittest.main()
