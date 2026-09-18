#!/usr/bin/env python3
"""Verify that loss of power after BEGIN leaves the RX bootloader recoverable.

The DUT must be in UART update mode.  The script accepts a valid package,
submits only its BEGIN header, cuts relay power after READY, restores power,
and checks that the bootloader remains in UPDATE_IN_PROGRESS.  It deliberately
does not restore the application; invoke fw_update.py with the same package
immediately after this script succeeds.
"""
import argparse
import struct
import sys
import time
from pathlib import Path

try:
    import serial
except ImportError as exc:
    raise SystemExit("pyserial is required: python3 -m pip install pyserial") from exc

from fw_package import parse
from fw_protocol import BEGIN, HELLO, INFO, READY, decode, encode
from relay_control import DEFAULT_BAUD, DEFAULT_PORT, RELAY_CHANNEL, frame, transact

UPDATE_IN_PROGRESS = 3


def receive(port, timeout=1.0):
    deadline = time.monotonic() + timeout
    wire = bytearray()
    while time.monotonic() < deadline:
        byte = port.read(1)
        if not byte:
            continue
        wire += byte
        if byte == b"\0":
            return decode(wire)
    raise TimeoutError("bootloader response timed out")


def request(port, message_type, sequence, payload=b""):
    port.write(encode(message_type, sequence, payload))
    port.flush()
    return receive(port)


def set_relay(action):
    operation = {"off": 0x00, "on": 0x01}[action]
    transact(DEFAULT_PORT, DEFAULT_BAUD, frame(RELAY_CHANNEL, operation), 1.0)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--package", required=True, type=Path)
    parser.add_argument("--off-seconds", type=float, default=1.0)
    args = parser.parse_args()
    package = args.package.read_bytes()
    parse(package)  # Never erase the app slot with an invalid local fixture.

    try:
        with serial.Serial(args.port, 115200, timeout=0.05, write_timeout=1) as port:
            kind, sequence, response = request(port, BEGIN, 1, package[:32])
            if kind != READY or sequence != 1 or response != b"\0\0\0\0":
                raise AssertionError("BEGIN was not accepted")
            print("PASS begin_ready")

        # The valid update-state record is already committed before erase.  Cut
        # power only after READY so reset must choose update mode, never the app.
        set_relay("off")
        time.sleep(args.off_seconds)
        set_relay("on")
        time.sleep(1.5)

        with serial.Serial(args.port, 115200, timeout=0.05, write_timeout=1) as port:
            kind, sequence, response = request(port, HELLO, 2)
        if kind != INFO or sequence != 2 or len(response) != 20:
            raise AssertionError("bootloader did not respond after power restore")
        state = struct.unpack_from("<I", response, 4)[0]
        if state != UPDATE_IN_PROGRESS:
            raise AssertionError("INFO state %u, expected UPDATE_IN_PROGRESS" % state)
        print("PASS reset_after_begin_stays_in_update_mode")
    finally:
        # Ensure the relay is on even if serial/test handling fails.
        try:
            set_relay("on")
        except OSError:
            pass


if __name__ == "__main__":
    try:
        main()
    except (AssertionError, TimeoutError, ValueError, OSError, serial.SerialException) as exc:
        print("FAIL %s" % exc, file=sys.stderr)
        raise SystemExit(1)
