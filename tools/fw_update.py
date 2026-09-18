#!/usr/bin/env python3
"""Program a CRC32-protected package through the CC1310 UART bootloader."""
import argparse
import sys
import time
from pathlib import Path

try:
    import serial
except ImportError as exc:
    raise SystemExit("pyserial is required: python3 -m pip install pyserial") from exc

from fw_package import format_version, parse
from fw_protocol import HELLO, INFO, BEGIN, READY, DATA, ACK, NACK, END, COMPLETE, SET_ROLE, encode, decode

FRAME_TIMEOUT = 0.5
FRAME_RETRIES = 5
CHUNK_SIZE = 128
BOOT_STATES = {
    1: "valid_application",
    2: "update_requested",
    3: "update_in_progress",
}
ROLES = {
    0: "unset",
    1: "rx",
    2: "tx",
}
TARGET_NAMES = {
    0x4343314D: "CC1M",
}


def format_enum(value, names):
    return "%s(%u)" % (names.get(value, "unknown"), value)


def format_target(target):
    return "%s(0x%08X)" % (TARGET_NAMES.get(target, "unknown"), target)


def format_info(info):
    fields = [
        "target=%s" % format_target(info["target"]),
        "state=%s" % format_enum(info["state"], BOOT_STATES),
        "max_size=%u" % info["max_size"],
        "image_size=%u" % info["image_size"],
        "version=%s(0x%08X)" % (format_version(info["version"]), info["version"]),
    ]
    if "role" in info:
        fields.append("role=%s" % format_enum(info["role"], ROLES))
    return "INFO " + " ".join(fields)


class Updater:
    def __init__(self, port, baud):
        self.ser = serial.Serial(port, baudrate=baud, timeout=0.05, write_timeout=1)
        self.sequence = 0

    def close(self): self.ser.close()

    def _read_frame(self, deadline):
        data = bytearray()
        while time.monotonic() < deadline:
            byte = self.ser.read(1)
            if not byte: continue
            data += byte
            if byte == b"\0":
                try: return decode(bytes(data))
                except ValueError: data.clear()
        raise TimeoutError("bootloader response timed out")

    def request(self, kind, payload=b"", expected=(), retries=FRAME_RETRIES):
        self.sequence = (self.sequence + 1) & 0xFFFF
        frame = encode(kind, self.sequence, payload)
        for _ in range(retries):
            self.ser.write(frame); self.ser.flush()
            try:
                response, sequence, answer = self._read_frame(time.monotonic() + FRAME_TIMEOUT)
            except TimeoutError:
                continue
            if sequence == self.sequence and response in expected:
                return response, answer
        raise TimeoutError("no acceptable response after %u retries" % retries)

    def info(self):
        _, payload = self.request(HELLO, expected=(INFO,))
        if len(payload) not in (20, 24): raise ValueError("invalid INFO response")
        version = int.from_bytes(payload[16:20], "little")
        info = {"target": int.from_bytes(payload[0:4], "little"),
                "state": int.from_bytes(payload[4:8], "little"),
                "max_size": int.from_bytes(payload[8:12], "little"),
                "image_size": int.from_bytes(payload[12:16], "little"),
                "version": version}
        if len(payload) == 24: info["role"] = int.from_bytes(payload[20:24], "little")
        return info

    def flash(self, package, role):
        info = parse(package)
        device = self.info()
        if device["target"] != info["target"]: raise ValueError("package target does not match device")
        if info["size"] > device["max_size"]: raise ValueError("package exceeds device App slot")
        _, ready = self.request(BEGIN, package[:32] + role.to_bytes(4, "little"), expected=(READY,))
        if len(ready) != 4: raise ValueError("invalid READY response")
        offset = int.from_bytes(ready, "little")
        if offset != 0: raise ValueError("bootloader requested unsupported resume offset")
        image = info["image"]
        while offset < len(image):
            block = image[offset:offset + CHUNK_SIZE]
            response, answer = self.request(DATA, offset.to_bytes(4, "little") + block,
                                            expected=(ACK, NACK))
            if len(answer) != 4: raise ValueError("invalid ACK/NACK response")
            expected = int.from_bytes(answer, "little")
            if response == NACK and expected > offset: raise ValueError("invalid NACK offset")
            offset = expected
            print("\rPROGRESS %u/%u (%u%%)" % (offset, len(image), offset * 100 // len(image)), end="", flush=True)
        print()
        self.request(END, expected=(COMPLETE,))

    def set_role(self, role):
        self.info()
        self.request(SET_ROLE, role.to_bytes(4, "little"), expected=(COMPLETE,))


def parse_role(value):
    roles = {"rx": 1, "tx": 2}
    try: return roles[value.lower()]
    except KeyError: raise argparse.ArgumentTypeError("role must be tx or rx")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--baud", type=int, default=115200)
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("info")
    flash = commands.add_parser("flash"); flash.add_argument("--package", required=True, type=Path)
    flash.add_argument("--role", required=True, type=parse_role)
    set_role = commands.add_parser("set-role"); set_role.add_argument("--role", required=True, type=parse_role)
    args = parser.parse_args()
    updater = Updater(args.port, args.baud)
    try:
        if args.command == "info":
            print(format_info(updater.info()))
        elif args.command == "flash":
            package = args.package.read_bytes(); details = parse(package)
            print("INFO package target=0x%08X version=%s code=0x%08X size=%u" %
                  (details["target"], format_version(details["version"]),
                   details["version"], details["size"]))
            updater.flash(package, args.role); print("COMPLETE")
        elif args.command == "set-role":
            updater.set_role(args.role); print("COMPLETE")
    finally:
        updater.close()


if __name__ == "__main__": main()
