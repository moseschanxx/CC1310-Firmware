#!/usr/bin/env python3
"""Monitor the RX app CLI during a sustained runtime interval."""
import argparse
import sys
import time

try:
    import serial
except ImportError as exc:
    raise SystemExit("pyserial is required") from exc


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", required=True)
    parser.add_argument("--duration", type=float, default=1800)
    parser.add_argument("--interval", type=float, default=30)
    args = parser.parse_args()
    deadline = time.monotonic() + args.duration
    sample = 0
    with serial.Serial(args.port, 115200, timeout=.2, write_timeout=1) as port:
        while time.monotonic() < deadline:
            sample += 1
            port.reset_input_buffer()
            port.write(b"version\nrx status\n"); port.flush()
            response = bytearray()
            response_deadline = time.monotonic() + 3
            while time.monotonic() < response_deadline:
                response += port.read(256)
                if b"OK version=0.1.0" in response and b"OK rx=" in response:
                    break
            else:
                raise AssertionError("sample %u incomplete response: %r" % (sample, bytes(response)))
            print("PASS sample=%u elapsed=%us" % (sample, int(args.duration - (deadline - time.monotonic()))), flush=True)
            remaining = deadline - time.monotonic()
            if remaining > 0:
                time.sleep(min(args.interval, remaining))
    print("PASS stability duration=%us samples=%u" % (int(args.duration), sample))


if __name__ == "__main__":
    try:
        main()
    except (AssertionError, serial.SerialException) as exc:
        print("FAIL %s" % exc, file=sys.stderr)
        raise SystemExit(1)
