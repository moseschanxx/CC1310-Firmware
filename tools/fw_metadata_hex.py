#!/usr/bin/env python3
"""Create a J-Link Intel HEX record that marks a verified boot app valid.

This is only for factory/recovery J-Link programming.  Normal field updates
must use the UART package protocol so the bootloader writes metadata itself.
"""
import argparse
import struct
import sys
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import fw_package
import ihex_to_bin

METADATA_ADDRESS = 0x6000
METADATA_MAGIC = 0x424C4D44
FIRMWARE_TARGET_ID = 0x4343314D  # "CC1M"
VALID_APPLICATION = 1
METADATA_FORMAT_VERSION = 2
METADATA = struct.Struct("<IHHIIIIIIIIII")


def parse_role(value):
    roles = {"rx": 1, "tx": 2}
    try:
        return roles[value.lower()]
    except KeyError:
        raise argparse.ArgumentTypeError("role must be tx or rx")


def ihex_record(address, record_type, data):
    body = bytes([len(data), address >> 8, address & 0xFF, record_type]) + data
    return ":" + (body + bytes([(-sum(body)) & 0xFF])).hex().upper() + "\n"


def app_hex_matches_package(app_hex, package):
    values = ihex_to_bin.read_ihex(app_hex)
    start = package["address"]
    expected = package["image"]
    actual = bytes(values.get(address, 0xFF)
                   for address in range(start, start + len(expected)))
    if actual != expected:
        raise ValueError("app HEX flash contents do not match the OTA package image")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--package", required=True, type=Path)
    parser.add_argument("--app-hex", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--role", type=parse_role, default=1)
    args = parser.parse_args()

    package = fw_package.parse(args.package.read_bytes())
    if package["target"] != FIRMWARE_TARGET_ID:
        raise ValueError("package target is not the combined firmware (CC1M)")
    app_hex_matches_package(args.app_hex, package)
    record_without_crc = METADATA.pack(
        METADATA_MAGIC, METADATA_FORMAT_VERSION, METADATA.size,
        1, VALID_APPLICATION, package["size"], package["image_crc"],
        package["version"], args.role, 0, 0, 0, 0)
    crc = zlib.crc32(record_without_crc[:-4]) & 0xFFFFFFFF
    record = record_without_crc[:-4] + struct.pack("<I", crc)

    lines = [ihex_record(METADATA_ADDRESS + offset, 0, record[offset:offset + 16])
             for offset in range(0, len(record), 16)]
    lines.append(ihex_record(0, 1, b""))
    args.output.write_text("".join(lines))
    print("Wrote %s: valid metadata for version=%s code=0x%08X size=%u crc32=%08X" %
          (args.output, fw_package.format_version(package["version"]),
           package["version"], package["size"], package["image_crc"]))


if __name__ == "__main__":
    main()
