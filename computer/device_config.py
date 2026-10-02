"""Versioned, secret-free device screen configuration helpers."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any, Callable
from urllib.parse import urlparse


CONFIG_VERSION = 1
SCREEN_TYPES = ("gc9a01_240",)
PROVIDERS = ("codex", "cursor", "claude")
_TOP_LEVEL_KEYS = {"version", "device", "screens", "allow_insecure_http"}
_DEVICE_KEYS = {"base_url"}
_SCREEN_KEYS = {"type", "provider"}
_API_PATHS = {"", "/", "/api/config", "/api/credentials"}


class ConfigError(ValueError):
    """An invalid or unsafe local device configuration."""


def canonical_base_url(value: str) -> str:
    """Turn a host or known API endpoint into a canonical HTTP(S) base URL."""
    if not isinstance(value, str):
        raise ConfigError("device must be a string")
    text = value.strip()
    if not text:
        raise ConfigError("device hostname is required")
    parsed = urlparse(text if "://" in text else f"http://{text}")
    if parsed.scheme not in ("http", "https") or not parsed.netloc:
        raise ConfigError("device must be a hostname or an http(s) URL")
    if parsed.username or parsed.password or parsed.query or parsed.fragment:
        raise ConfigError("device URL must not contain credentials, query, or fragment")
    if parsed.path.rstrip("/") not in {path.rstrip("/") for path in _API_PATHS}:
        raise ConfigError("device URL path must be the base, /api/config, or /api/credentials")
    try:
        if not parsed.hostname or any(c.isspace() for c in parsed.netloc):
            raise ValueError()
        _ = parsed.port
    except ValueError:
        raise ConfigError("device URL has an invalid hostname or port") from None
    return f"{parsed.scheme}://{parsed.netloc}"


def _require_keys(value: dict[str, Any], allowed: set[str], location: str) -> None:
    unknown = set(value) - allowed
    if unknown:
        raise ConfigError(f"{location} contains unsupported key: {sorted(unknown)[0]}")


def validate_config(config: Any) -> dict[str, Any]:
    if not isinstance(config, dict):
        raise ConfigError("configuration must be a JSON object")
    _require_keys(config, _TOP_LEVEL_KEYS, "configuration")
    version = config.get("version")
    if isinstance(version, bool) or not isinstance(version, int) or version != CONFIG_VERSION:
        raise ConfigError(f"version must be integer {CONFIG_VERSION}")
    device = config.get("device")
    if not isinstance(device, dict):
        raise ConfigError("device must be an object")
    _require_keys(device, _DEVICE_KEYS, "device")
    base_url = canonical_base_url(device.get("base_url", ""))
    screens = config.get("screens")
    if not isinstance(screens, list) or not 1 <= len(screens) <= 3:
        raise ConfigError("screens must contain between 1 and 3 entries")
    normalized_screens: list[dict[str, str]] = []
    for index, screen in enumerate(screens, start=1):
        if not isinstance(screen, dict):
            raise ConfigError(f"screen {index} must be an object")
        _require_keys(screen, _SCREEN_KEYS, f"screen {index}")
        screen_type = screen.get("type")
        provider = screen.get("provider")
        if screen_type not in SCREEN_TYPES:
            raise ConfigError(f"screen {index} has unsupported type")
        if provider not in PROVIDERS:
            raise ConfigError(f"screen {index} has unsupported provider")
        normalized_screens.append({"type": screen_type, "provider": provider})
    insecure = config.get("allow_insecure_http", False)
    if not isinstance(insecure, bool):
        raise ConfigError("allow_insecure_http must be true or false")
    if base_url.startswith("http://") and not insecure:
        raise ConfigError("http device URLs require allow_insecure_http=true")
    return {
        "version": CONFIG_VERSION,
        "device": {"base_url": base_url},
        "screens": normalized_screens,
        "allow_insecure_http": insecure,
    }


def load_config(path: Path) -> dict[str, Any]:
    try:
        document = json.loads(path.read_text())
    except OSError as error:
        raise ConfigError(f"cannot read configuration: {error.strerror or 'file unavailable'}") from error
    except json.JSONDecodeError:
        raise ConfigError("configuration is not valid JSON") from None
    return validate_config(document)


def save_config(path: Path, config: dict[str, Any], *, overwrite: bool = False) -> None:
    """Save validated config without silently replacing an existing file."""
    normalized = validate_config(config)
    if path.exists() and not overwrite:
        raise ConfigError(f"refusing to overwrite existing configuration: {path}")
    try:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(normalized, indent=2) + "\n")
    except OSError as error:
        raise ConfigError(f"cannot save configuration: {error.strerror or 'write failed'}") from error


def config_payload(config: dict[str, Any]) -> dict[str, Any]:
    """Return only the data the device's /api/config endpoint needs."""
    normalized = validate_config(config)
    return {"version": normalized["version"], "screens": normalized["screens"]}


def _choice(prompt: str, values: tuple[str, ...], input_fn: Callable[[str], str], output_fn: Callable[[str], None]) -> str:
    while True:
        value = input_fn(f"{prompt} ({', '.join(values)}): ").strip()
        if value in values:
            return value
        output_fn(f"Choose one of: {', '.join(values)}")


def wizard(output: Path, *, input_fn: Callable[[str], str] = input, output_fn: Callable[[str], None] = print) -> dict[str, Any]:
    """Interactively collect and save a device config.  PINs are never requested."""
    while True:
        try:
            base_url = canonical_base_url(input_fn("Device hostname or URL: "))
            break
        except ConfigError as error:
            output_fn(str(error))
    while True:
        count_text = input_fn("Number of screens (1-3): ").strip()
        try:
            count = int(count_text)
        except ValueError:
            output_fn("screen count must be an integer")
            continue
        if 1 <= count <= 3:
            break
        output_fn("screen count must be between 1 and 3")
    screens = [
        {
            "type": _choice(f"Screen {index} type", SCREEN_TYPES, input_fn, output_fn),
            "provider": _choice(f"Screen {index} provider", PROVIDERS, input_fn, output_fn),
        }
        for index in range(1, count + 1)
    ]
    insecure = False
    if base_url.startswith("http://"):
        insecure = input_fn("Allow insecure HTTP credential transport? [y/N]: ").strip().lower() in {"y", "yes"}
        if not insecure:
            raise ConfigError("use HTTPS or explicitly allow insecure HTTP")
    config = {"version": CONFIG_VERSION, "device": {"base_url": base_url}, "screens": screens, "allow_insecure_http": insecure}
    save_config(output, config)
    return config
