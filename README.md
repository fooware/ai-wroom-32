# ESP32 hardware meter for AI subscription usage

## Disclaimer

**This repository is largely vibe-coded.** Treat it as a personal experiment, not production software. Do not rely on it for billing, access control, or anything that would hurt if it were wrong.

This project handles live account credentials and can send them over the local network. Insecure use may expose those credentials and the associated accounts.

The firmware, the computer-side tool, and this documentation are provided **as is**, without warranty of any kind, express or implied, including but not limited to merchantability, fitness for a particular purpose, and non-infringement. The authors make no claim that the code is correct, complete, secure, or suitable for any use. You use it at your own risk.

## Overview

Firmware and a dual-screen LVGL demo live in [`esp32/`](esp32/). Hardware pinout and the backlight change for the next board revision are in [`esp32/README.md`](esp32/README.md). The computer-side usage tool is `computer/usage.py`. USB serial logs: `computer/serial_log.py`.

`computer/usage.py` is the computer-side example. It shows how to:

1. Read the credentials already stored by the Cursor and Codex clients.
2. Call each product's JSON usage endpoint with those credentials.
3. Print a normalized usage/limit summary.
4. Optionally send the credentials to an ESP32 so the device can poll the
   same endpoints. The device is expected to keep them in RAM only.

The example uses the Python standard library (`sqlite3`, `urllib`, `json`).
There is no first-party SDK for these usage endpoints.

## Quick start

Requirements:

- macOS (the default Cursor database path is macOS-specific)
- Python 3.11 or newer
- Signed-in clients for the providers you select
- Network access to the selected providers (`cursor.com`, `chatgpt.com`, `api.anthropic.com`)

JSON dump of the normalized payloads:

```sh
python3 computer/usage.py fetch
```

Plain-text summary (percent remaining, reset times, available Codex resets):

```sh
python3 computer/usage.py print
```

Credentials are never printed. `fetch` output is JSON of this shape:

```json
{
  "cursor": {
    "billing_cycle_start": "...",
    "individual_usage": {
      "plan": {
        "autoPercentUsed": 11.3,
        "apiPercentUsed": 100,
        "totalPercentUsed": 25.6
      }
    }
  },
  "codex": {
    "rate_limit": {
      "primary_window": {
        "used_percent": 100,
        "reset_at": 1789391733
      },
      "secondary_window": {
        "used_percent": 16,
        "reset_at": 1789978533
      }
    }
  }
}
```

Erase the device's stored Wi-Fi and screen configuration so SoftAP provisioning runs again:

```sh
python3 computer/usage.py wifi-reset --serial /dev/cu.SLAB_USBtoUART
```

Needs `esptool`. After reset, join `AIOM-…` and use ESP SoftAP Prov with the PIN as proof of possession.

Print firmware logs while the board is on USB (auto-detects the adapter):

```sh
python3 computer/serial_log.py
```

Opening the port may reset the chip on a typical USB-UART adapter. `python3 computer/serial_log.py --help` lists `--port`, `--baud`, `--timestamps`, and `--reset`.

`python3 computer/usage.py --help` lists subcommands and flags.

These usage endpoints are undocumented and can change without notice.

## Cursor credentials and lookup

Cursor stores an access JWT in:

```text
~/Library/Application Support/Cursor/User/globalStorage/state.vscdb
```

Table `ItemTable`, key `cursorAuth/accessToken`. The JWT `sub` claim contains
the user ID. The dashboard session cookie is:

```text
WorkosCursorSessionToken=<user-id>::<access-jwt>
```

A JWT without the `<user-id>::` prefix is rejected (login redirect).

Usage summary:

```http
GET https://cursor.com/api/usage-summary
Cookie: WorkosCursorSessionToken=<user-id>::<access-jwt>
Origin: https://cursor.com
Accept: application/json
```

This is the request the meter example uses. It returns billing-cycle
percentages in a small payload.

Per-event token breakdown (optional; not used by `fetch`):

```http
POST https://cursor.com/api/dashboard/get-filtered-usage-events
Cookie: WorkosCursorSessionToken=<user-id>::<access-jwt>
Origin: https://cursor.com
Content-Type: application/json

{"page":1,"pageSize":1000}
```

A Cursor user API key (`crsr_...`) authenticates Cloud Agents
(`GET https://api.cursor.com/v1/me`) but is not accepted by
`/api/usage-summary` or the team Admin spend/usage APIs. Those Admin APIs
require a team admin key and are out of scope for a signed-in user.

## Codex credentials and lookup

With ChatGPT login, Codex stores OAuth tokens in:

```text
~/.codex/auth.json
```

Fields used: `tokens.access_token`, `tokens.account_id`.

```http
GET https://chatgpt.com/backend-api/wham/usage
Authorization: Bearer <access-token>
ChatGPT-Account-Id: <account-id>
Accept: application/json
```

The response includes primary and secondary rate-limit windows, percent used,
reset times, credit state, and spend-control state.
`https://chatgpt.com/backend-api/codex/usage` returns the same payload.

The access token expires on a short interval. Codex rewrites `auth.json` when
it refreshes. The `watch` subcommand rereads that file and can reprovision
the ESP32 when the token changes.

OpenAI Platform project keys (`sk-...`) authenticate pay-per-token API usage.
They are a different ledger from a ChatGPT/Codex subscription and do not
authorize the `wham/usage` endpoint. Organization cost and aggregate usage
APIs require an OpenAI Admin key and report Platform billing, not ChatGPT
plan quota.

## What the numbers mean

The two products do not report the same quantity:

- Cursor `/api/usage-summary` reports billing-cycle included-usage percentages
  and on-demand fields. The JSON `onDemand.used` / `limit` / `remaining`
  values are treated as USD cents. `print` converts them to dollars
  (`1816` → `$18.16`). `fetch` leaves the raw integers unchanged. Plan
  `used` / `limit` are not converted; their unit is not documented the same way.
- Codex `wham/usage` reports percent of rolling rate-limit windows consumed.
  It does not return historical token totals or dollar spend for ChatGPT-plan
  activity.

This is a usage/limit meter, not a reconciled invoice.

## Provisioning an ESP32 without persisting credentials on the device

```text
Cursor / Codex local auth files
              |
        computer/usage.py
              |
        HTTP on the local LAN
              |
     ESP32 credentials in RAM
              |
    Cursor / Codex usage endpoints
```

Firmware must keep received credentials in RAM only: not NVS, Preferences,
SPIFFS, LittleFS, logs, crash dumps, or serial debug. After reset the device
has no credentials until the computer provisions it again. If the computer
does not push credentials for about an hour, the device must drop the RAM
tokens and return to attraction mode.

One-shot to the IP and PIN shown on the right display:

```sh
python3 computer/usage.py push \
  --url http://192.168.1.42/api/credentials \
  --pin 4827 \
  --allow-insecure-http
```

Refresh the one-hour RAM credential lease every minute:

```sh
python3 computer/usage.py watch \
  --url http://192.168.1.42/api/credentials \
  --pin 4827 \
  --allow-insecure-http \
  --interval 60 \
  --always
```

The endpoint requires the boot-scoped pairing PIN in `Authorization: Bearer`.
Three incorrect PINs lock credential intake until reboot. A valid request
stores the service credentials in RAM and returns
`{"ok":true,"lease_seconds":3600}`. The device then HTTPS-fetches Cursor and
Codex usage, waiting 15 seconds after each attempt, and switches to the meter
faces. If no refresh arrives for an hour, it drops the RAM tokens and returns
to attraction.

Plain HTTP would expose the Cursor session and Codex bearer token on the LAN.
The tool therefore requires the explicit `--allow-insecure-http` flag.
TLS or USB serial intake can replace this development path later.

Payload:

```json
{
  "version": 1,
  "issued_at": "...",
  "cursor": {
    "usage_url": "https://cursor.com/api/usage-summary",
    "cookie": "WorkosCursorSessionToken=<user-id>::<jwt>",
    "expires_at": 1794556938
  },
  "codex": {
    "usage_url": "https://chatgpt.com/backend-api/wham/usage",
    "access_token": "<oauth-access-token>",
    "account_id": "<chatgpt-account-id>",
    "expires_at": 1789467008
  }
}
```

Placeholders are not usable credentials.

## Screen configuration

Create a secret-free local configuration for one to three screens. The wizard
stores only the device address, the display type, provider names, and the
explicit HTTP opt-in; it never stores a PIN or account credential.

```sh
python3 computer/usage.py config wizard --output device.json
```

The resulting version-1 JSON has this shape. Repeating a provider is allowed;
the computer reads and sends that provider's credential once.

```json
{
  "version": 1,
  "device": {"base_url": "http://192.168.1.42"},
  "screens": [
    {"type": "gc9a01_240", "provider": "codex"},
    {"type": "gc9a01_240", "provider": "cursor"},
    {"type": "gc9a01_240", "provider": "claude"}
  ],
  "allow_insecure_http": true
}
```

Apply the layout first, using the current pairing PIN shown by the device:

```sh
python3 computer/usage.py configure --config device.json --pin 4827
```

The device stores the screen layout in NVS and may respond that a reboot is
required. Reboot it, read the new boot-scoped PIN, then provision credentials:

```sh
python3 computer/usage.py push --config device.json --pin 4827
python3 computer/usage.py watch --config device.json --pin 4827 --interval 60
```

With `--config`, the tool posts the layout before reading any provider
credentials. A reboot-required response stops there, so no credentials are
sent with the old layout. The configuration chooses the providers; do not add
`--providers`, `--url`, or `--serial` to those commands. `wifi-reset` clears
this stored screen configuration as well as Wi-Fi.

## Security

- Extracted credentials act as the signed-in user. Treat them as secrets.
- `fetch` keeps them in process memory only.
- `push` and `watch` do not write a credential cache.
- Cursor and Codex continue to persist their own auth files; this project
  does not change that.
- JWT payload decoding is used only for metadata (`sub`, `exp`), not signature
  verification.
- Do not commit captured responses. They can include account identifiers.

## Selectable providers and Claude

`fetch`, `print`, `push`, and `watch` accept `--providers codex,cursor,claude`.
The default remains `codex,cursor`. Each provider has its own credential reader,
usage adapter, and display metadata. A failure in one provider does not prevent
successful providers from updating. Missing windows display `--`, not a full quota.
Screen mapping is selected through the screen configuration described above.

Claude requires **Claude Code signed in with the Claude subscription**, even if
Claude for Mac is your usual client. On macOS the tool reads the
`Claude Code-credentials` Keychain item. Alternatively, pass `--claude-auth PATH`
to an existing Claude Code credential JSON file containing
`claudeAiOauth.accessToken`. Do not copy tokens into a project configuration.
The tool does not refresh OAuth tokens itself; sign in/refresh with Claude Code.

```sh
python3 computer/usage.py print --providers claude
python3 computer/usage.py fetch --providers codex,cursor,claude
```

Claude usage is fetched from `https://api.anthropic.com/api/oauth/usage` using
its OAuth bearer and `anthropic-beta: oauth-2025-04-20`. This subscription endpoint
is undocumented and may change. The two meters use `five_hour` and `seven_day`
utilization percentages and reset times; this is not Anthropic API credit billing.
No account tokens or captured responses are included in the repository or tests.

The version-1 credential payload now accepts any nonempty subset of the known
provider keys. Claude's entry contains `access_token`; existing Codex and Cursor
fields are unchanged. Each successful POST replaces the entire set: omitted
providers are cleared. Malformed supplied credentials reject the replacement.
Device endpoints are fixed in firmware and cannot be overridden by payload URLs.

Credentials remain RAM-only with a one-hour lease. `watch` rereads credentials
and renews even unchanged credentials every 30 minutes; `--always` still pushes
each interval. Provider errors are reported separately without response bodies.

Validation (no real credentials needed):

```sh
python3 -m unittest discover -s computer/tests -v
cmake -S esp32/tests -B /tmp/ai-wroom-tests -DCJSON_DIR=/path/to/cJSON
cmake --build /tmp/ai-wroom-tests
ctest --test-dir /tmp/ai-wroom-tests --output-on-failure
```

The SDL simulator accepts `--claude` to preview a Claude face, `--meters` for
meter previews, and `--smoke` to exit after a few frames (`SDL_VIDEODRIVER=dummy`
works in a headless environment).

Firmware wiring is set in `idf.py menuconfig` → **AI-O-Meter screen wiring**.
All screens share SCLK, MOSI and DC; each ordered screen slot has its own CS.
RST may be shared or set to `-1` for software reset. The third CS defaults to
`-1` (unused): configure a valid free output GPIO before selecting three screens.
The firmware rejects colliding pins and unwired slots. On a classic ESP32, avoid
flash pins 6–11, and check your board's reserved PSRAM and boot-strapping pins.

The display manager uses partial 20-line RGB565 buffers, rather than a full
framebuffer per screen. If PSRAM is enabled and available, LVGL draws there and
the port copies strips into internal DMA buffers. DMA sends one panel's strip
at a time on the shared SPI bus. This release supports at most three panels;
PSRAM capacity alone does not increase that limit.
