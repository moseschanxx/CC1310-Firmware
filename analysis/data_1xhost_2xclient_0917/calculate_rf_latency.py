#!/usr/bin/env python3
"""Analyze TX/RX GPIO rising edges with separate pairing and gap thresholds."""
import argparse
import csv
from pathlib import Path


def rising_edges(rows, column, glitch_filter_s=0.0):
    """Return rising edges, optionally suppressing pulses no wider than a limit."""
    previous, transitions = "0", []
    for row in rows:
        value, time = row[column], float(row["Time [s]"])
        if value == previous:
            continue
        previous = value
        # A transition which returns to the pre-existing level within the
        # filter window is a transient pulse, not an application GPIO edge.
        prior_value = transitions[-2][1] if len(transitions) >= 2 else "0"
        if (glitch_filter_s and transitions and value == prior_value and
                time - transitions[-1][0] <= glitch_filter_s):
            transitions.pop()
        else:
            transitions.append((time, value))
    return [time for time, value in transitions if value == "1"]


def match_receives(tx_edges, rx_edges, max_latency_s):
    """Match each RX edge once, preserving edge order and detecting loss."""
    matches, index, stale = [], 0, 0
    for tx in tx_edges:
        while index < len(rx_edges) and rx_edges[index] <= tx:
            stale += 1
            index += 1
        if index < len(rx_edges) and rx_edges[index] <= tx + max_latency_s:
            matches.append(rx_edges[index])
            index += 1
        else:
            # Do not consume the future edge: it may belong to the next TX.
            matches.append(None)
    return matches, stale, len(rx_edges) - index


def missing_runs(matches):
    runs, start = [], None
    for event, value in enumerate(matches, 1):
        if value is None and start is None:
            start = event
        elif value is not None and start is not None:
            runs.append((start, event - 1))
            start = None
    return runs + ([(start, len(matches))] if start is not None else [])


def long_gaps(rx_edges, threshold_s):
    return {current: current - previous for previous, current in zip(rx_edges, rx_edges[1:])
            if current - previous > threshold_s}


parser = argparse.ArgumentParser(description="Calculate RF latency and RX packet loss from GPIO edges.")
parser.add_argument("input_csv", nargs="?", type=Path, default=Path("Session 0 digital.csv"))
parser.add_argument("-o", "--output", type=Path)
parser.add_argument("--rx-max-latency-ms", type=float, default=15.0,
                    help="maximum TX-to-RX latency permitted when pairing edges (default: %(default)s)")
parser.add_argument("--rx-timeout-ms", "--rx-gap-threshold-ms", dest="rx_gap_threshold_ms",
                    type=float, default=30.0,
                    help="RX inter-arrival gap treated as a possible loss (default: %(default)s)")
parser.add_argument("--tx-period-ms", type=float, default=25.0,
                    help="expected TX interval for RX-gap loss estimate (default: %(default)s)")
parser.add_argument("--glitch-filter-ns", type=float, default=0.0,
                    help="ignore GPIO pulses no wider than this value (default: %(default)s; disabled)")
args = parser.parse_args()
if (args.rx_max_latency_ms <= 0 or args.rx_gap_threshold_ms <= 0 or
        args.tx_period_ms <= 0 or args.glitch_filter_ns < 0):
    parser.error("RX thresholds/TX period must be positive and glitch filter cannot be negative")

source = args.input_csv
destination = args.output or source.with_name(f"{source.stem}_output.csv")
max_latency_s = args.rx_max_latency_ms / 1000
gap_threshold_s, period_s = args.rx_gap_threshold_ms / 1000, args.tx_period_ms / 1000
glitch_filter_s = args.glitch_filter_ns / 1e9
with source.open(newline="") as file:
    rows = list(csv.DictReader(file))
required = {"Time [s]", "Tx", "Rx1", "Rx2"}
if not rows or not required.issubset(rows[0]):
    raise ValueError(f"{source} must contain: {', '.join(sorted(required))}")

tx_edges = rising_edges(rows, "Tx", glitch_filter_s)
rx1_edges = rising_edges(rows, "Rx1", glitch_filter_s)
rx2_edges = rising_edges(rows, "Rx2", glitch_filter_s)
rx1, rx1_stale, rx1_extra = match_receives(tx_edges, rx1_edges, max_latency_s)
rx2, rx2_stale, rx2_extra = match_receives(tx_edges, rx2_edges, max_latency_s)
rx1_gaps, rx2_gaps = long_gaps(rx1_edges, gap_threshold_s), long_gaps(rx2_edges, gap_threshold_s)
lat1, lat2, delta = [], [], []

with destination.open("w", newline="") as file:
    fields = ["event", "T0_tx_rise_s", "T1_rx1_rise_s", "T2_rx2_rise_s", "T1-T0_us",
              "T2-T0_us", "T2-T1_us", "RX1_gap_before_ms", "RX2_gap_before_ms", "status"]
    writer = csv.DictWriter(file, fieldnames=fields, lineterminator="\n")
    writer.writeheader()
    for event, (t0, t1, t2) in enumerate(zip(tx_edges, rx1, rx2), 1):
        if t1 is not None: lat1.append((t1 - t0) * 1e6)
        if t2 is not None: lat2.append((t2 - t0) * 1e6)
        if t1 is not None and t2 is not None: delta.append(abs(t2 - t1) * 1e6)
        status = "ok" if t1 is not None and t2 is not None else ("missing_rx1_rx2" if t1 is None and t2 is None else "missing_rx1" if t1 is None else "missing_rx2")
        writer.writerow({"event": event, "T0_tx_rise_s": f"{t0:.9f}",
            "T1_rx1_rise_s": f"{t1:.9f}" if t1 is not None else "", "T2_rx2_rise_s": f"{t2:.9f}" if t2 is not None else "",
            "T1-T0_us": f"{(t1-t0)*1e6:.3f}" if t1 is not None else "", "T2-T0_us": f"{(t2-t0)*1e6:.3f}" if t2 is not None else "",
            "T2-T1_us": f"{abs(t2-t1)*1e6:.3f}" if t1 is not None and t2 is not None else "",
            "RX1_gap_before_ms": f"{rx1_gaps[t1]*1e3:.3f}" if t1 in rx1_gaps else "", "RX2_gap_before_ms": f"{rx2_gaps[t2]*1e3:.3f}" if t2 in rx2_gaps else "", "status": status})


def receiver_summary(name, matches, gaps, stale, extra):
    missing, count = matches.count(None), len(matches)
    estimated = sum(max(0, round(gap / period_s) - 1) for gap in gaps.values())
    print(f"{name} missing: {missing}/{count} ({missing/count*100 if count else 0:.3f}%); long RX gaps (> {args.rx_gap_threshold_ms:g} ms): {len(gaps)} (estimated {estimated} lost); stale/unmatched: {stale}/{extra}.")
    runs = missing_runs(matches)
    if runs: print(f"{name} consecutive missing event ranges: " + ", ".join(str(a) if a == b else f"{a}-{b}" for a, b in runs))


def latency_summary(label, values):
    print(f"{label} [us]: count={len(values)}" + (f", min={min(values):.3f}, max={max(values):.3f}, mean={sum(values)/len(values):.3f}" if values else " (no valid samples)"))


def tx_interval_summary(edges):
    intervals = [current - previous for previous, current in zip(edges, edges[1:])]
    if not intervals:
        return
    mean_ms = sum(intervals) / len(intervals) * 1e3
    print(f"TX interval [ms]: count={len(intervals)}, min={min(intervals)*1e3:.3f}, max={max(intervals)*1e3:.3f}, mean={mean_ms:.3f}")
    # RX-gap loss interpretation requires the captured TX cadence to match the
    # configured period.  The per-TX matching above remains authoritative.
    if abs(mean_ms - args.tx_period_ms) > 1.0:
        print(f"WARNING: captured TX cadence ({mean_ms:.3f} ms) differs from --tx-period-ms ({args.tx_period_ms:g} ms); RX long-gap estimates are not valid for this capture.")


print(f"Wrote {destination} ({len(tx_edges)} TX events; max pairing latency {args.rx_max_latency_ms:g} ms; RX gap threshold {args.rx_gap_threshold_ms:g} ms; glitch filter {args.glitch_filter_ns:g} ns).")
tx_interval_summary(tx_edges)
receiver_summary("RX1", rx1, rx1_gaps, rx1_stale, rx1_extra)
receiver_summary("RX2", rx2, rx2_gaps, rx2_stale, rx2_extra)
latency_summary("T1-T0", lat1)
latency_summary("T2-T0", lat2)
latency_summary("abs(T2-T1)", delta)
