#!/usr/bin/env python3
"""Match RF TX/RX console logs and record host-observed latency to CSV.

The timestamp is taken when this program receives a complete log line from
each UART.  Thus it includes any UART/logging/buffering delay; it is not an
on-air RF latency measurement unless the devices print their own timestamps.
"""

import argparse
import csv
import re
import sys
import threading
import time
from collections import defaultdict, deque
from datetime import datetime
from pathlib import Path

try:
    import serial
except ImportError:  # Show an actionable message instead of a traceback.
    print("Missing dependency: install it with `python3 -m pip install -r requirements.txt`", file=sys.stderr)
    raise SystemExit(2)
from serial.tools import list_ports


TX_RE = re.compile(
    r"\bTX:\s*(?:t_tick=(\d+)\s+t_us=(\d+)\s+data=|tick=(\d+)\s+data=)?"
    r"((?:[0-9A-Fa-f]{2}\s*)+)"
)
RX_RE = re.compile(
    r"\bRX\s+seq=(\d+)(?:\s+t_tick=(\d+)\s+t_us=(\d+)|\s+tick=(\d+))?"
    r"\s+len=(\d+)\s+data=([0-9A-Fa-f]+)"
)
CSV_FIELDS = (
    "tx_time", "rx_time", "tx_time_ns", "rx_time_ns", "latency_us", "latency_ms",
    "tx_port", "rx_port", "tx_local_tick", "tx_local_us", "rx_local_tick",
    "rx_local_us", "rx_seq", "data_hex",
)
WORLD_FIELDS = (
    "tx_world_time", "rx_world_time", "tx_world_time_ns", "rx_world_time_ns",
    "physical_latency_us", "physical_latency_ms", "tx_ns_per_tick", "rx_ns_per_tick",
)


def iso_time(ns: int) -> str:
    return datetime.fromtimestamp(ns / 1_000_000_000).astimezone().isoformat(timespec="microseconds")


def build_clock_models(rows, port_field: str, tick_field: str, host_ns_field: str):
    """Map each device's wrapping 32-bit tick clock to host wall-clock nanoseconds.

    The first and last observed timestamp for each port define the model, as
    requested.  A wrap is recognized only for a backwards jump larger than
    half the 32-bit range, so small out-of-order host deliveries do not wrap.
    """
    grouped = defaultdict(list)
    for index, row in enumerate(rows):
        if row.get(port_field) and row.get(tick_field) and row.get(host_ns_field):
            grouped[row[port_field]].append((int(row[host_ns_field]), int(row[tick_field]), index))

    models = {}
    unwrapped = {}
    for port, samples in grouped.items():
        samples.sort()
        epoch, previous_raw = 0, None
        converted = []
        for host_ns, raw_tick, index in samples:
            if previous_raw is not None and raw_tick < previous_raw and previous_raw - raw_tick > (1 << 31):
                epoch += 1 << 32
            previous_raw = raw_tick
            tick = epoch + raw_tick
            converted.append((host_ns, tick, index))
            unwrapped[index] = tick
        first_host_ns, first_tick, _ = converted[0]
        last_host_ns, last_tick, _ = converted[-1]
        if last_tick != first_tick:
            models[port] = (first_host_ns, first_tick,
                            (last_host_ns - first_host_ns) / (last_tick - first_tick))
    return models, unwrapped


def generate_world_csv(source: Path) -> Path | None:
    """Create a calibrated CSV with device ticks converted to host/world time."""
    with source.open(newline="", encoding="utf-8") as file:
        rows = list(csv.DictReader(file))
    if not rows:
        return None

    tx_models, tx_ticks = build_clock_models(rows, "tx_port", "tx_local_tick", "tx_time_ns")
    rx_models, rx_ticks = build_clock_models(rows, "rx_port", "rx_local_tick", "rx_time_ns")
    destination = source.with_name(source.stem + "_world.csv")
    with destination.open("w", newline="", encoding="utf-8") as file:
        writer = csv.DictWriter(file, fieldnames=CSV_FIELDS + WORLD_FIELDS)
        writer.writeheader()
        for index, row in enumerate(rows):
            output = dict(row)
            tx_model = tx_models.get(row.get("tx_port"))
            rx_model = rx_models.get(row.get("rx_port"))
            tx_world_ns = None
            rx_world_ns = None
            if tx_model and index in tx_ticks:
                base_ns, base_tick, ns_per_tick = tx_model
                tx_world_ns = round(base_ns + (tx_ticks[index] - base_tick) * ns_per_tick)
                output["tx_world_time"] = iso_time(tx_world_ns)
                output["tx_world_time_ns"] = tx_world_ns
                output["tx_ns_per_tick"] = f"{ns_per_tick:.9f}"
            if rx_model and index in rx_ticks:
                base_ns, base_tick, ns_per_tick = rx_model
                rx_world_ns = round(base_ns + (rx_ticks[index] - base_tick) * ns_per_tick)
                output["rx_world_time"] = iso_time(rx_world_ns)
                output["rx_world_time_ns"] = rx_world_ns
                output["rx_ns_per_tick"] = f"{ns_per_tick:.9f}"
            if tx_world_ns is not None and rx_world_ns is not None:
                physical_ns = rx_world_ns - tx_world_ns
                output["physical_latency_us"] = f"{physical_ns / 1_000:.3f}"
                output["physical_latency_ms"] = f"{physical_ns / 1_000_000:.6f}"
            writer.writerow(output)
    return destination


class Matcher:
    def __init__(self, writer: csv.DictWriter, output_file, lock: threading.Lock):
        self.pending_tx = defaultdict(deque)
        self.pending_rx = defaultdict(deque)
        self.writer = writer
        self.output_file = output_file
        self.lock = lock
        self.total_tx = 0
        self.total_rx = 0
        self.matched = 0
        self.lost = 0
        self.latency_sum_ns = 0
        self.latency_min_ns = None
        self.latency_max_ns = None

    def tx(self, data: bytes, timestamp_ns: int, device: str, tick, local_us) -> None:
        with self.lock:
            self.total_tx += 1
            received = self.pending_rx.get(data)
            if received:
                seq, rx_ns, rx_device, rx_tick, rx_local_us = received.popleft()
                if not received:
                    del self.pending_rx[data]
                self._write(data, timestamp_ns, device, tick, local_us, seq, rx_ns, rx_device, rx_tick, rx_local_us)
                return
            self.pending_tx[data].append((timestamp_ns, device, tick, local_us))

    def rx(self, seq: int, data: bytes, timestamp_ns: int, device: str, tick, local_us) -> None:
        with self.lock:
            self.total_rx += 1
            timestamps = self.pending_tx.get(data)
            if not timestamps:
                # Keep it: host serial scheduling can occasionally make RX arrive first.
                self.pending_rx[data].append((seq, timestamp_ns, device, tick, local_us))
                return
            tx_ns, tx_device, tx_tick, tx_local_us = timestamps.popleft()
            if not timestamps:
                del self.pending_tx[data]
            self._write(data, tx_ns, tx_device, tx_tick, tx_local_us, seq, timestamp_ns, device, tick, local_us)

    def _write(self, data: bytes, tx_ns: int, tx_device: str, tx_tick, tx_local_us,
               seq: int, rx_ns: int, rx_device: str, rx_tick, rx_local_us) -> None:
        latency_ns = rx_ns - tx_ns
        self.writer.writerow({
            "tx_time": iso_time(tx_ns),
            "rx_time": iso_time(rx_ns),
            "tx_time_ns": tx_ns,
            "rx_time_ns": rx_ns,
            "latency_us": f"{latency_ns / 1_000:.3f}",
            "latency_ms": f"{latency_ns / 1_000_000:.6f}",
            "tx_port": tx_device,
            "rx_port": rx_device,
            "tx_local_tick": "" if tx_tick is None else tx_tick,
            "tx_local_us": "" if tx_local_us is None else tx_local_us,
            "rx_local_tick": "" if rx_tick is None else rx_tick,
            "rx_local_us": "" if rx_local_us is None else rx_local_us,
            "rx_seq": seq,
            "data_hex": data.hex().upper(),
        })
        self.matched += 1
        self.latency_sum_ns += latency_ns
        self.latency_min_ns = latency_ns if self.latency_min_ns is None else min(self.latency_min_ns, latency_ns)
        self.latency_max_ns = latency_ns if self.latency_max_ns is None else max(self.latency_max_ns, latency_ns)
        self.output_file.flush()
        print(f"seq={seq} latency={latency_ns / 1_000_000:.3f} ms")

    def expire(self, now_ns: int, timeout_ns: int = 1_000_000_000, force: bool = False) -> None:
        """Discard TX entries that can no longer receive a valid RX match."""
        with self.lock:
            for data, entries in list(self.pending_tx.items()):
                while entries and (force or now_ns - entries[0][0] > timeout_ns):
                    tx_ns, device, _tick, _local_us = entries.popleft()
                    self.lost += 1
                    reason = "stopped before RX" if force else "RX timeout > 1.000 s"
                    print(f"LOST TX: {reason}; port={device} data={data.hex().upper()}", file=sys.stderr)
                if not entries:
                    del self.pending_tx[data]

    def summary(self) -> str:
        with self.lock:
            pending_rx = sum(len(entries) for entries in self.pending_rx.values())
            loss_rate = (self.lost / self.total_tx * 100) if self.total_tx else 0.0
            lines = [
                "--- RF latency summary ---",
                f"valid TX: {self.total_tx}",
                f"valid RX: {self.total_rx}",
                f"matched: {self.matched}",
                f"lost TX: {self.lost} ({loss_rate:.3f}%)",
                f"RX without matching TX: {pending_rx}",
            ]
            if self.matched:
                lines.extend((
                    f"latency min: {self.latency_min_ns / 1_000_000:.3f} ms",
                    f"latency avg: {self.latency_sum_ns / self.matched / 1_000_000:.3f} ms",
                    f"latency max: {self.latency_max_ns / 1_000_000:.3f} ms",
                ))
            return "\n".join(lines)


class PortRoles:
    """Assign ports from their observed log format, not their device names."""
    def __init__(self):
        self.lock = threading.Lock()
        self.tx_device = None
        self.rx_devices = set()

    def accept(self, device: str, kind: str) -> bool:
        with self.lock:
            if kind == "TX":
                if self.tx_device is None:
                    self.tx_device = device
                    print(f"identified TX: {device}", file=sys.stderr)
                if self.tx_device != device:
                    print(f"ignoring TX log from {device}; TX is {self.tx_device}", file=sys.stderr)
                    return False
                return True
            if device == self.tx_device:
                print(f"ignoring RX log from TX port {device}", file=sys.stderr)
                return False
            if device not in self.rx_devices:
                self.rx_devices.add(device)
                print(f"identified RX: {device}", file=sys.stderr)
            return True


def read_uart(device: str, baudrate: int, matcher: Matcher, roles: PortRoles, stop: threading.Event) -> None:
    try:
        with serial.Serial(device, baudrate=baudrate, timeout=0.5) as uart:
            print(f"scanning: {device} @ {baudrate}", file=sys.stderr)
            while not stop.is_set():
                raw = uart.readline()
                if not raw:
                    continue
                received_ns = time.time_ns()
                line = raw.decode("ascii", errors="replace").strip()
                match = TX_RE.search(line)
                if match:
                    if not roles.accept(device, "TX"):
                        continue
                    try:
                        data = bytes.fromhex(match.group(4))
                    except ValueError:
                        continue
                    if len(data) == 30:
                        matcher.tx(data, received_ns, device,
                                   int(match.group(1) or match.group(3)) if (match.group(1) or match.group(3)) else None,
                                   int(match.group(2)) if match.group(2) else None)
                    else:
                        print(f"ignored TX with {len(data)} bytes", file=sys.stderr)
                    continue
                match = RX_RE.search(line)
                if match:
                    if not roles.accept(device, "RX"):
                        continue
                    seq, length, encoded = int(match.group(1)), int(match.group(5)), match.group(6)
                    try:
                        data = bytes.fromhex(encoded)
                    except ValueError:
                        continue
                    if length == 30 and len(data) == 30:
                        matcher.rx(seq, data, received_ns, device,
                                   int(match.group(2) or match.group(4)) if (match.group(2) or match.group(4)) else None,
                                   int(match.group(3)) if match.group(3) else None)
                    else:
                        print(f"ignored RX seq={seq}: declared={length}, actual={len(data)}", file=sys.stderr)
    except serial.SerialException as exc:
        print(f"serial error on {device}: {exc}", file=sys.stderr)


def usbserial_devices() -> list[str]:
    """Return only macOS cu.usbserial devices, including newly plugged ones."""
    return sorted(port.device for port in list_ports.comports()
                  if port.device.startswith("/dev/cu.usbserial"))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--output-dir", type=Path, default=Path("."))
    parser.add_argument("--scan-interval", type=float, default=2.0,
                        help="seconds between scans for newly connected USB serial ports (default: 2)")
    args = parser.parse_args()

    args.output_dir.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    output = args.output_dir / f"rf_latency_{stamp}.csv"
    stop, lock, roles = threading.Event(), threading.Lock(), PortRoles()
    with output.open("w", newline="", encoding="utf-8") as file:
        writer = csv.DictWriter(file, fieldnames=CSV_FIELDS)
        writer.writeheader()
        file.flush()
        matcher = Matcher(writer, file, lock)
        print(f"writing matches to {output.resolve()}", file=sys.stderr)
        print("waiting for /dev/cu.usbserial* ports and TX:/RX logs...", file=sys.stderr)
        threads, started = [], set()
        next_scan_ns = 0
        try:
            while not stop.is_set():
                now_ns = time.time_ns()
                if now_ns >= next_scan_ns:
                    for device in usbserial_devices():
                        if device in started:
                            continue
                        started.add(device)
                        thread = threading.Thread(target=read_uart,
                                                  args=(device, args.baud, matcher, roles, stop), daemon=True)
                        threads.append(thread)
                        thread.start()
                    next_scan_ns = now_ns + int(args.scan_interval * 1_000_000_000)
                matcher.expire(now_ns)
                stop.wait(0.1)
        except KeyboardInterrupt:
            print("stopping...", file=sys.stderr)
            stop.set()
        for thread in threads:
            thread.join(timeout=1)
        matcher.expire(time.time_ns(), force=True)
    world_output = generate_world_csv(output)
    print(f"matched rows: {matcher.matched}; CSV: {output.resolve()}", file=sys.stderr)
    if world_output:
        print(f"calibrated world-time CSV: {world_output.resolve()}", file=sys.stderr)
    print(matcher.summary(), file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
