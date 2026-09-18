#!/usr/bin/env python3
"""Non-destructive UART protocol smoke tests for an active CC1310 bootloader.

The device must already be in bootloader update mode.  These checks never send
a valid BEGIN frame, so they must not erase or program the application slot.
"""
import argparse
import struct
import sys
import time
import zlib

try:
    import serial
except ImportError as exc:
    raise SystemExit("pyserial is required: python3 -m pip install pyserial") from exc

from fw_package import APP_ADDRESS, HEADER, MAGIC, MAX_IMAGE_SIZE
from fw_protocol import BEGIN, DATA, ERROR, HELLO, INFO, cobs_decode, cobs_encode, decode, encode

RX_TARGET_ID = 0x43433152


def read_frame(port, timeout):
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
    return read_frame(port, 1.0)


def expect_hello(port, sequence):
    message_type, answer_sequence, payload = request(port, HELLO, sequence)
    if message_type != INFO or answer_sequence != sequence or len(payload) != 20:
        raise AssertionError("HELLO did not return a valid INFO frame")
    return payload


def make_header(**changes):
    """Build a package header with a valid CRC unless header_crc is requested."""
    fields = {
        "magic": MAGIC,
        "format_version": 1,
        "header_size": HEADER.size,
        "target": RX_TARGET_ID,
        "address": APP_ADDRESS,
        "size": 4,
        "version": 1,
        "image_crc": 0,
    }
    fields.update(changes)
    prefix = HEADER.pack(fields["magic"], fields["format_version"], fields["header_size"],
                         fields["target"], fields["address"], fields["size"],
                         fields["version"], fields["image_crc"], 0)
    header_crc = fields.get("header_crc", zlib.crc32(prefix[:-4]) & 0xFFFFFFFF)
    return prefix[:-4] + struct.pack("<I", header_crc)


def expect_error(port, sequence, payload):
    message_type, answer_sequence, answer = request(port, BEGIN, sequence, payload)
    if message_type != ERROR or answer_sequence != sequence or answer:
        raise AssertionError("invalid BEGIN was not rejected")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--baud", type=int, default=115200)
    args = parser.parse_args()

    with serial.Serial(args.port, args.baud, timeout=0.05, write_timeout=1) as port:
        info_before = expect_hello(port, 1)
        print("PASS hello target=0x%08X state=%u" %
              (struct.unpack_from("<I", info_before)[0], struct.unpack_from("<I", info_before, 4)[0]))

        # Sequence is a transaction correlation value, including both boundary
        # values and normal uint16 wraparound.  HELLO must echo it unchanged.
        for sequence in (0, 65535, 0):
            if expect_hello(port, sequence) != info_before:
                raise AssertionError("HELLO sequence %u changed bootloader INFO" % sequence)
        print("PASS hello_sequence_wraparound")

        # Invalid COBS block: code 2 claims one byte that is absent.
        port.write(b"\x02\x00")
        port.flush()
        expect_hello(port, 2)
        print("PASS malformed_cobs_recovered")

        # A valid COBS frame with a deliberately bad CRC16 must be ignored.
        corrupt = bytearray(cobs_decode(encode(HELLO, 3)[:-1]))
        corrupt[-1] ^= 0x01
        port.write(cobs_encode(corrupt) + b"\0")
        port.flush()
        expect_hello(port, 4)
        print("PASS bad_crc16_recovered")

        invalid_headers = (
            ("magic", make_header(magic=0)),
            ("format_version", make_header(format_version=2)),
            ("header_size", make_header(header_size=0)),
            ("target", make_header(target=0x43433154)),
            ("address", make_header(address=0)),
            ("zero_size", make_header(size=0)),
            ("oversized", make_header(size=MAX_IMAGE_SIZE + 4)),
            ("unaligned_size", make_header(size=6)),
            ("header_crc", make_header(header_crc=0)),
        )
        for sequence, (name, header) in enumerate(invalid_headers, start=5):
            expect_error(port, sequence, header)
            if expect_hello(port, sequence + 20) != info_before:
                raise AssertionError("invalid %s BEGIN changed bootloader metadata" % name)
            print("PASS invalid_begin_%s_rejected" % name)

        # DATA before BEGIN must be rejected and must not create a session.
        message_type, sequence, payload = request(port, DATA, 40, struct.pack("<I", 0) + b"\xff" * 4)
        if message_type != ERROR or sequence != 40 or payload:
            raise AssertionError("DATA before BEGIN was not rejected")
        expect_hello(port, 41)
        print("PASS data_without_begin_rejected")


if __name__ == "__main__":
    try:
        main()
    except (AssertionError, TimeoutError, ValueError, serial.SerialException) as exc:
        print("FAIL %s" % exc, file=sys.stderr)
        raise SystemExit(1)
