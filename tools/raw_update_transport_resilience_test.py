#!/usr/bin/env python3
"""Exercise CRC16 retry and noisy UART recovery during a real app transfer."""
import argparse
import sys
import time
from pathlib import Path

try:
    import serial
except ImportError as exc:
    raise SystemExit("pyserial is required: python3 -m pip install pyserial") from exc

from fw_package import parse
from fw_protocol import ACK, BEGIN, COMPLETE, DATA, END, READY, cobs_decode, cobs_encode, decode, encode

CHUNK = 128


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


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--package", required=True, type=Path)
    args = parser.parse_args()
    package = args.package.read_bytes()
    image = parse(package)["image"]

    with serial.Serial(args.port, 115200, timeout=0.02, write_timeout=1) as port:
        # Oversized encoded garbage and malformed raw bytes must not prevent a
        # subsequent valid BEGIN.  Delimiters bound the bootloader frame buffer.
        port.write(b"\x55" * 300 + b"\0\x02\0")
        port.flush()
        time.sleep(0.05)
        kind, sequence, response = request(port, BEGIN, 1, package[:32])
        if kind != READY or sequence != 1 or response != b"\0\0\0\0":
            raise AssertionError("BEGIN failed after noise")
        print("PASS noise_before_begin_recovered")

        offset = 0
        sequence = 2
        blocks = 0
        while offset < len(image):
            block = image[offset:offset + CHUNK]
            payload = offset.to_bytes(4, "little") + block
            if blocks and blocks % 10 == 0:
                # Send a syntactically valid frame with a bad CRC16.  It must
                # be discarded silently, then the correctly retransmitted
                # frame must receive the normal ACK.
                corrupted = bytearray(cobs_decode(encode(DATA, sequence, payload)[:-1]))
                corrupted[-1] ^= 1
                port.write(cobs_encode(corrupted) + b"\0")
                port.flush()
                time.sleep(0.02)
            kind, answer_sequence, response = request(port, DATA, sequence, payload)
            expected = offset + len(block)
            if kind != ACK or answer_sequence != sequence or response != expected.to_bytes(4, "little"):
                raise AssertionError("DATA ACK mismatch at %u" % offset)
            offset = expected
            blocks += 1
            sequence = (sequence + 1) & 0xffff
        print("PASS crc16_corruption_retried blocks=%u" % blocks)
        kind, answer_sequence, response = request(port, END, sequence)
        if kind != COMPLETE or answer_sequence != sequence or response:
            raise AssertionError("END did not complete")
        print("PASS complete_after_transport_errors")


if __name__ == "__main__":
    try:
        main()
    except (AssertionError, TimeoutError, ValueError, serial.SerialException) as exc:
        print("FAIL %s" % exc, file=sys.stderr)
        raise SystemExit(1)
