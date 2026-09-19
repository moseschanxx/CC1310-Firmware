# Host to CC1310 TX/RX Data Flow and GPIO Timing

## Scope and assumptions

This document describes the current sync-frame link: the host controls one CC1310 TX over UART, the TX
broadcasts over the 433 MHz proprietary PHY, and RX1 and RX2 each receive and drive a measurement GPIO. Both
the TX and RX UART0 run at 115200 baud, 8N1, no hardware flow control; the CC1310 LaunchPad UART0 pins are
DIO2 (RXD) and DIO3 (TXD).

```text
Host test tool / terminal
        | UART CLI: sync_time / sync_frame
        v
CC1310 TX: parse -> build 30-byte app frame -> RF_runCmd
        | 433 MHz, 50 kBaud, -10 dBm
        +---------------------------> CC1310 RX1: RF queue -> GPIO -> optional UART dump
        |
        +---------------------------> CC1310 RX2: RF queue -> GPIO -> optional UART dump
```

## Host and TX

The host sends one ASCII CLI command per line, terminated with CR, LF or CRLF:

```text
sync_time 0123456789ABCDEF    # uint64 UTC, hexadecimal
sync_frame 000186A0           # uint32 exposure time, hexadecimal
```

`tools/tx_sync_stress.py` sends `sync_frame` continuously by default; `--command sync_time` sends only time
packets, and `--command both` alternates. The script waits for the TX's `OK <command> seq=<n>`, so the
specified `--interval-ms` is the target minimum start interval; the actual period is further bounded by the
UART round trip and the synchronous RF transmit time.

The TX encodes the command into a fixed 30-byte application frame:

| Offset | Field | Bytes | Encoding |
| --- | --- | --- | --- |
| 0–1 | magic | 2 | `0x5359`, big-endian |
| 2 | type | 1 | time `0x01`; frame `0x02` |
| 3 | payload length | 1 | time 8; frame 4 |
| 4–5 | site ID | 2 | currently `0x0000`, big-endian |
| 6 | sequence | 1 | incremented on every send, wraps naturally |
| 7 | session ID | 1 | currently `0x00` |
| 8–27 | payload + reserved | 20 | filled with pseudo-random bytes after the payload |
| 28–29 | CRC16 | 2 | CRC-16/CCITT-FALSE, big-endian |

The RF uses a variable-length PHY; `CMD_PROP_TX.pktLen=30` supplies the length to the RF Core, and the
application array contains **no** extra leading length byte. The PHY is configured for 50 kBaud, a 30-byte
maximum packet and −10 dBm.

## RX data handling

The RX runs `CMD_PROP_RX` continuously, accepting up to a 30-byte payload. The RF Core triggers the callback
for entries with a correct CRC; the firmware records the RSSI, takes a SYS/BIOS tick timestamp, and copies the
payload into a 32-deep software mailbox. With `rx dump on`, a separate print task outputs:

```text
RX seq=<local queue sequence> tick=<local tick> len=<payload length> data=<hex>
```

The `rx`, `enq`, `drop`, `full`, `crc` and `coll` fields of `rx status` distinguish RF reception, application
queue, RX data-entry buffer, CRC and collision events. High-rate tests should turn dump off so that UART
printing itself does not affect queue consumption.

## GPIO measurement points

All measurement GPIOs are **DIO1 / IOID_1**, push-pull output, maximum drive strength, idle low. Each board's
DIO1 is an independent output; connect it only to the corresponding logic analyzer channel with a common
ground, and **do not wire the TX and RX DIO1 pins directly together**.

| Device | Rising edge | Falling edge | Meaning |
| --- | --- | --- | --- |
| TX | Before calling `RF_runCmd(CMD_PROP_TX)` | After that call returns | Covers software submission to the RF Core, waiting and transmission of the whole packet; not the exact instant of the first on-air bit. |
| RX1/RX2 | After receiving a CRC-correct entry, before copying into the software queue | After posting to the software queue | Marks the RX callback processing point; not the exact instant of sync-word detection or the first bit. |

The TX→RX GPIO latency therefore includes the TX RF command start-up, on-air transmission, receive
completion, RF callback scheduling and a small amount of queue posting time; it excludes the Host→UART time
and must not be interpreted as pure on-air propagation delay. The RX1/RX2 edge difference reflects the relative
timing difference between the two receive/callback paths.

## 1-hour periodic test results

`Session 3.sal` was captured at 100 MS/s. Before analysis, a 20 ns glitch filter was applied to the exported
digital edges: at `617.880564480–617.880564490 s`, Tx showed a 10 ns low pulse while Rx1 showed a 10 ns high
pulse at the same time. That width is exactly one sample period, and the two channels are inverted and
synchronous, which does not match what the firmware's GPIO operations can do; it was judged a
capture/signal-integrity glitch, and that spurious edge is not counted as a TX or RX event.

The analysis rules are: TX target period 20 ms; maximum TX→RX edge pairing latency 15 ms; an interval between
consecutive RX receptions longer than 25 ms is flagged as a long gap. The pairing window and the long-gap
threshold are separate, to avoid pairing the RX edge of the next packet with the previous TX when the TX period
is abnormal.

| Item | Result |
| --- | --- |
| Valid TX count / capture duration | 180,000 packets / 3,600.065 s (about 1 hour) |
| Actual average send rate | 49.999 Hz |
| TX interval | mean 20.000 ms; P50/P95/P99 19.983/20.883/21.184 ms; range 14.637–32.098 ms |
| TX cadence anomalies | 10 below 15 ms; 16 above 25 ms |
| RX1 reception | 179,231/180,000 received; 769 packets lost, loss rate **0.427%** |
| RX1 consecutive losses | 743 runs: 726 of one packet, 12 of 2, 3 of 3, 1 of 4, 1 of 6 (longest) |
| RX2 reception | 180,000/180,000 received; no loss detected |
| TX→RX1 GPIO latency | mean 7,289.063 µs; P50/P95/P99 7,287.910/7,290.970/7,354.390 µs; range 7,230.880–7,364.270 µs |
| TX→RX2 GPIO latency | mean 7,289.226 µs; P50/P95/P99 7,288.070/7,291.120/7,354.620 µs; range 7,231.230–7,364.160 µs |
| RX1/RX2 relative time difference | mean 5.544 µs; P50/P95/P99 1.870/38.890/68.390 µs; max 123.180 µs |

RX1 had 759 receive gaps over 25 ms (mean 40.395 ms, max 139.324 ms), consistent with its loss statistics.
RX2 had 16 gaps over 25 ms, but every per-TX pairing succeeded; those gaps match the TX's own cadence jitter
and cannot be attributed to RX2 packet loss.

Conclusion: after glitch filtering, the GPIO latency of both normally receiving paths stays at about 7.29 ms,
and RX2 lost no packets over the full 1-hour test; the losses are concentrated on RX1.
The RX1 loss has been confirmed to be power related: RX1 had no battery attached and less stable power; RX2 had
a battery, more stable power, and no loss in this round.
When capturing, enable a digital glitch filter of at least 20 ns, or move Tx/Rx1 to non-adjacent capture
channels to verify crosstalk.
