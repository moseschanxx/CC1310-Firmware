#!/usr/bin/env python3
"""Cut power immediately after the RX CLI accepts `bootloader`."""
import argparse
import sys
import time

try:
    import serial
except ImportError as exc:
    raise SystemExit("pyserial is required") from exc

from fw_protocol import HELLO, INFO, decode, encode
from relay_control import DEFAULT_BAUD, DEFAULT_PORT, RELAY_CHANNEL, frame, transact


def relay(action):
    transact(DEFAULT_PORT, DEFAULT_BAUD, frame(RELAY_CHANNEL, {"off": 0, "on": 1}[action]), 1.0)


def boot_info(port_name):
    with serial.Serial(port_name, 115200, timeout=.05, write_timeout=1) as port:
        port.write(encode(HELLO, 1)); port.flush()
        deadline = time.monotonic() + 2
        wire = bytearray()
        while time.monotonic() < deadline:
            value = port.read(1)
            if value:
                wire += value
                if value == b"\0":
                    kind, sequence, data = decode(wire)
                    if kind == INFO and sequence == 1 and len(data) == 20:
                        return int.from_bytes(data[4:8], "little")
    raise TimeoutError("no bootloader INFO")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    args = parser.parse_args()
    with serial.Serial(args.port, 115200, timeout=.05, write_timeout=1) as port:
        port.reset_input_buffer(); port.write(b"\nbootloader\n"); port.flush()
        deadline = time.monotonic() + 2
        response = bytearray()
        while time.monotonic() < deadline:
            response += port.read(256)
            if b"OK rebooting_to_bootloader" in response:
                break
        else:
            raise AssertionError("CLI did not acknowledge bootloader request")
    relay("off"); time.sleep(.5); relay("on"); time.sleep(1.5)
    state = boot_info(args.port)
    if state not in (2, 3):
        raise AssertionError("unsafe post-cut boot state %u" % state)
    print("PASS cli_request_power_loss_state=%u" % state)


if __name__ == "__main__":
    try:
        main()
    except (AssertionError, TimeoutError, ValueError, OSError, serial.SerialException) as exc:
        print("FAIL %s" % exc, file=sys.stderr)
        raise SystemExit(1)
