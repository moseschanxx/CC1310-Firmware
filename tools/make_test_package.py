#!/usr/bin/env python3
"""Create intentionally simple, CRC-valid packages for bootloader negative tests."""
import argparse
import struct
from pathlib import Path

from fw_package import build


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--target", type=lambda value: int(value, 0), default=0x43433152)
    parser.add_argument("--version", type=int, default=999)
    parser.add_argument("--msp", type=lambda value: int(value, 0), default=0)
    parser.add_argument("--reset", type=lambda value: int(value, 0), default=0)
    args = parser.parse_args()
    # Four additional bytes keep the package word aligned while making the
    # vector table intentionally invalid only when requested by the caller.
    args.output.write_bytes(build(struct.pack("<II", args.msp, args.reset),
                                  args.target, args.version))


if __name__ == "__main__":
    main()
