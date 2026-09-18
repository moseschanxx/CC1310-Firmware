#!/usr/bin/env python3
"""Destructive protocol tests ending in a deliberate image CRC failure.

This test erases the app slot.  Run it only with a known-good package available
for immediate recovery.  The test verifies offset handling and that an END CRC
failure leaves the bootloader in update mode.
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
from fw_protocol import ACK, BEGIN, COMPLETE, DATA, END, ERROR, HELLO, INFO, NACK, READY, decode, encode

CHUNK_SIZE = 128


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


def request(port, kind, sequence, payload=b""):
    port.write(encode(kind, sequence, payload))
    port.flush()
    return receive(port)


def expect_offset(port, kind, sequence, payload, expected_kind, expected_offset):
    answer_kind, answer_sequence, answer = request(port, kind, sequence, payload)
    if answer_kind != expected_kind or answer_sequence != sequence or len(answer) != 4:
        raise AssertionError("unexpected response to sequence %u" % sequence)
    offset = int.from_bytes(answer, "little")
    if offset != expected_offset:
        raise AssertionError("sequence %u offset %u, expected %u" %
                             (sequence, offset, expected_offset))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--package", required=True, type=Path)
    args = parser.parse_args()
    package = args.package.read_bytes()
    details = parse(package)
    image = bytearray(details["image"])

    with serial.Serial(args.port, 115200, timeout=0.05, write_timeout=1) as port:
        kind, sequence, info = request(port, HELLO, 1)
        if kind != INFO or sequence != 1 or len(info) != 20:
            raise AssertionError("bootloader INFO unavailable")

        kind, sequence, ready = request(port, BEGIN, 2, package[:32])
        if kind != READY or sequence != 2 or ready != b"\0\0\0\0":
            raise AssertionError("valid BEGIN was not accepted")
        print("PASS begin_ready")

        # A future offset must not skip the first block.
        expect_offset(port, DATA, 3, (CHUNK_SIZE).to_bytes(4, "little") + image[CHUNK_SIZE:2 * CHUNK_SIZE],
                      NACK, 0)
        print("PASS future_offset_nack")

        first = bytes(image[:CHUNK_SIZE])
        expect_offset(port, DATA, 4, b"\0\0\0\0" + first, ACK, CHUNK_SIZE)
        print("PASS first_data_ack")

        # A lost ACK is handled by retransmitting the same DATA block.
        expect_offset(port, DATA, 5, b"\0\0\0\0" + first, ACK, CHUNK_SIZE)
        print("PASS duplicate_data_ack")

        # Write a modified second block, then all remaining original data.
        image[CHUNK_SIZE] ^= 0x01
        offset = CHUNK_SIZE
        sequence = 6
        while offset < len(image):
            block = image[offset:offset + CHUNK_SIZE]
            expect_offset(port, DATA, sequence, offset.to_bytes(4, "little") + block,
                          ACK, offset + len(block))
            offset += len(block)
            sequence = (sequence + 1) & 0xFFFF
        print("PASS corrupted_image_transferred")

        kind, answer_sequence, answer = request(port, END, sequence)
        if kind != ERROR or answer_sequence != sequence or answer:
            raise AssertionError("CRC-mismatched image was not rejected at END")
        print("PASS end_crc_failure_rejected")


if __name__ == "__main__":
    try:
        main()
    except (AssertionError, TimeoutError, ValueError, serial.SerialException) as exc:
        print("FAIL %s" % exc, file=sys.stderr)
        raise SystemExit(1)
