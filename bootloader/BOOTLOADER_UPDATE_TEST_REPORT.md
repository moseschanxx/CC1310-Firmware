# CC1310 RX Bootloader Update Test Report

**Report date:** 2026-09-17  
**Items under test:** CC1310F128 RX bootloader, non-ROM boot app, Python update tools  
**DUT serial port:** `/dev/cu.usbserial-A50285BI`, 115200 8N1  
**App:** package version v110; product version `0.1.0`  
**Update package:** `rfPacketRx/boot_build/nonrom_test/rfPacketRx_boot_nonrom.pkg`  
**Package attributes:** target `0x43433152`, size 51,352 bytes, CRC32 `9FD0EE5C`

## 1. Summary of conclusions

As of this report, the bootloader's normal update path, the app's request to enter update mode, basic
package/frame validation and part of the protocol error recovery have all passed on real RX hardware. All 14
host-side package format and protocol encode/decode unit tests passed. During testing it was found that the
Python COBS decoder accepted truncated blocks; this has been fixed and is covered by a test. The newly executed
destructive transfer, power-loss recovery and 10 routine power-cycle reboot cases all passed.

The device has been restored and is running v110, with CLI `version`, `help` and `rx status` working normally.
Destructive tests such as deliberate power cuts, metadata full-page rotation, the crash-count threshold and
J-Link corruption injection have not yet been executed; these are not passed items and still need to be run
separately according to the test design.

## 2. Test environment and build results

| Item | Result |
| --- | --- |
| Bootloader | CC1310 NoRTOS bootloader, app slot `0x8000–0x1EFFF` |
| App | RX non-ROM SYS/BIOS boot app |
| Package format | 32-byte CRC32 header + app image CRC32 |
| UART protocol | COBS frames + CRC16/CCITT-FALSE, 128-byte DATA chunks |
| Power control | `tools/relay_control.py` available |
| Debug interface | J-Link cJTAG available; this round of protocol testing did not rely on it for flash writes |

Build and package verification commands:

```bash
bash rfPacketRx/build_boot_nonrom_test.sh 110
python3 tools/fw_package.py --verify \
  rfPacketRx/boot_build/nonrom_test/rfPacketRx_boot_nonrom.pkg
```

Result: build succeeded, package verification succeeded, output `target=0x43433152 version=110`
`size=51352 crc32=9FD0EE5C`.

## 3. Host-side automated tests

Commands executed:

```bash
python3 -m unittest discover -s tools/tests -v
python3 -m py_compile tools/fw_package.py tools/fw_protocol.py \
  tools/fw_update.py tools/raw_update_smoke.py
```

Result: **14/14 passed**.

| Module | Coverage | Result |
| --- | --- | --- |
| `test_fw_package.py` | round-trip, 4-byte padding, empty image, oversized image, header CRC32, image CRC32, package length, magic/format/header size/load address/size | 6/6 passed |
| `test_fw_protocol.py` | COBS round-trip, zero bytes and long runs of data, empty/128-byte payload, CRC16 standard vector, bad-CRC frame, wrong protocol version, wrong length, truncated COBS | 6/6 passed |

New package tests cover the 4-byte minimum legal image, the `0x17000` maximum legal image and rejection of
appended package bytes.

### 3.1 Issue found and fixed

On the first run, the truncated-COBS-block check in `tools/fw_protocol.py:cobs_decode()` used an overly loose
upper bound, so inputs such as `b"\x02"` and `b"\x03\x01"` were silently truncated instead of rejected. That
implementation was inconsistent with the bounds check on the bootloader C side.

It was fixed to check for the full number of bytes required by the code byte:

```python
if code == 0 or index + code > len(data):
    raise ValueError("invalid COBS frame")
```

After the fix all 12 tests passed.

## 4. Real-hardware end-to-end tests

### 4.1 Normal app → bootloader → app

| ID | Steps | Result |
| --- | --- | --- |
| BL-N-01 | Query `version` and `help` after app boot | Passed; returns `OK version=0.1.0`, help lists rx, bootloader, version. |
| BL-N-02 | Enter `bootloader` on the app | Passed; returns `OK rebooting_to_bootloader`. |
| BL-N-02 | `fw_update.py ... info` | Passed; `target=1128477010` (i.e. `0x43433152`), `state=2`, `max_size=94208`. |
| BL-N-03 | `fw_update.py ... flash --package ...pkg` | Passed; 51,352 bytes from 0% to 100%, ending with `COMPLETE`. |
| BL-N-04 | Query `version`, `help`, `rx status` after the update | Passed; the app came back automatically and all commands work. |

Final CLI output during the hardware test:

```text
OK version=0.1.0
OK commands:
OK help [command]
OK rx rx status | rx dump on|off
OK bootloader reboot into UART firmware updater
OK version show firmware version
OK rx=0 enq=0 drop=0 full=0 crc=0 coll=0 rssi=-128[127,-128] samples=0 dump=off
```

### 4.2 Non-destructive raw protocol smoke test

Added and executed:

```bash
python3 tools/raw_update_smoke.py --port /dev/cu.usbserial-A50285BI
```

The script sends only malformed frames or `BEGIN` requests that are guaranteed invalid, never a valid header,
so it should not erase or write the app slot. After each item it resends HELLO and compares the INFO payload to
confirm that the invalid request did not change the metadata.

| Test item | Expected | Actual result |
| --- | --- | --- |
| HELLO | Returns INFO with the RX target | Passed |
| Truncated COBS block | Bad frame dropped, subsequent HELLO works | Passed |
| Wrong CRC16 | Bad frame dropped, subsequent HELLO works | Passed |
| Wrong magic | BEGIN returns ERROR, INFO unchanged | Passed |
| Wrong format version | ERROR, INFO unchanged | Passed |
| Wrong header size | ERROR, INFO unchanged | Passed |
| Wrong target ID | ERROR, INFO unchanged | Passed |
| Wrong app address | ERROR, INFO unchanged | Passed |
| imageSize=0 | ERROR, INFO unchanged | Passed |
| Oversized imageSize | ERROR, INFO unchanged | Passed |
| Unaligned imageSize | ERROR, INFO unchanged | Passed |
| Wrong header CRC32 | ERROR, INFO unchanged | Passed |
| DATA without BEGIN | ERROR, subsequent HELLO works | Passed |

Summary of the run:

```text
PASS hello target=0x43433152 state=2
PASS malformed_cobs_recovered
PASS bad_crc16_recovered
PASS invalid_begin_magic_rejected
PASS invalid_begin_format_version_rejected
PASS invalid_begin_header_size_rejected
PASS invalid_begin_target_rejected
PASS invalid_begin_address_rejected
PASS invalid_begin_zero_size_rejected
PASS invalid_begin_oversized_rejected
PASS invalid_begin_unaligned_size_rejected
PASS invalid_begin_header_crc_rejected
PASS data_without_begin_rejected
```

After this test the device was updated again with the v110 package and returned to app mode successfully.

## 5. Anomalies during the process and their handling

During the first hardware smoke run, the host serial port raised a `SerialException` after the app switched to
the bootloader. Investigation showed that `/dev/cu.usbserial-A50285BI` was held by the user's `picocom`
process. Once that process released it, `INFO`, the raw smoke test and the full update all succeeded. This was
a test-environment resource conflict, not a bootloader protocol or flash update fault.

Another observation: if the app receives the bootloader's binary HELLO data without actually entering the
bootloader, those leftover bytes may be concatenated with subsequent ASCII CLI commands. In testing, sending an
empty line `\n` before app commands to clear the line buffer made entering update mode reliable. This behaviour
is recorded as a UART switching risk in the test design, and the UART flush strategy on bootloader/app
switching should be evaluated later.

## 6. Additional automation results from this round

### 6.1 Transfer transactions: out-of-order, duplicate and image CRC

Destructive script executed:

```bash
python3 tools/raw_update_transaction_test.py \
  --port /dev/cu.usbserial-A50285BI \
  --package rfPacketRx/boot_build/nonrom_test/rfPacketRx_boot_nonrom.pkg
```

After a valid `BEGIN`, the script deliberately sends a future offset, resends the first chunk at offset 0, then
flips one bit in the second 128-byte DATA chunk and sends through to END. Actual result:

```text
PASS begin_ready
PASS future_offset_nack
PASS first_data_ack
PASS duplicate_data_ack
PASS corrupted_image_transferred
PASS end_crc_failure_rejected
```

This shows the bootloader does not allow skipping unwritten regions, duplicate chunks get an ACK for the current
offset, and the update is accepted only when the full-image CRC32 at END matches. This case erases the app
slot; immediately afterwards a full update with the v110 `.pkg` returned `COMPLETE`, and the app's `version`
and `rx status` were normal.

### 6.2 Real power-loss recovery after BEGIN

Added `tools/raw_update_power_loss_test.py`: after verifying the local package it sends a valid BEGIN, and on
receiving READY it cuts power via the relay for 1 s, powers back on, then sends HELLO. Result:

```text
PASS begin_ready
PASS reset_after_begin_stays_in_update_mode
```

The INFO state after recovery is `3` (`UPDATE_IN_PROGRESS`); the bootloader did not execute the erased app and
accepted the v110 package to complete recovery. This covers the "metadata committed, erase just started" path of
BL-R-02/BL-R-03; the exact power-cut windows inside FlashProgram and inside each metadata record write
instruction are not yet covered.

### 6.3 Sequence wraparound and input regression

`raw_update_smoke.py` was extended to check the echo and INFO consistency for HELLO sequences `0, 65535, 0`,
then rerun the full set of invalid frame/invalid header checks. New result:

```text
PASS hello_sequence_wraparound
```

The existing 13 non-destructive checks all passed as well. Afterwards the v110 package was installed back onto
the app.

### 6.4 Routine power-cycle reboot stress: retest passed

Added `tools/reboot_stability_test.py`, which cuts power via the relay for 0.3 s each cycle, waits 2 s after
power-on, and asserts via `version` that the app has booted. When first run as a single batch of 10 cycles, the
execution environment truncated the command at about 30 s; the script actually needs about 5 s per cycle, so
only the first 6 cycles were recorded, which is not a DUT fault. The case was then split into short batches of
6 and 4 under the same conditions, and all passed:

```text
PASS cycle_01
PASS cycle_02
PASS cycle_03
PASS cycle_04
PASS cycle_05
PASS cycle_06
```

The second batch output `PASS cycle_01` to `PASS cycle_04`. Finally `version=0.1.0` and `rx status` were both
normal. Note: while the normal app is running, a `fw_update.py info` timeout is expected, because the UART
bootloader service does not listen in app mode. Conclusion: **BL-N-05 is closed as passed 10/10**; the earlier
P1 record was a false alarm caused by the host test execution time limit.

### 6.5 Invalid DATA session validation: found and fixed

Added `tools/raw_update_session_validation_test.py` covering BL-U-06/07. The first run reproduced the issue:
after a valid BEGIN, a zero-length DATA containing only the offset made the old bootloader reply
`ACK offset=0`, where it should have been ERROR/NACK. This is a protocol acknowledgement semantics error,
recorded as P1.

The DATA branch in `bootloader.c` was fixed: before the offset check or the flash write, it rejects zero-length,
longer than 128 bytes, non-4-byte-aligned and out-of-image-bounds DATA and replies `ERROR`; only a legal new
chunk or a legal duplicate chunk may get ACK/NACK. Rebuilt with `build_bootloader.sh`, flashed the bootloader
and app ELF with `flash-jlink.sh`, then installed the v110 package over UART to restore the metadata.

Hardware regression result after the fix:

```text
PASS begin_ready
PASS invalid_data_zero_length_rejected
PASS invalid_data_overlong_rejected
PASS invalid_data_unaligned_rejected
PASS invalid_data_beyond_image_rejected
PASS incomplete_end_rejected
PASS second_begin_resets_session
PASS session_offset_preserved
```

Finally v110 was reinstalled, and `version=0.1.0` and `rx status` are normal; this P1 is closed.

### 6.6 Rejection of a CRC-correct but invalid vector table

`tools/make_test_package.py` was used to create a CRC-correct package with image size=8, MSP=0 and ResetISR=0.
The UART transfer returned `COMPLETE`, and INFO after the reboot showed `state=3`, `image_size=0`, `version=0`.
That is, the bootloader did not jump to the invalid image but safely entered update mode. The v110 package was
then installed and the app restored. BL-P-09, BL-P-10 and BL-P-12 passed.

### 6.7 UART CRC16 retransmission and noise recovery

`tools/raw_update_transport_resilience_test.py` sends 300 bytes of noise and malformed COBS data before BEGIN;
then for every 10th of the 402 DATA chunks it sends a frame with a wrong CRC16 and immediately retransmits the
correct frame. Actual result:

```text
PASS noise_before_begin_recovered
PASS crc16_corruption_retried blocks=402
PASS complete_after_transport_errors
```

After the update the v110 app's `version` and `rx status` are normal. BL-U-02 and BL-U-09 passed.

### 6.8 Power loss after the CLI update request

`tools/cli_bootloader_power_loss_test.py` waits for the app to return `OK rebooting_to_bootloader` and
immediately cuts power for 0.5 s. After restoring power and sending HELLO, the actual output was:

```text
PASS cli_request_power_loss_state=2
```

state=2 (`UPDATE_REQUESTED`) shows the request was safely persisted and no half-switched app was executed. The
v110 package was then fully installed and the device restored. BL-R-01 passed.

### 6.9 Metadata journal arbitration fix verification

During precise power-cut debugging, multiple CRC-valid records with the same sequence were found. The old
implementation kept the physically earlier entry among records with an equal sequence, with no deterministic
recovery arbitration. The fix uses a wraparound-safe sequence comparison, picks the physically later record
among equal sequences in the same page, and refuses to erase the app when the BEGIN metadata append fails.

The first implementation read flash back for a CRC check immediately after `FlashProgram()` returned; on the
CC1310 that write path made BEGIN unresponsive because of flash/cache access restrictions. That immediate
read-back was removed, keeping the existing CRC checks on the boot/read paths. The fixed bootloader was flashed
with J-Link, the 51,352-byte v110 package transferred fully over UART with `COMPLETE`, and `version=0.1.0` and
`rx status` were normal afterwards.

## 7. Coverage status

| Category | Current status |
| --- | --- |
| Normal update, re-flash, auto boot | Passed |
| Explicit update request from the app | Passed |
| Local Python header/image CRC verification | Passed |
| COBS/CRC16 parsing and malformed-frame recovery | Passed |
| Bootloader rejection of invalid headers | Passed |
| DATA rejection without an established session | Passed |
| Basic RF operation and CLI | Passed |
| sequence=0/65535 wraparound | Passed |
| Idempotent DATA retransmission after a lost ACK | Passed (same DATA chunk resent) |
| Mid-transfer DATA offset, duplicate/future offset | Passed |
| Image CRC failure at END | Passed |
| DATA length/alignment/bounds and incomplete END | Passed (regression after fix) |
| Session reset on repeated BEGIN | Passed (explicit READY/offset 0) |
| Package truncation/appending, local max/min package format | Passed (host unit tests) |
| Invalid app vector table | Not executed |
| CRC-correct image with invalid MSP/ResetISR | Passed; state=3 after reset |
| Power loss/reset mid-update after BEGIN | Passed |
| Metadata two-page full rotation and power loss | Not executed |
| Crash count of three unconfirmed boots | Not executed |
| App confirmation idempotency | Not executed |
| 10 normal power-cycle reboots | Passed (short batches of 6 + 4, original conditions) |
| Long-run RF/CLI (30 min) | Not executed |

### 7.1 P0 case status summary

| Category | Cases | Status |
| --- | --- | --- |
| Normal flow | BL-N-01 to BL-N-04 | Passed |
| Package validation | BL-P-01 to BL-P-07 | Passed |
| UART | BL-U-01 to BL-U-05 | Passed |
| Power loss | BL-R-01 | Passed: power cut after CLI confirmation, recovered as state=2. |
| Power loss | BL-R-02 | Partially passed: power cut after BEGIN/READY reboots to state=3; the window inside the metadata write instruction is not covered precisely. |
| Power loss | BL-R-03 | Partially passed: power cut after BEGIN does not boot the app; the inside of FlashSectorErase is not covered precisely. |
| Power loss | BL-R-04 to BL-R-06 | Not executed |
| Metadata | BL-M-01, BL-M-02 | Not executed |
| Handoff | BL-A-01 | Not completed: SYS/BIOS relocates VTOR to SRAM at runtime, so the checkpoint must be redefined according to the runtime vector table policy. |
| Stability | BL-A-02 | Not executed (30-minute RF/CLI). |

Of 27 P0 items: 18 passed, 2 partially passed, 7 not completed or pending redefinition. Partially passed items
must not be taken as substitute evidence for precise flash/metadata critical power-cut coverage.

## 8. Recommendations for further testing

Next, run the metadata full-page rotation, three-unconfirmed-boot crash count, invalid vector table and
30-minute RF/CLI long-run tests. Host automation cases that exceed about 30 seconds should be run in batches or
persist their results to a log file, to avoid misdiagnosing the host execution time limit as a DUT fault.

Until all P0/P1 items are closed, the current results must not be treated as complete production qualification;
the current conclusion only shows that the core update path and basic input protection work on real hardware.
