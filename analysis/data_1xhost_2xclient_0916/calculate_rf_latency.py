#!/usr/bin/env python3
"""Calculate per-transmission GPIO edge latency from a logic-analyzer CSV."""

import csv
import argparse
from pathlib import Path

RECEIVE_TIMEOUT_S = 0.050


def rising_edges(rows, column):
    """Return timestamps for low-to-high transitions in one GPIO column."""
    previous = "0"
    edges = []
    for row in rows:
        value = row[column]
        if previous == "0" and value == "1":
            edges.append(float(row["Time [s]"]))
        previous = value
    return edges


parser = argparse.ArgumentParser(
    description="Calculate TX-to-RX GPIO rising-edge latencies from a logic-analyzer CSV."
)
parser.add_argument(
    "input_csv",
    nargs="?",
    type=Path,
    default=Path("Session 3 digital.csv"),
    help="logic-analyzer CSV to process (default: %(default)s)",
)
parser.add_argument(
    "-o",
    "--output",
    type=Path,
    help="output CSV path (default: <input-stem>_output.csv)",
)
args = parser.parse_args()

source = args.input_csv
destination = args.output or source.with_name(f"{source.stem}_output.csv")

with source.open(newline="") as source_file:
    rows = list(csv.DictReader(source_file))

tx_edges = rising_edges(rows, "Tx")
rx1_edges = rising_edges(rows, "Rx1")
rx2_edges = rising_edges(rows, "Rx2")
latency_t1_t0 = []
latency_t2_t0 = []
latency_abs_t2_t1 = []
missing_rx1 = 0
missing_rx2 = 0

with destination.open("w", newline="") as destination_file:
    fields = [
        "event", "T0_tx_rise_s", "T1_rx1_rise_s", "T2_rx2_rise_s",
        "T1-T0_us", "T2-T0_us", "T2-T1_us", "status",
    ]
    writer = csv.DictWriter(destination_file, fieldnames=fields, lineterminator="\n")
    writer.writeheader()

    for event, t0 in enumerate(tx_edges, start=1):
        # A receive belongs to this TX only if it occurs after T0 and within
        # the 50 ms receive deadline.  This also prevents a late edge from
        # being incorrectly paired with the next transmission.
        deadline = t0 + RECEIVE_TIMEOUT_S
        t1 = next((t for t in rx1_edges if t0 < t <= deadline), None)
        t2 = next((t for t in rx2_edges if t0 < t <= deadline), None)
        if t1 is not None and t2 is not None:
            status = "ok"
        elif t1 is None and t2 is None:
            status = "missing_rx1_rx2"
        elif t1 is None:
            status = "missing_rx1"
        else:
            status = "missing_rx2"

        if t1 is None:
            missing_rx1 += 1
        else:
            latency_t1_t0.append((t1 - t0) * 1_000_000)
        if t2 is None:
            missing_rx2 += 1
        else:
            latency_t2_t0.append((t2 - t0) * 1_000_000)
        if t1 is not None and t2 is not None:
            latency_abs_t2_t1.append(abs(t2 - t1) * 1_000_000)

        writer.writerow({
            "event": event,
            "T0_tx_rise_s": f"{t0:.9f}",
            "T1_rx1_rise_s": f"{t1:.9f}" if t1 is not None else "",
            "T2_rx2_rise_s": f"{t2:.9f}" if t2 is not None else "",
            "T1-T0_us": f"{(t1 - t0) * 1_000_000:.3f}" if t1 is not None else "",
            "T2-T0_us": f"{(t2 - t0) * 1_000_000:.3f}" if t2 is not None else "",
            "T2-T1_us": f"{abs(t2 - t1) * 1_000_000:.3f}" if t1 is not None and t2 is not None else "",
            "status": status,
        })

print(f"Wrote {destination} ({len(tx_edges)} TX events).")
tx_count = len(tx_edges)
rx1_missing_rate = missing_rx1 / tx_count * 100 if tx_count else 0
rx2_missing_rate = missing_rx2 / tx_count * 100 if tx_count else 0
print(
    f"RX1 missing: {missing_rx1} ({rx1_missing_rate:.3f}%); "
    f"RX2 missing: {missing_rx2} ({rx2_missing_rate:.3f}%); "
    f"both received: {len(latency_abs_t2_t1)}."
)

for label, values in (
    ("T1-T0", latency_t1_t0),
    ("T2-T0", latency_t2_t0),
    ("abs(T2-T1)", latency_abs_t2_t1),
):
    if values:
        print(
            f"{label} [us]: count={len(values)}, min={min(values):.3f}, "
            f"max={max(values):.3f}, mean={sum(values) / len(values):.3f}"
        )
    else:
        print(f"{label} [us]: count=0 (no valid samples)")
