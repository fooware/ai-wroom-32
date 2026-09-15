# ESP32 firmware and LVGL demo

Dual 240×240 GC9A01 gauges on ESP-WROOM-32, plus a desktop SDL simulator.
Both targets share `ui/`.

```
esp32/
  ui/         LVGL demo screens (no drivers)
  sim/        two SDL windows, same UI
  firmware/   ESP-IDF app, two GC9A01 panels
```

Pinout matches `rxh-wroom-32` `dual-display/src/displays.hpp`:

| Signal | GPIO |
|---|---|
| SCLK | 5 |
| MOSI | 18 |
| DC (shared) | 19 |
| CS A / RST A | 22 / 23 |
| CS B / RST B | 15 / 4 |

There is no backlight GPIO on this revision. The GC9A01 modules are wired as seven-pin panels (RST, CS, DC, SDA, SCL, GND, VCC). The LED is tied to 3.3 V, so it is always on.

GC9A01 commands `53h` (CTRL display) and `51h` (write brightness) were sent after panel init. They did not change intensity on this hardware. Do not rely on those registers for dimming.

### Final hardware revision (backlight)

Bring out **BLK/BL** on both modules and drive it from the ESP32. Do not solder BLK to VCC.

Recommended:

| Signal | GPIO | Notes |
|---|---|---|
| BLK (both panels, shared) | 21 | PWM via LEDC. Unused on the current pinout; on the WROOM header next to CS A / RST A. |

- Use 8-pin (or more) GC9A01 modules that actually break out BLK, or add an LED driver if the 7-pin part has no control pin.
- One PWM GPIO is enough for both screens if they share polarity. Tie both BLK pins together at the ESP32.
- Typical module datasheets: BLK high = on, low = off. Confirm on the chosen part; invert in firmware if needed.
- Dim with LEDC PWM at a few kHz (about 1–5 kHz) so it does not flicker and does not beat with the LCD refresh.
- If PWM on BLK does not dim (some boards only hard-switch, or BLK is a logic input without a transistor), add a low-side N-MOSFET (or the Waveshare-style BJT) between LED cathode / BLK transistor and GND, PWM the gate, and keep the LED current off the GPIO.
- Size the 3.3 V LED supply for **two** round panels at full white. PWM only reduces average current; the regulator still has to handle peaks.
- After reset, default BLK so the screens are visible (PWM at a known duty, not 0%) until firmware sets the user brightness.
- Firmware should stop treating `51h`/`53h` as the brightness path once GPIO PWM is in place.

Unused WROOM GPIOs that are already on the same header, if BLK must be split per panel: **16** and **17**.

## Wi-Fi provisioning and stored secrets

ESP-IDF NVS stores the Wi-Fi SSID/password so the unit reconnects after reboot.
Cursor and Codex credentials stay in RAM only and must never be written to NVS,
logs, crash dumps, or a filesystem.

First boot, after `idf.py erase-nvs`, or after `python3 computer/usage.py wifi-reset --serial …`:

1. The right screen shows SoftAP name `AIOM-` plus the last three MAC bytes,
   and a four-digit PIN.
2. Join that AP and run Espressif SoftAP provisioning (phone app **ESP SoftAP
   Prov**, or `esp_prov.py --transport softap --sec_ver 1`). Security version
   **1**, proof-of-possession = the PIN on the glass.
3. The accepted SSID/password are stored in NVS. The SoftAP stops.
4. The right screen then shows the LAN IP and a **new** boot-scoped pairing PIN
   for later RAM-only Cursor/Codex credential push. Three wrong pairing PINs
   lock that path until reboot.

If saved credentials do not yield an IP within 30 seconds after boot, the
device clears that Wi-Fi config and opens the SoftAP again.

Three wrong SoftAP PINs also lock provisioning until reboot.

For development, ordinary NVS is enough. For a final device, enable NVS
encryption together with ESP32 flash encryption before production programming.
Do not put Wi-Fi passwords in `sdkconfig.defaults`, a committed header, or the
firmware image.

## Simulator

Needs CMake, Ninja, SDL2 (`brew install sdl2`).

```sh
cmake -S esp32/sim -B esp32/sim/build
cmake --build esp32/sim/build
./esp32/sim/build/sim
```

Two windows: Codex (left) and Cursor (right). Exit with Ctrl+C in the terminal.

To iterate on the meter faces (arcs and center text) without provisioning the
device, either set `SIM_START_METERS` to `1` at the top of `esp32/sim/main.c`,
or configure once with:

```sh
cmake -S esp32/sim -B esp32/sim/build -DSIM_START_METERS=ON
cmake --build esp32/sim/build
```

Edit the sample values in `show_meter_preview()` in `main.c`, or change the
layout in `esp32/ui/ui.c`, rebuild, and relaunch.

## Firmware

ESP-IDF v6.1 (this machine: `~/.espressif/v6.1/esp-idf`).

```sh
source ~/.espressif/tools/activate_idf_v6.1.sh
cd esp32/firmware
idf.py set-target esp32
idf.py build
idf.py -p /dev/cu.usbserial-* flash monitor
```

The first build downloads LVGL, `esp_lvgl_port`, and `esp_lcd_gc9a01` into `managed_components/` (gitignored).

## Credential intake

Once connected, the device serves `POST /api/credentials` on the displayed IP.
Send the displayed pairing PIN as `Authorization: Bearer NNNN`. Three incorrect
PINs lock intake until reboot. Accepted Cursor/Codex credentials exist only in
RAM and expire one hour after the latest successful push.

The development endpoint is plain HTTP, so `computer/usage.py` requires
`--allow-insecure-http`. TLS or USB serial intake can replace it later.

## Usage meters

After a successful credential POST, the device fetches Cursor and Codex usage
over HTTPS about every 30 seconds and draws the dual meter faces. Keep the
computer pushing with `watch --always` so the one-hour RAM lease does not
expire. When the lease ends, tokens are wiped and attraction returns.
