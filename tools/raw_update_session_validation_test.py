#!/usr/bin/env python3
"""Destructive validation of invalid DATA/END commands in an active session.

The script intentionally accepts BEGIN and erases the app slot.  It verifies
that malformed DATA is rejected without advancing the write offset, then uses
the known-good package to complete a valid transfer.  The caller should still
run a normal recovery flash after the script as the final health check.
"""
import argparse
import sys
import time
from pathlib import Path

try:
    import serial
except ImportError as exc:
    raise SystemExit("pyserial is required: python3 -m pip install pyserial") from exc

from fw_package import parse
from fw_protocol import ACK, BEGIN, DATA, END, ERROR, READY, decode, encode


def receive(port, timeout=1.0):
    deadline = time.monotonic() + timeout
    wire = bytearray()
    while time.monotonic() < deadline:
        byte = port.read(1)
        if byte:
            wire += byte
            if byte == b"\0":
                return decode(wire)
    raise TimeoutError("bootloader response timed out")


def request(port, message_type, sequence, payload=b""):
    port.write(encode(message_type, sequence, payload))
    port.flush()
    return receive(port)


def expect_rejected(port, sequence, payload):
    kind, response_sequence, response = request(port, DATA, sequence, payload)
    if kind not in (ERROR,) or response_sequence != sequence or response:
        raise AssertionError("invalid DATA was accepted: kind=%u payload=%r" % (kind, response))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--package", required=True, type=Path)
    args = parser.parse_args()
    package = args.package.read_bytes()
    image = parse(package)["image"]

    with serial.Serial(args.port, 115200, timeout=0.05, write_timeout=1) as port:
        kind, sequence, response = request(port, BEGIN, 1, package[:32])
        if kind != READY or sequence != 1 or response != b"\0\0\0\0":
            raise AssertionError("BEGIN was not accepted")
        print("PASS begin_ready")

        # offset only, overlong payload, non-word data length and beyond-image
        # offset must never be accepted as an ACK at offset zero.
        invalid = (
            (2, b"\0\0\0\0", "zero_length"),
            (3, b"\0\0\0\0" + b"\xff" * 129, "overlong"),
            (4, b"\0\0\0\0" + b"\xff" * 3, "unaligned"),
            (5, len(image).to_bytes(4, "little") + b"\xff" * 4, "beyond_image"),
        )
        for sequence, payload, label in invalid:
            expect_rejected(port, sequence, payload)
            print("PASS invalid_data_%s_rejected" % label)

        # END without a complete image must not create a valid app.  A second
        # BEGIN is allowed to explicitly restart the session at offset zero.
        kind, sequence, response = request(port, END, 6)
        if kind != ERROR or sequence != 6 or response:
            raise AssertionError("incomplete END was accepted")
        print("PASS incomplete_end_rejected")
        kind, sequence, response = request(port, BEGIN, 7, package[:32])
        if kind != READY or sequence != 7 or response != b"\0\0\0\0":
            raise AssertionError("second BEGIN did not explicitly reset the session")
        print("PASS second_begin_resets_session")

        # Send a legal first block after every rejected request; no invalid
        # frame may have advanced bytesWritten.
        kind, sequence, response = request(port, DATA, 8, b"\0\0\0\0" + image[:128])
        if kind != ACK or sequence != 8 or response != (128).to_bytes(4, "little"):
            raise AssertionError("valid first DATA was not acknowledged at 128")
        print("PASS session_offset_preserved")


if __name__ == "__main__":
    try:
        main()
    except (AssertionError, TimeoutError, ValueError, serial.SerialException) as exc:
        print("FAIL %s" % exc, file=sys.stderr)
        raise SystemExit(1)
