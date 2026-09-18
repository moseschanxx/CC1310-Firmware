#!/usr/bin/env python3
"""Extract one contiguous raw image from an Intel HEX file."""
import argparse
from pathlib import Path


def read_ihex(path):
    data, upper = {}, 0
    for number, line in enumerate(path.read_text().splitlines(), 1):
        if not line.startswith(":"): raise ValueError("line %u is not Intel HEX" % number)
        record = bytes.fromhex(line[1:])
        if sum(record) & 0xFF: raise ValueError("line %u checksum" % number)
        length, address, kind = record[0], (record[1] << 8) | record[2], record[3]
        payload = record[4:-1]
        if length != len(payload): raise ValueError("line %u length" % number)
        if kind == 0: data.update((upper + address + i, byte) for i, byte in enumerate(payload))
        elif kind == 4: upper = int.from_bytes(payload, "big") << 16
        elif kind == 1: break
    return data


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--base", required=True, type=lambda value: int(value, 0))
    parser.add_argument("--end", required=True, type=lambda value: int(value, 0))
    args = parser.parse_args()
    values = read_ihex(args.input)
    addresses = [address for address in values if args.base <= address < args.end]
    if not addresses or min(addresses) != args.base: raise SystemExit("image does not start at base")
    end = max(addresses) + 1
    args.output.write_bytes(bytes(values.get(address, 0xFF) for address in range(args.base, end)))
    print("Wrote %s: 0x%X bytes" % (args.output, end - args.base))


if __name__ == "__main__": main()
