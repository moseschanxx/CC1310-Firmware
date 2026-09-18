#!/usr/bin/env python3
"""Create and verify CRC32-protected CC1310 firmware packages."""
import argparse
import struct
import zlib
from pathlib import Path

MAGIC = 0x4B505746  # FWPK in little-endian memory order
FORMAT_VERSION = 1
APP_ADDRESS = 0x8000
MAX_IMAGE_SIZE = 0x17000
HEADER = struct.Struct("<IHHIIIIII")


def crc32(data):
    return zlib.crc32(data) & 0xFFFFFFFF


def format_version(version):
    """Format the packed 0x00MMmmpp firmware version as major.minor.patch."""
    if not 0 <= version <= 0x00FFFFFF:
        raise ValueError("version must reserve its most-significant byte")
    return "%u.%u.%u" % ((version >> 16) & 0xFF,
                           (version >> 8) & 0xFF,
                           version & 0xFF)


def parse(package):
    if len(package) < HEADER.size:
        raise ValueError("package is shorter than its header")
    fields = HEADER.unpack_from(package)
    magic, fmt, header_size, target, address, size, version, image_crc, header_crc = fields
    if magic != MAGIC or fmt != FORMAT_VERSION or header_size != HEADER.size:
        raise ValueError("unsupported package header")
    if crc32(package[:HEADER.size - 4]) != header_crc:
        raise ValueError("header CRC32 mismatch")
    if address != APP_ADDRESS or size > MAX_IMAGE_SIZE or size == 0:
        raise ValueError("invalid application address or size")
    format_version(version)
    image = package[HEADER.size:]
    if len(image) != size:
        raise ValueError("package length does not match header image size")
    if crc32(image) != image_crc:
        raise ValueError("image CRC32 mismatch")
    return {"target": target, "address": address, "size": size,
            "version": version, "image_crc": image_crc, "image": image}


def build(image, target, version):
    # CC1310 FlashProgram accepts word-sized writes. Padding is executable
    # erased-flash data and is included in the image CRC and package length.
    format_version(version)
    image += b"\xFF" * ((-len(image)) & 3)
    if not image or len(image) > MAX_IMAGE_SIZE:
        raise ValueError("image size is outside the App slot")
    prefix = HEADER.pack(MAGIC, FORMAT_VERSION, HEADER.size, target, APP_ADDRESS,
                         len(image), version, crc32(image), 0)
    return prefix[:-4] + struct.pack("<I", crc32(prefix[:-4])) + image


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--target-id", type=lambda x: int(x, 0))
    parser.add_argument("--version", type=lambda x: int(x, 0))
    parser.add_argument("--verify", type=Path)
    args = parser.parse_args()
    if args.verify:
        info = parse(args.verify.read_bytes())
        print("OK target=0x%08X version=%s code=0x%08X size=%u crc32=%08X" %
              (info["target"], format_version(info["version"]), info["version"],
               info["size"], info["image_crc"]))
        return
    if None in (args.input, args.output, args.target_id, args.version):
        parser.error("use --verify, or provide --input --output --target-id --version")
    package = build(args.input.read_bytes(), args.target_id, args.version)
    parse(package)
    args.output.write_bytes(package)
    print("Wrote %s (%u bytes)" % (args.output, len(package)))


if __name__ == "__main__":
    main()
