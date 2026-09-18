#!/usr/bin/env python3
"""Continuously exercise the ``tx sync_*`` UART commands."""
import argparse
import signal
import sys
import time

try:
    import serial
except ImportError as exc:
    raise SystemExit("pyserial is required: python3 -m pip install pyserial") from exc


DEFAULT_PORT = "/dev/cu.usbserial-1140"
DEFAULT_INTERVAL_MS = 10.0
DEFAULT_EXPOSURE_US = 0x000186A0
TX_CLI_COMMAND = "tx"


def read_response(port, command, timeout):
    """Read complete CR/LF-delimited lines until this command's response."""
    deadline = time.monotonic() + timeout
    received = bytearray()
    expected = "OK " + command
    while time.monotonic() < deadline:
        # Do not request an arbitrary large read: pyserial waits for its
        # timeout when the short CLI reply cannot fill that request. Reading
        # only bytes already buffered keeps the command interval responsive.
        available = port.in_waiting
        if available == 0:
            time.sleep(0.0005)
            continue
        received.extend(port.read(available))
        while b"\n" in received:
            raw_line, _, remainder = received.partition(b"\n")
            received = bytearray(remainder)
            line = raw_line.rstrip(b"\r").decode("ascii", errors="replace")
            if line.startswith(expected):
                return line
            if line.startswith("ERR "):
                raise RuntimeError(line)
    raise TimeoutError("no response to %s within %.3fs" % (command, timeout))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", default=DEFAULT_PORT,
                        help="TX CLI serial port (default: %(default)s)")
    parser.add_argument("--interval-ms", type=float, default=DEFAULT_INTERVAL_MS,
                        help="minimum interval between command starts (default: %(default)s)")
    parser.add_argument("--count", type=int, default=0,
                        help="number of commands; 0 runs until Ctrl-C (default: %(default)s)")
    parser.add_argument("--command", choices=("sync_frame", "sync_time", "both"),
                        default="sync_frame",
                        help="command to send; both alternates time then frame (default: %(default)s)")
    parser.add_argument("--exposure-us", type=lambda value: int(value, 0),
                        default=DEFAULT_EXPOSURE_US,
                        help="base sync_frame exposure value, decimal or 0x-prefixed hex")
    parser.add_argument("--response-timeout", type=float, default=1.0,
                        help="seconds to wait for each CLI response (default: %(default)s)")
    args = parser.parse_args()

    if args.interval_ms < 0:
        parser.error("--interval-ms must be non-negative")
    if args.count < 0:
        parser.error("--count must be non-negative")
    if not 0 <= args.exposure_us <= 0xFFFFFFFF:
        parser.error("--exposure-us must fit uint32")
    if args.response_timeout <= 0:
        parser.error("--response-timeout must be positive")

    stop_requested = False

    def request_stop(_signal, _frame):
        nonlocal stop_requested
        stop_requested = True

    signal.signal(signal.SIGINT, request_stop)
    interval = args.interval_ms / 1000.0
    sent = 0
    failures = 0
    started = time.monotonic()
    next_start = started

    with serial.Serial(args.port, 115200, timeout=0.02, write_timeout=1) as port:
        # Discard a possible startup banner or stale response before the test.
        port.reset_input_buffer()
        while not stop_requested and (args.count == 0 or sent < args.count):
            now = time.monotonic()
            if now < next_start:
                time.sleep(next_start - now)
            is_time = args.command == "sync_time" or (
                args.command == "both" and (sent % 2) == 0)
            command = "sync_time" if is_time else "sync_frame"
            if is_time:
                # UTC microseconds fit uint64 and change on every invocation.
                value = time.time_ns() // 1000
                argument = "%016X" % value
            else:
                # Vary the low word to make each test frame distinguishable.
                value = (args.exposure_us + sent // 2) & 0xFFFFFFFF
                argument = "%08X" % value

            try:
                # TX commands are now dispatched through the common CLI as
                # ``tx sync_time <value>`` or ``tx sync_frame <value>``.
                port.write((TX_CLI_COMMAND + " " + command + " " + argument + "\n").encode("ascii"))
                port.flush()
                response = read_response(port, command, args.response_timeout)
                sent += 1
                print("PASS n=%u command=%s value=%s %s" %
                      (sent, command, argument, response), flush=True)
            except (OSError, RuntimeError, TimeoutError, serial.SerialException) as exc:
                failures += 1
                print("FAIL n=%u command=%s value=%s: %s" %
                      (sent + 1, command, argument, exc), file=sys.stderr, flush=True)
                raise SystemExit(1)
            next_start += interval
            if next_start < time.monotonic():
                next_start = time.monotonic()

    elapsed = time.monotonic() - started
    print("COMPLETE commands=%u failures=%u elapsed=%.3fs" %
          (sent, failures, elapsed))


if __name__ == "__main__":
    main()
