#!/usr/bin/env python3
"""Exercise normal RX application boot/confirmation across relay power cycles."""
import argparse
import sys
import time

try:
    import serial
except ImportError as exc:
    raise SystemExit("pyserial is required: python3 -m pip install pyserial") from exc

from relay_control import DEFAULT_BAUD, DEFAULT_PORT, RELAY_CHANNEL, frame, transact


def set_relay(action):
    transact(DEFAULT_PORT, DEFAULT_BAUD,
             frame(RELAY_CHANNEL, {"off": 0x00, "on": 0x01}[action]), 1.0)


def assert_app(port_name, settle_seconds):
    time.sleep(settle_seconds)
    with serial.Serial(port_name, 115200, timeout=0.2, write_timeout=1) as port:
        port.reset_input_buffer()
        port.write(b"version\n")
        port.flush()
        deadline = time.monotonic() + 3.0
        received = bytearray()
        while time.monotonic() < deadline:
            received += port.read(256)
            if b"OK version=0.1.0" in received:
                return
    raise AssertionError("app version response missing: %r" % bytes(received))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--cycles", type=int, default=10)
    parser.add_argument("--off-seconds", type=float, default=0.3)
    parser.add_argument("--settle-seconds", type=float, default=2.0)
    args = parser.parse_args()
    if args.cycles < 1:
        raise SystemExit("--cycles must be positive")

    try:
        for cycle in range(1, args.cycles + 1):
            set_relay("off")
            time.sleep(args.off_seconds)
            set_relay("on")
            assert_app(args.port, args.settle_seconds)
            print("PASS cycle_%02u" % cycle)
    finally:
        try:
            set_relay("on")
        except OSError:
            pass


if __name__ == "__main__":
    try:
        main()
    except (AssertionError, OSError, serial.SerialException) as exc:
        print("FAIL %s" % exc, file=sys.stderr)
        raise SystemExit(1)
