#!/usr/bin/env python3
"""Control a CH340/LC-series USB relay over a serial device.

The relay protocol is the four-byte LC USB switch frame:
    A0 <channel> <operation> <checksum>

Examples (macOS):
    python3 tools/relay_control.py on
    python3 tools/relay_control.py off

The default port is the Songle controller currently connected to this host.
"""

import argparse
import os
import select
import sys
import termios
import time


DEFAULT_PORT = "/dev/cu.usbserial-2130"
DEFAULT_BAUD = 9600
RELAY_CHANNEL = 0x01


def baud_constant(baud: int) -> int:
    name = f"B{baud}"
    try:
        return getattr(termios, name)
    except AttributeError as exc:
        raise ValueError(f"unsupported baud rate: {baud}") from exc


def open_serial(port: str, baud: int) -> tuple[int, list]:
    fd = os.open(port, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
    previous = termios.tcgetattr(fd)
    settings = termios.tcgetattr(fd)
    speed = baud_constant(baud)

    settings[0] = 0  # input flags
    settings[1] = 0  # output flags
    settings[2] = termios.CLOCAL | termios.CREAD | termios.CS8
    settings[3] = 0  # local flags: raw mode
    settings[4] = speed
    settings[5] = speed
    settings[6][termios.VMIN] = 0
    settings[6][termios.VTIME] = 0
    termios.tcsetattr(fd, termios.TCSANOW, settings)
    termios.tcflush(fd, termios.TCIOFLUSH)
    return fd, previous


def transact(port: str, baud: int, frame: bytes, timeout: float) -> bytes:
    fd, previous = open_serial(port, baud)
    try:
        os.write(fd, frame)
        deadline = time.monotonic() + timeout
        response = bytearray()
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                return bytes(response)
            readable, _, _ = select.select([fd], [], [], remaining)
            if not readable:
                return bytes(response)
            response.extend(os.read(fd, 256))
    finally:
        termios.tcsetattr(fd, termios.TCSANOW, previous)
        os.close(fd)


def frame(channel: int, operation: int) -> bytes:
    # LC protocol checksum is the sum of the first three bytes modulo 256.
    prefix = bytes((0xA0, channel, operation))
    return prefix + bytes((sum(prefix) & 0xFF,))


def format_response(response: bytes) -> str:
    """Render text responses safely; preserve binary responses as hex."""
    cleaned = response.rstrip(b"\0\r\n")
    if cleaned and all(0x20 <= byte <= 0x7E for byte in cleaned):
        return cleaned.decode("ascii")
    return "hex=" + response.hex(" ")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default=DEFAULT_PORT, help=f"serial port (default: {DEFAULT_PORT})")
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD, help="serial baud rate (default: 9600)")
    parser.add_argument("--timeout", type=float, default=1.0, help="response timeout in seconds (default: 1)")
    parser.add_argument("action", choices=("on", "off"),
                        help="control the single relay channel")
    args = parser.parse_args()

    operation = {"off": 0x00, "on": 0x01}[args.action]
    request = frame(RELAY_CHANNEL, operation)

    try:
        response = transact(args.port, args.baud, request, args.timeout)
    except (OSError, ValueError) as exc:
        print(f"relay error: {exc}", file=sys.stderr)
        return 1

    if response:
        print(format_response(response))
    else:
        print(f"relay {args.action} command sent")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
