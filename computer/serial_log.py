#!/usr/bin/env python3
"""Print ESP32 firmware logs from the USB serial adapter.

Typical CP210x/CH340 adapters pulse DTR/RTS on open, which resets the chip
and drops RAM-only credentials. This tool clears those lines after open;
pass --reset to pulse them deliberately before printing.
"""

from __future__ import annotations

import argparse
import array
import fcntl
from glob import glob
import os
from pathlib import Path
import select
import sys
import termios
import time


DEFAULT_BAUD = 115200
SKIP_PORT_NAMES = {
    "Bluetooth-Incoming-Port",
    "debug-console",
    "wlan-debug",
}
PORT_GLOBS = (
    "/dev/cu.SLAB_USBtoUART",
    "/dev/cu.usbserial-*",
    "/dev/cu.wchusbserial*",
    "/dev/cu.usbmodem*",
    "/dev/ttyUSB*",
    "/dev/ttyACM*",
)

# Darwin / Linux TIOCM bits for DTR and RTS.
TIOCMGET = getattr(termios, "TIOCMGET", 0x4004746A)
TIOCMSET = getattr(termios, "TIOCMSET", 0x8004746D)
TIOCM_DTR = 0x0002
TIOCM_RTS = 0x0004


class SerialLogError(RuntimeError):
    pass


def discover_port() -> Path:
    seen: list[Path] = []
    for pattern in PORT_GLOBS:
        for match in sorted(glob(pattern)):
            path = Path(match)
            if path.name in SKIP_PORT_NAMES:
                continue
            if path.exists() and path not in seen:
                seen.append(path)
    if not seen:
        raise SerialLogError(
            "no USB serial device found; pass --port /dev/cu.SLAB_USBtoUART"
        )
    return seen[0]


def configure_tty(fd: int, baud: int) -> None:
    speed = getattr(termios, f"B{baud}", None)
    if speed is None:
        raise SerialLogError(f"unsupported baud rate {baud}")
    attributes = termios.tcgetattr(fd)
    attributes[0] = 0
    attributes[1] = 0
    attributes[2] = termios.CS8 | termios.CREAD | termios.CLOCAL
    attributes[3] = 0
    attributes[4] = speed
    attributes[5] = speed
    attributes[6][termios.VMIN] = 0
    attributes[6][termios.VTIME] = 0
    termios.tcsetattr(fd, termios.TCSANOW, attributes)


def set_modem_bits(fd: int, dtr: bool, rts: bool) -> None:
    buf = array.array("I", [0])
    fcntl.ioctl(fd, TIOCMGET, buf, True)
    if dtr:
        buf[0] |= TIOCM_DTR
    else:
        buf[0] &= ~TIOCM_DTR
    if rts:
        buf[0] |= TIOCM_RTS
    else:
        buf[0] &= ~TIOCM_RTS
    fcntl.ioctl(fd, TIOCMSET, buf, True)


def pulse_reset(fd: int) -> None:
    """Classic USB-UART auto-reset: RTS/DTR into EN, then run mode."""
    set_modem_bits(fd, dtr=False, rts=True)
    time.sleep(0.05)
    set_modem_bits(fd, dtr=True, rts=False)
    time.sleep(0.05)
    set_modem_bits(fd, dtr=False, rts=False)


def emit(text: str, timestamps: bool) -> None:
    if timestamps:
        now = time.strftime("%H:%M:%S")
        sys.stdout.write(f"{now} {text}")
    else:
        sys.stdout.write(text)
    sys.stdout.flush()


def follow(port: Path, baud: int, timestamps: bool, reset: bool) -> None:
    if not port.exists():
        raise SerialLogError(f"serial device not found: {port}")

    fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    try:
        configure_tty(fd, baud)
        if reset:
            pulse_reset(fd)
        else:
            try:
                set_modem_bits(fd, dtr=False, rts=False)
            except OSError:
                pass
        print(f"# {port} at {baud} baud", file=sys.stderr)
        leftover = b""
        while True:
            ready, _, _ = select.select([fd], [], [], 0.25)
            if not ready:
                continue
            chunk = os.read(fd, 4096)
            if not chunk:
                continue
            leftover += chunk
            leftover = leftover.replace(b"\r\n", b"\n").replace(b"\r", b"\n")
            while b"\n" in leftover:
                raw, leftover = leftover.split(b"\n", 1)
                emit(raw.decode("utf-8", "replace") + "\n", timestamps)
            if leftover and leftover[-1] != 10 and len(leftover) > 2048:
                emit(leftover.decode("utf-8", "replace"), timestamps)
                leftover = b""
    finally:
        os.close(fd)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--port",
        type=Path,
        help="serial device (default: first USB UART on this machine)",
    )
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD)
    parser.add_argument(
        "--timestamps",
        action="store_true",
        help="prefix each line with the local time",
    )
    parser.add_argument(
        "--reset",
        action="store_true",
        help="pulse DTR/RTS to reset the ESP32 before printing",
    )
    return parser


def main() -> int:
    args = build_parser().parse_args()
    try:
        port = args.port.expanduser() if args.port else discover_port()
        follow(port, args.baud, args.timestamps, args.reset)
        return 0
    except SerialLogError as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        return 130


if __name__ == "__main__":
    raise SystemExit(main())
