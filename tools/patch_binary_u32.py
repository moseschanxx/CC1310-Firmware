#!/usr/bin/env python3
"""Patch one little-endian uint32 in a bounded firmware binary."""
import argparse
from pathlib import Path


def parse_number(value: str) -> int:
    return int(value, 0)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--file", type=Path, required=True)
    parser.add_argument("--offset", type=parse_number, required=True)
    parser.add_argument("--value", type=parse_number, required=True)
    args = parser.parse_args()
    image = bytearray(args.file.read_bytes())
    if args.offset < 0 or args.offset + 4 > len(image):
        raise SystemExit(f"offset {args.offset:#x} is outside {args.file}")
    if not 0 <= args.value <= 0xFFFFFFFF:
        raise SystemExit("value must fit in uint32")
    image[args.offset:args.offset + 4] = args.value.to_bytes(4, "little")
    args.file.write_bytes(image)
    print(f"Patched {args.file}: offset={args.offset:#x}, value={args.value:#x}")


if __name__ == "__main__":
    main()
