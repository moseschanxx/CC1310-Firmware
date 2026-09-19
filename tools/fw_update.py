#!/usr/bin/env python3
"""Program a CRC32-protected package through the CC1310 UART bootloader.

The bootloader speaks the framed protocol only while the device is in UART update mode.  When
the application CLI answers instead, ``flash`` and ``set-role`` first send its ``bootloader``
command (the application records an update request and resets into the updater) and then
continue; ``info`` reports what the application says and leaves it running, because a device
that entered the updater only returns to the application through a completed ``flash`` or
``set-role``.  ``--port auto`` selects the single USB serial adapter on this host, which is
convenient on Windows where COM numbers change between adapters.
"""
import argparse
import sys
import time
from pathlib import Path

try:
    import serial
    from serial.tools import list_ports
except ImportError as exc:
    raise SystemExit("pyserial is required: python3 -m pip install pyserial") from exc

from fw_package import format_version, parse
from fw_protocol import HELLO, INFO, BEGIN, READY, DATA, ACK, NACK, END, COMPLETE, SET_ROLE, encode, decode

FRAME_TIMEOUT = 0.5
FRAME_RETRIES = 5
PROBE_RETRIES = 2            # HELLO attempts before looking for the application CLI
REBOOT_RETRIES = 10          # HELLO attempts while the application resets into the updater
CHUNK_SIZE = 128
CLI_TIMEOUT = 1.0
BOOTLOADER_COMMAND_TIMEOUT = 2.0   # wait for "OK rebooting_to_bootloader"
RESET_SETTLE = 0.5                 # let the device reset before the first HELLO
BANNER_TIMEOUT = 8.0
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


def format_application(fields):
    return "INFO application version=%s role=%s" % (fields.get("version", "unknown"),
                                                    fields.get("role", "unknown"))


def parse_cli_fields(line):
    """Return the key=value pairs of an application CLI line such as 'OK version=0.2.1 role=tx'."""
    fields = {}
    for token in line.split():
        key, separator, value = token.partition("=")
        if separator:
            fields[key] = value
    return fields


def resolve_port(port):
    """Return the serial device for ``port``; 'auto' picks the only USB serial adapter present."""
    if port.lower() != "auto":
        return port
    candidates = [entry for entry in list_ports.comports() if entry.vid is not None]
    if len(candidates) == 1:
        print("INFO port=%s (%s)" % (candidates[0].device, candidates[0].description))
        return candidates[0].device
    if not candidates:
        raise RuntimeError("no USB serial adapter found; pass --port explicitly")
    raise RuntimeError("several USB serial adapters found; pass --port explicitly: "
                       + ", ".join("%s (%s)" % (entry.device, entry.description)
                                   for entry in candidates))


class Updater:
    def __init__(self, port, baud, transport=None):
        self.ser = transport or serial.Serial(port, baudrate=baud, timeout=0.05, write_timeout=1)
        self.sequence = 0

    def close(self): self.ser.close()

    # --- framed bootloader protocol ---------------------------------------------------------
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

    def info(self, retries=FRAME_RETRIES):
        _, payload = self.request(HELLO, expected=(INFO,), retries=retries)
        if len(payload) not in (20, 24): raise ValueError("invalid INFO response")
        version = int.from_bytes(payload[16:20], "little")
        info = {"target": int.from_bytes(payload[0:4], "little"),
                "state": int.from_bytes(payload[4:8], "little"),
                "max_size": int.from_bytes(payload[8:12], "little"),
                "image_size": int.from_bytes(payload[12:16], "little"),
                "version": version}
        if len(payload) == 24: info["role"] = int.from_bytes(payload[20:24], "little")
        return info

    # --- application CLI (text, one command per line, replies start with OK or ERR) ---------
    def _iter_lines(self, deadline):
        """Yield complete text lines as they arrive until ``deadline``, then any partial line."""
        data = bytearray()
        while time.monotonic() < deadline:
            chunk = self.ser.read(256)
            if not chunk: continue
            data += chunk
            while b"\n" in data:
                line, _, data = data.partition(b"\n")
                text = line.decode("ascii", "replace").strip()
                if text: yield text
        text = data.decode("ascii", "replace").strip()
        if text: yield text

    def _read_lines(self, seconds):
        return list(self._iter_lines(time.monotonic() + seconds))

    def _command(self, command, seconds=None):
        self.ser.write(command.encode("ascii") + b"\r\n"); self.ser.flush()
        return self._read_lines(CLI_TIMEOUT if seconds is None else seconds)

    def probe_application(self):
        """Return the application's version/role fields when its CLI answers, else None."""
        self._command("", 0.3)   # terminate any partial line the CLI may hold (e.g. HELLO bytes)
        fields = {}
        for line in self._command("version"):
            if line.startswith("OK version="):
                fields.update(parse_cli_fields(line))
        if "version" not in fields:
            return None
        if "role" not in fields:
            commands = [line.split()[1] for line in self._command("help")
                        if line.startswith("OK ") and len(line.split()) > 1]
            fields["role"] = "tx" if "tx" in commands else "rx" if "rx" in commands else "unknown"
        return fields

    def enter_bootloader(self):
        """Tell the running application to reset into the updater; return the bootloader INFO."""
        replies = self._command("bootloader", BOOTLOADER_COMMAND_TIMEOUT)
        if not any(line.startswith("OK rebooting_to_bootloader") for line in replies):
            raise RuntimeError("application did not enter the bootloader: %s"
                               % ("; ".join(replies) or "no reply"))
        time.sleep(RESET_SETTLE)
        self.ser.reset_input_buffer()
        return self.info(retries=REBOOT_RETRIES)

    def connect(self, allow_application=True):
        """Return the bootloader INFO, switching a running application into the updater."""
        try:
            return self.info(retries=PROBE_RETRIES)
        except TimeoutError:
            pass
        if not allow_application:
            raise TimeoutError("no bootloader response; put the device in UART update mode "
                               "(CLI command 'bootloader') or drop --no-auto-bootloader")
        fields = self.probe_application()
        if fields is None:
            raise TimeoutError("neither the bootloader nor the application CLI answered on this port")
        print(format_application(fields) + "; rebooting it into the UART updater")
        return self.enter_bootloader()

    def wait_for_application(self, seconds=None):
        """Return the application's startup banner fields after a reset, or None.

        The first bytes after a reset can be garbled by the UART line settling, so the banner
        is recognised by its ``version=``/``role=`` fields rather than by an exact prefix.
        """
        deadline = time.monotonic() + (BANNER_TIMEOUT if seconds is None else seconds)
        fields = {}
        for line in self._iter_lines(deadline):
            if "version=" in line:
                fields.update(parse_cli_fields(line))
            elif "cli=ready" in line and fields:
                break
        return fields or None

    # --- operations -------------------------------------------------------------------------
    def flash(self, package, role, allow_application=True):
        info = parse(package)
        device = self.connect(allow_application)
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

    def set_role(self, role, allow_application=True):
        self.connect(allow_application)
        self.request(SET_ROLE, role.to_bytes(4, "little"), expected=(COMPLETE,))


def parse_role(value):
    roles = {"rx": 1, "tx": 2}
    try: return roles[value.lower()]
    except KeyError: raise argparse.ArgumentTypeError("role must be tx or rx")


def report_restart(updater):
    fields = updater.wait_for_application()
    if fields:
        print("INFO application restarted: version=%s role=%s"
              % (fields.get("version", "unknown"), fields.get("role", "unknown")))
    else:
        print("INFO application banner not seen within %.0f s; check the device manually" % BANNER_TIMEOUT)


def run(args):
    updater = Updater(resolve_port(args.port), args.baud)
    try:
        if args.command == "info":
            try:
                print(format_info(updater.info(retries=PROBE_RETRIES)))
            except TimeoutError:
                fields = updater.probe_application()
                if fields is None: raise
                print(format_application(fields) + " state=running (bootloader inactive; "
                      "'flash' and 'set-role' reboot into it automatically)")
        elif args.command == "flash":
            package = args.package.read_bytes(); details = parse(package)
            print("INFO package target=0x%08X version=%s code=0x%08X size=%u" %
                  (details["target"], format_version(details["version"]),
                   details["version"], details["size"]))
            updater.flash(package, args.role, not args.no_auto_bootloader); print("COMPLETE")
            report_restart(updater)
        elif args.command == "set-role":
            updater.set_role(args.role, not args.no_auto_bootloader); print("COMPLETE")
            report_restart(updater)
    finally:
        updater.close()


def build_parser():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--port", required=True,
                        help="serial device, e.g. /dev/cu.usbserial-XXXX or COM4; 'auto' picks the only USB adapter")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--no-auto-bootloader", action="store_true",
                        help="fail instead of sending the application CLI 'bootloader' command")
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("info")
    flash = commands.add_parser("flash"); flash.add_argument("--package", required=True, type=Path)
    flash.add_argument("--role", required=True, type=parse_role)
    set_role = commands.add_parser("set-role"); set_role.add_argument("--role", required=True, type=parse_role)
    return parser


def main():
    args = build_parser().parse_args()
    try:
        run(args)
    except (TimeoutError, ValueError, RuntimeError, OSError, serial.SerialException) as error:
        print("ERROR %s" % error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__": sys.exit(main())
