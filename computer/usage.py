#!/usr/bin/env python3
"""Extract Cursor and Codex client credentials, fetch usage, print it, and
optionally provision those credentials to an ESP32.
"""

from __future__ import annotations

import argparse
import base64
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import shutil
import sqlite3
import subprocess
import sys
import time
from typing import Any
import urllib.error
import urllib.parse
import urllib.request


CURSOR_USAGE_URL = "https://cursor.com/api/usage-summary"
CODEX_USAGE_URL = "https://chatgpt.com/backend-api/wham/usage"
DEFAULT_CURSOR_DB = (
    Path.home()
    / "Library"
    / "Application Support"
    / "Cursor"
    / "User"
    / "globalStorage"
    / "state.vscdb"
)
DEFAULT_CODEX_AUTH = Path.home() / ".codex" / "auth.json"
# nvs partition in the firmware two-OTA-large table (ssid/password only).
NVS_OFFSET = 0x9000
NVS_SIZE = 0x6000


class UsageError(RuntimeError):
    pass


def pairing_pin(value: str) -> str:
    if len(value) != 4 or not value.isascii() or not value.isdigit():
        raise argparse.ArgumentTypeError("PIN must be exactly four digits")
    return value


def decode_jwt_payload(token: str) -> dict[str, Any]:
    """Decode JWT metadata without treating it as cryptographically verified."""
    try:
        segment = token.split(".")[1]
        segment += "=" * (-len(segment) % 4)
        return json.loads(base64.urlsafe_b64decode(segment))
    except (IndexError, ValueError, json.JSONDecodeError) as error:
        raise UsageError("credential is not a valid JWT") from error


def iso_time(timestamp: int | float | None) -> str | None:
    if timestamp is None:
        return None
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime(timestamp))


def read_cursor_credentials(db_path: Path) -> dict[str, Any]:
    if not db_path.exists():
        raise UsageError(f"Cursor state database not found: {db_path}")

    uri = f"file:{urllib.parse.quote(str(db_path))}?mode=ro"
    try:
        with sqlite3.connect(uri, uri=True, timeout=5) as database:
            row = database.execute(
                "SELECT value FROM ItemTable WHERE key = ?",
                ("cursorAuth/accessToken",),
            ).fetchone()
    except sqlite3.Error as error:
        raise UsageError(f"cannot read Cursor state database: {error}") from error

    if not row:
        raise UsageError("Cursor access token is missing; sign in to Cursor first")

    token = row[0]
    if isinstance(token, bytes):
        token = token.decode()
    token = token.strip().strip('"')
    claims = decode_jwt_payload(token)
    subject = str(claims.get("sub", ""))
    user_id = subject.rsplit("|", 1)[-1]
    if not user_id or user_id == subject and "|" not in subject:
        raise UsageError("Cursor JWT does not contain the expected user ID")

    return {
        "cookie": f"WorkosCursorSessionToken={user_id}::{token}",
        "expires_at": claims.get("exp"),
    }


def read_codex_credentials(auth_path: Path) -> dict[str, Any]:
    if not auth_path.exists():
        raise UsageError(f"Codex auth file not found: {auth_path}")

    try:
        document = json.loads(auth_path.read_text())
        tokens = document["tokens"]
        access_token = tokens["access_token"]
        account_id = tokens["account_id"]
    except (OSError, KeyError, TypeError, json.JSONDecodeError) as error:
        raise UsageError(f"cannot read Codex credentials: {error}") from error

    claims = decode_jwt_payload(access_token)
    return {
        "access_token": access_token,
        "account_id": account_id,
        "expires_at": claims.get("exp"),
        "auth_mode": document.get("auth_mode"),
    }


def request_json(
    url: str,
    *,
    method: str = "GET",
    headers: dict[str, str] | None = None,
    body: dict[str, Any] | None = None,
    timeout: float = 20,
) -> dict[str, Any]:
    encoded = json.dumps(body).encode() if body is not None else None
    request = urllib.request.Request(url, data=encoded, method=method)
    request.add_header("Accept", "application/json")
    if encoded is not None:
        request.add_header("Content-Type", "application/json")
    for name, value in (headers or {}).items():
        request.add_header(name, value)

    try:
        with urllib.request.urlopen(request, timeout=timeout) as response:
            return json.load(response)
    except urllib.error.HTTPError as error:
        response_body = error.read(500).decode(errors="replace")
        raise UsageError(f"{url} returned HTTP {error.code}: {response_body}") from error
    except (urllib.error.URLError, TimeoutError, json.JSONDecodeError) as error:
        raise UsageError(f"request to {url} failed: {error}") from error


def fetch_cursor_usage(credentials: dict[str, Any]) -> dict[str, Any]:
    raw = request_json(
        CURSOR_USAGE_URL,
        headers={
            "Cookie": credentials["cookie"],
            "Origin": "https://cursor.com",
        },
    )
    return {
        "credential_expires_at": iso_time(credentials.get("expires_at")),
        "billing_cycle_start": raw.get("billingCycleStart"),
        "billing_cycle_end": raw.get("billingCycleEnd"),
        "membership_type": raw.get("membershipType"),
        "limit_type": raw.get("limitType"),
        "is_unlimited": raw.get("isUnlimited"),
        "auto_message": raw.get("autoModelSelectedDisplayMessage"),
        "api_message": raw.get("namedModelSelectedDisplayMessage"),
        "individual_usage": raw.get("individualUsage"),
        "team_usage": raw.get("teamUsage"),
    }


def fetch_codex_usage(credentials: dict[str, Any]) -> dict[str, Any]:
    raw = request_json(
        CODEX_USAGE_URL,
        headers={
            "Authorization": f"Bearer {credentials['access_token']}",
            "ChatGPT-Account-Id": credentials["account_id"],
            "User-Agent": "ai-wroom-32/0.1",
        },
    )
    return {
        "credential_expires_at": iso_time(credentials.get("expires_at")),
        "auth_mode": credentials.get("auth_mode"),
        "plan_type": raw.get("plan_type"),
        "rate_limit": raw.get("rate_limit"),
        "code_review_rate_limit": raw.get("code_review_rate_limit"),
        "additional_rate_limits": raw.get("additional_rate_limits"),
        "model_usage": raw.get("model_usage"),
        "credits": raw.get("credits"),
        "spend_control": raw.get("spend_control"),
        "rate_limit_reached_type": raw.get("rate_limit_reached_type"),
        "rate_limit_reset_credits": raw.get("rate_limit_reset_credits"),
    }


def credential_payload(
    cursor_credentials: dict[str, Any],
    codex_credentials: dict[str, Any],
) -> dict[str, Any]:
    return {
        "version": 1,
        "issued_at": iso_time(time.time()),
        "cursor": {
            "usage_url": CURSOR_USAGE_URL,
            "cookie": cursor_credentials["cookie"],
            "expires_at": cursor_credentials.get("expires_at"),
        },
        "codex": {
            "usage_url": CODEX_USAGE_URL,
            "access_token": codex_credentials["access_token"],
            "account_id": codex_credentials["account_id"],
            "expires_at": codex_credentials.get("expires_at"),
        },
    }


def push_http(
    device_url: str,
    payload: dict[str, Any],
    device_token: str | None,
    allow_insecure_http: bool,
) -> None:
    parsed = urllib.parse.urlparse(device_url)
    if parsed.scheme not in ("http", "https"):
        raise UsageError("device URL must use http:// or https://")
    if parsed.scheme == "http" and not allow_insecure_http:
        raise UsageError(
            "refusing to send account credentials over plaintext HTTP; "
            "use HTTPS or explicitly pass --allow-insecure-http"
        )

    headers = {}
    if device_token:
        headers["Authorization"] = f"Bearer {device_token}"
    response = request_json(
        device_url,
        method="POST",
        headers=headers,
        body=payload,
    )
    print(json.dumps(response, indent=2))


def push_serial(port: Path, payload: dict[str, Any], baud: int) -> None:
    if sys.platform == "win32":
        raise UsageError("serial provisioning currently supports macOS/Linux only")

    try:
        import termios

        speed = getattr(termios, f"B{baud}")
        descriptor = os.open(port, os.O_RDWR | os.O_NOCTTY)
        try:
            attributes = termios.tcgetattr(descriptor)
            attributes[0] = 0
            attributes[1] = 0
            attributes[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
            attributes[3] = 0
            attributes[4] = speed
            attributes[5] = speed
            termios.tcsetattr(descriptor, termios.TCSANOW, attributes)
            os.write(descriptor, json.dumps(payload, separators=(",", ":")).encode() + b"\n")
            termios.tcdrain(descriptor)
        finally:
            os.close(descriptor)
    except (AttributeError, OSError) as error:
        raise UsageError(f"serial provisioning failed: {error}") from error

    print(f"credentials sent to {port} at {baud} baud")


def esptool_command() -> list[str]:
    for name in ("esptool.py", "esptool"):
        found = shutil.which(name)
        if found:
            return [found]
    try:
        subprocess.run(
            [sys.executable, "-m", "esptool", "--help"],
            check=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        return [sys.executable, "-m", "esptool"]
    except (OSError, subprocess.CalledProcessError):
        raise UsageError(
            "esptool is required to erase stored Wi-Fi; install with "
            "`python3 -m pip install esptool`"
        ) from None


def erase_wifi_nvs(port: Path, baud: int) -> None:
    if not port.exists():
        raise UsageError(f"serial device not found: {port}")
    command = [
        *esptool_command(),
        "--chip",
        "esp32",
        "-p",
        str(port),
        "-b",
        str(baud),
        "--after",
        "hard-reset",
        "erase-region",
        f"0x{NVS_OFFSET:x}",
        f"0x{NVS_SIZE:x}",
    ]
    try:
        subprocess.run(command, check=True)
    except subprocess.CalledProcessError as error:
        raise UsageError(
            f"failed to erase Wi-Fi NVS on {port} (exit {error.returncode})"
        ) from error
    print(
        f"Wi-Fi credentials erased on {port}. After reset, join the AIOM- SoftAP "
        "and provision again with ESP SoftAP Prov (proof of possession = PIN)."
    )


def load_credentials(args: argparse.Namespace) -> tuple[dict[str, Any], dict[str, Any]]:
    return (
        read_cursor_credentials(args.cursor_db.expanduser()),
        read_codex_credentials(args.codex_auth.expanduser()),
    )


def collect_usage(args: argparse.Namespace) -> dict[str, Any]:
    cursor, codex = load_credentials(args)
    return {
        "fetched_at": iso_time(time.time()),
        "cursor": fetch_cursor_usage(cursor),
        "codex": fetch_codex_usage(codex),
    }


def format_hms(seconds: int | float | None) -> str:
    if seconds is None:
        return "unknown"
    total = max(0, int(seconds))
    days, remainder = divmod(total, 86400)
    hours, remainder = divmod(remainder, 3600)
    minutes, secs = divmod(remainder, 60)
    if days:
        return f"{days}d {hours}h {minutes}m {secs}s"
    return f"{hours}h {minutes}m {secs}s"


def format_usd_cents(value: Any) -> str:
    if value is None:
        return "unknown"
    return f"${float(value) / 100.0:.2f}"


def format_percent_left(used_percent: Any) -> str:
    if used_percent is None:
        return "unknown"
    left = max(0.0, min(100.0, 100.0 - float(used_percent)))
    return f"{left:.1f}%"


def format_unix_local(timestamp: int | float | None) -> str:
    if timestamp is None:
        return "unknown"
    return time.strftime("%Y-%m-%d %H:%M:%S %Z", time.localtime(timestamp))


def parse_iso_datetime(value: str | None) -> datetime | None:
    if not value:
        return None
    try:
        return datetime.fromisoformat(value.replace("Z", "+00:00"))
    except ValueError:
        return None


def format_iso_local(value: str | None) -> str:
    parsed = parse_iso_datetime(value)
    if parsed is None:
        return "unknown"
    return parsed.astimezone().strftime("%Y-%m-%d %H:%M:%S %Z")


def seconds_until_iso(value: str | None) -> int | None:
    parsed = parse_iso_datetime(value)
    if parsed is None:
        return None
    now = datetime.now(timezone.utc)
    if parsed.tzinfo is None:
        parsed = parsed.replace(tzinfo=timezone.utc)
    return max(0, int((parsed - now).total_seconds()))


def format_codex_window(title: str, window: dict[str, Any] | None) -> list[str]:
    window = window or {}
    return [
        f"  {title}",
        f"    Usage left: {format_percent_left(window.get('used_percent'))}",
        f"    Next reset: {format_unix_local(window.get('reset_at'))}",
        f"    Until reset: {format_hms(window.get('reset_after_seconds'))}",
    ]


def render_plain(result: dict[str, Any]) -> str:
    codex = result.get("codex") or {}
    rate_limit = codex.get("rate_limit") or {}
    resets = (codex.get("rate_limit_reset_credits") or {}).get("available_count")
    if resets is None:
        resets = "unknown"

    lines = [
        "Codex",
        f"  Available resets: {resets}",
        *format_codex_window("Primary", rate_limit.get("primary_window")),
        *format_codex_window("Secondary", rate_limit.get("secondary_window")),
        "",
    ]

    cursor = result.get("cursor") or {}
    plan = ((cursor.get("individual_usage") or {}).get("plan") or {})
    on_demand = ((cursor.get("individual_usage") or {}).get("onDemand") or {})
    team_on_demand = ((cursor.get("team_usage") or {}).get("onDemand") or {})
    cycle_end = cursor.get("billing_cycle_end")

    lines.extend(
        [
            "Cursor",
            "  Billing cycle",
            f"    Usage left: {format_percent_left(plan.get('totalPercentUsed'))}",
            f"    Next reset: {format_iso_local(cycle_end)}",
            f"    Until reset: {format_hms(seconds_until_iso(cycle_end))}",
            "  Auto models",
            f"    Usage left: {format_percent_left(plan.get('autoPercentUsed'))}",
            "  Named / API models",
            f"    Usage left: {format_percent_left(plan.get('apiPercentUsed'))}",
        ]
    )
    if on_demand.get("enabled"):
        used = format_usd_cents(on_demand.get("used"))
        limit = on_demand.get("limit")
        if limit:
            remaining = format_usd_cents(on_demand.get("remaining"))
            lines.append(
                f"  On-demand: {used} used, {remaining} remaining of "
                f"{format_usd_cents(limit)}"
            )
        else:
            lines.append(f"  On-demand: {used} used (no individual cap)")
    if team_on_demand.get("enabled") and team_on_demand.get("limit") is not None:
        lines.append(
            "  Team on-demand: "
            f"{format_usd_cents(team_on_demand.get('used'))} used, "
            f"{format_usd_cents(team_on_demand.get('remaining'))} remaining of "
            f"{format_usd_cents(team_on_demand.get('limit'))}"
        )
    return "\n".join(lines)


def run_fetch(args: argparse.Namespace) -> None:
    print(json.dumps(collect_usage(args), indent=2))


def run_print(args: argparse.Namespace) -> None:
    print(render_plain(collect_usage(args)))


def run_push(args: argparse.Namespace) -> None:
    cursor, codex = load_credentials(args)
    payload = credential_payload(cursor, codex)
    device_token = args.pin or (
        os.environ.get(args.device_token_env) if args.device_token_env else None
    )

    if args.serial:
        push_serial(args.serial.expanduser(), payload, args.baud)
    else:
        if not device_token:
            raise UsageError(
                "HTTP push requires --pin or AI_WROOM_DEVICE_TOKEN"
            )
        push_http(args.url, payload, device_token, args.allow_insecure_http)


def run_wifi_reset(args: argparse.Namespace) -> None:
    if not args.yes:
        prompt = (
            f"Erase stored Wi-Fi on {args.serial} and return the device to SoftAP? [y/N] "
        )
        if input(prompt).strip().lower() not in ("y", "yes"):
            raise UsageError("aborted")
    erase_wifi_nvs(args.serial.expanduser(), args.baud)


def run_watch(args: argparse.Namespace) -> None:
    previous_fingerprint = None
    while True:
        cursor, codex = load_credentials(args)
        payload = credential_payload(cursor, codex)
        fingerprint = hashlib.sha256(
            (cursor["cookie"] + codex["access_token"]).encode()
        ).hexdigest()
        if fingerprint != previous_fingerprint or args.always:
            device_token = (
                args.pin
                or (
                    os.environ.get(args.device_token_env)
                    if args.device_token_env
                    else None
                )
            )
            if args.serial:
                push_serial(args.serial.expanduser(), payload, args.baud)
            else:
                if not device_token:
                    raise UsageError(
                        "HTTP watch requires --pin or AI_WROOM_DEVICE_TOKEN"
                    )
                push_http(args.url, payload, device_token, args.allow_insecure_http)
            previous_fingerprint = fingerprint
        time.sleep(args.interval)


def add_source_arguments(parser: argparse.ArgumentParser) -> None:
    parser.add_argument(
        "--cursor-db",
        type=Path,
        default=DEFAULT_CURSOR_DB,
        help=f"Cursor state database (default: {DEFAULT_CURSOR_DB})",
    )
    parser.add_argument(
        "--codex-auth",
        type=Path,
        default=DEFAULT_CODEX_AUTH,
        help=f"Codex auth file (default: {DEFAULT_CODEX_AUTH})",
    )


def add_push_arguments(parser: argparse.ArgumentParser) -> None:
    target = parser.add_mutually_exclusive_group(required=True)
    target.add_argument("--url", help="ESP32 credential endpoint URL")
    target.add_argument("--serial", type=Path, help="ESP32 serial device")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument(
        "--pin",
        type=pairing_pin,
        help="four-digit pairing PIN shown on the ESP32",
    )
    parser.add_argument(
        "--device-token-env",
        default="AI_WROOM_DEVICE_TOKEN",
        help="environment variable containing the ESP32 provisioning token",
    )
    parser.add_argument(
        "--allow-insecure-http",
        action="store_true",
        help="allow account credentials to cross the LAN without TLS",
    )


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    fetch = subparsers.add_parser("fetch", help="fetch and print normalized JSON")
    add_source_arguments(fetch)
    fetch.set_defaults(handler=run_fetch)

    plain = subparsers.add_parser("print", help="fetch and print a plain-text summary")
    add_source_arguments(plain)
    plain.set_defaults(handler=run_print)

    push = subparsers.add_parser("push", help="push current credentials to the ESP32")
    add_source_arguments(push)
    add_push_arguments(push)
    push.set_defaults(handler=run_push)

    watch = subparsers.add_parser(
        "watch",
        help="watch local credentials and push them when they change",
    )
    add_source_arguments(watch)
    add_push_arguments(watch)
    watch.add_argument("--interval", type=float, default=60)
    watch.add_argument(
        "--always",
        action="store_true",
        help="push every interval, useful after unattended ESP32 restarts",
    )
    watch.set_defaults(handler=run_watch)

    wifi_reset = subparsers.add_parser(
        "wifi-reset",
        help="erase stored Wi-Fi so the ESP32 starts SoftAP provisioning again",
    )
    wifi_reset.add_argument(
        "--serial",
        type=Path,
        required=True,
        help="ESP32 serial device, e.g. /dev/cu.SLAB_USBtoUART",
    )
    wifi_reset.add_argument("--baud", type=int, default=115200)
    wifi_reset.add_argument(
        "--yes",
        action="store_true",
        help="do not ask for confirmation",
    )
    wifi_reset.set_defaults(handler=run_wifi_reset)
    return parser


def main() -> int:
    args = build_parser().parse_args()
    try:
        args.handler(args)
        return 0
    except UsageError as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        return 130


if __name__ == "__main__":
    raise SystemExit(main())
