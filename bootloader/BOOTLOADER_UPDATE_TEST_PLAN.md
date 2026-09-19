# CC1310 RX Bootloader Update Feature Test Design

## 1. Purpose, scope and pass criteria

This document defines the verification plan for the CC1310F128 RX bootloader, covering the full chain from the
app requesting an update, the UART update, flash writing, metadata recovery and app boot confirmation through to
abnormal fallback. The items under test are:

- `bootloader/bootloader.c`: update protocol, flash operations, metadata A/B journal, app handoff;
- `rfPacketRx/boot_api.c`, `cli_bootloader.c`: app boot confirmation and update request;
- `tools/fw_package.py`, `tools/fw_protocol.py`, `tools/fw_update.py`: package and serial client;
- the non-ROM boot app for RX v0.1.0 or a later identifiable version.

Pass criteria: no corruption, power loss, communication failure or invalid input may ever execute an incomplete
app; after a valid package update the app must boot, confirm its boot and be able to enter update mode again.
All P0/P1 items must pass, and P2 items must have their results and known limitations recorded.

## 2. Test environment

| Item | Requirement |
| --- | --- |
| DUT | CC1310F128 RX, UART `/dev/cu.usbserial-A50285BI` |
| Power control | `python3 tools/relay_control.py off/on`, able to cut power at a specified stage |
| Debug | J-Link Pro, cJTAG, CC1310F128, reading flash/RAM/exception registers when needed |
| Host | Python 3, `pyserial`, `tools/fw_update.py` |
| Logs | Save client stdout/stderr, raw serial bytes, J-Link commands/output, package SHA-256 |

Before testing, build two distinguishable valid app packages A/B, e.g. with different package versions and CLI
boot text:

```bash
bash rfPacketRx/build_boot_nonrom_test.sh 110
cp rfPacketRx/boot_build/nonrom_test/rfPacketRx_boot_nonrom.pkg /tmp/rx_A.pkg
# Change the visible version or build number and rebuild B; do not overwrite A.
bash bootloader/build_bootloader.sh
```

Every package must pass:

```bash
python3 tools/fw_package.py --verify /tmp/rx_A.pkg
```

## 3. Observability and judgement basis

### 3.1 Serial port and protocol

The UART is fixed at 115200 8N1; frames are COBS-encoded and end with `0x00`. A decoded frame contains the
protocol version, message type, sequence, payload length, payload and CRC16/CCITT-FALSE. Test scripts should
call `tools/fw_protocol.py` directly to build both normal and malformed frames rather than relying only on the
high-level client.

The `INFO` payload for `HELLO` should be 20 bytes: target, state, maximum app length, current imageSize and
firmwareVersion, all little-endian uint32.

### 3.2 Metadata and boot state

The metadata pages are `0x6000` and `0x7000`, and the app is at `0x8000–0x1EFFF`. When needed, use J-Link to
read both pages and verify the record CRC, sequence monotonicity and newest-record selection offline. State
definitions:

| Value | State | Expected |
| ---: | --- | --- |
| 1 | VALID_APPLICATION | Boot the app when it is valid and the failure threshold is not exceeded |
| 2 | UPDATE_REQUESTED | Enter UART update mode |
| 3 | UPDATE_IN_PROGRESS | Enter UART update mode; booting the app is forbidden |

The unconfirmed boot threshold is currently 3. It is incremented every time the bootloader decides to boot the
app; the app calls `bl_confirm_boot()` to decrement it only after RF initialization succeeds. When observing
boot confirmation, compare the newest record's `unconfirmedBootCount`, `bootAttemptId` and
`confirmedBootAttemptId` before and after the reboot.

## 4. Normal function tests

| ID | Priority | Precondition | Steps | Expected result |
| --- | --- | --- | --- | --- |
| BL-N-01 | P0 | Valid app A present | Power on, wait 5 s, send `help`, `version` | App runs normally; version correct; no bootloader binary frames output. |
| BL-N-02 | P0 | App A running | Enter `bootloader`, then run `fw_update.py ... info` | CLI returns `OK rebooting_to_bootloader`; INFO state=2, target=`0x43433152`. |
| BL-N-03 | P0 | state=2, package B valid | Run `fw_update.py ... flash --package B` | Shows 0–100% and `COMPLETE`; device resets into B. |
| BL-N-04 | P0 | B booted | Send `help`, `version` and the functional `rx status` | B's visible identity is correct, all CLI commands work, RF reception works. |
| BL-N-05 | P1 | B booted | Reboot normally 10 times, waiting for RF init to complete each time | Boots into B every time; after boot confirmation the unconfirmed count returns to 0 and update mode is never entered by mistake. |
| BL-N-06 | P1 | state=2 | Send B, then send the same B again | The second update also succeeds; metadata version, CRC and imageSize match. |
| BL-N-07 | P1 | Bootloader flashed, no metadata | After `HELLO`, send valid package A | Initial state=3; after successful completion the state is written as valid and A boots. |

## 5. Header, image and capacity validation

Each case starts from a valid `.pkg` and changes only one field or byte. Cases that the client's local pre-check
would reject must still be sent as a raw `BEGIN` through the "raw frame sender" to verify the bootloader's own
defenses.

| ID | Priority | Injection | Expected result |
| --- | --- | --- | --- |
| BL-P-01 | P0 | Wrong header magic | `BEGIN` returns ERROR; app not erased, valid state unchanged. |
| BL-P-02 | P0 | Wrong formatVersion or headerSize | ERROR; no flash write. |
| BL-P-03 | P0 | Wrong header CRC32 | ERROR; no flash write. |
| BL-P-04 | P0 | Target changed to TX or anything other than `0x43433152` | ERROR; the old RX app still boots. |
| BL-P-05 | P0 | Load address not `0x8000` | ERROR. |
| BL-P-06 | P0 | size=0, size>`0x17000`, size not 4-byte aligned | ERROR. |
| BL-P-07 | P0 | Flip 1 bit in the image but keep the old image CRC | DATA may be ACKed, END must be ERROR; next reboot state=3. |
| BL-P-08 | P1 | Correct image CRC, wrong total package length (truncated/appended) | Python parser rejects; the raw protocol must not mark an incomplete image valid. |
| BL-P-09 | P1 | Initial MSP outside `0x20000000–0x20004FFF` | END may complete, but the boot check must pass before metadata is written; the next boot must enter update mode and must not jump. |
| BL-P-10 | P1 | ResetISR not Thumb or outside the app slot | Same as BL-P-09. |
| BL-P-11 | P2 | Maximum legal image with imageSize=`0x17000` | Fully written, CRC correct, bootable; must not overwrite the CCFG at `0x1F000`. |
| BL-P-12 | P2 | Minimum legal 4-byte image | Accepted at the package layer, but because the vector table is invalid the reboot must stay in update mode. |

## 6. UART protocol and transport fault tolerance

| ID | Priority | Injection/steps | Expected result |
| --- | --- | --- | --- |
| BL-U-01 | P0 | Normal HELLO with sequence 0, 1, 65535 and then wrapping to 0 | INFO sequence matches the request; the service stays available. |
| BL-U-02 | P0 | In DATA, corrupt the CRC16 of every 10th chunk, then resend the correct chunk | Bad frames are dropped; correct frames get ACK; the final CRC succeeds. |
| BL-U-03 | P0 | Send invalid COBS, empty frames, short frames, and a length field that does not match the actual length | No exception/reset/out-of-bounds write; HELLO still responds afterwards. |
| BL-U-04 | P0 | DATA offset lower than the current offset (exact duplicate) | ACK returns the current written offset; flash content unchanged. |
| BL-U-05 | P0 | DATA offset higher than the current offset | NACK returns the expected offset; no flash range may be skipped. |
| BL-U-06 | P1 | DATA length 0, >128, not 4-byte aligned, or offset+length beyond imageSize | NACK/ERROR; bytesWritten unchanged. |
| BL-U-07 | P1 | DATA or END without BEGIN; END right after BEGIN; another BEGIN in the middle of a BEGIN | All return ERROR or explicitly reset the session; the app must not be marked valid. |
| BL-U-08 | P1 | Lost ACK: the host resends the same DATA | The bootloader ACKs idempotently and the update eventually succeeds. |
| BL-U-09 | P1 | High-speed back-to-back sending, random noise insertion, frames longer than the receive buffer | No memory corruption; abnormal frames dropped; subsequent HELLO/BEGIN still usable. |
| BL-U-10 | P2 | Host unplugs/closes the serial port and reconnects after 1 min | The current implementation has no resume across reset; within the same power-on session it can BEGIN again and transfer the whole image. |

## 7. Power loss, reset and flash fault injection

Use the relay to cut power at the following points; run each point at least 10 times, covering different write
offsets. After each recovery, run `info` first, then check whether any old or half-written app was executed.

| ID | Priority | Power-cut point | Required result after recovery |
| --- | --- | --- | --- |
| BL-R-01 | P0 | After the app CLI replied to `bootloader`, before the reset | The next boot enters update mode or conservatively boots the app; metadata must not be corrupted. |
| BL-R-02 | P0 | After BEGIN validated the package, before/after writing the UPDATE_IN_PROGRESS metadata | Must not erase the app while keeping VALID; if the state is uncertain, update mode must be entered. |
| BL-R-03 | P0 | During erase of the first app-slot page | state=3; the app cannot boot; a full update can be redone. |
| BL-R-04 | P0 | During any DATA FlashProgram (first, middle, last chunk) | state=3; BEGIN can be repeated; the re-flash eventually succeeds. |
| BL-R-05 | P0 | During the END CRC computation | Must not be marked VALID by mistake; the reboot enters update mode. |
| BL-R-06 | P0 | While writing the VALID metadata body after END, or its last 4 bytes of record CRC | The journal ignores the incomplete record and selects the previous valid one; a half-written app must not boot. |
| BL-R-07 | P1 | Before the app jump, after the VTOR switch | The next reset boots the valid app normally; no permanently disabled interrupt state. |
| BL-R-08 | P1 | App started but `bl_confirm_boot()` not yet called | The unconfirmed count keeps increasing; after reaching 3 the device enters update mode. |
| BL-R-09 | P1 | After the app has confirmed | The next boot is normal; the count does not accumulate. |

Power-cut control should record "the last protocol response/chunk offset received and the relay timestamp". If
the hardware cannot be stopped precisely inside a flash API, use a loop, a GPIO test point or a J-Link
breakpoint to help; "cutting power just once" must not substitute for full coverage.

## 8. Metadata journal and boot counter stress tests

`BootMetadata` is 48 bytes, so each page holds about 85 records. Normal boots, confirmations, update requests
and update completions all append records, so full-page rotation must be verified.

| ID | Priority | Steps | Expected result |
| --- | --- | --- | --- |
| BL-M-01 | P0 | Boot/confirm at least 200 times in a row | The sequence strictly increases; the newest valid record survives page rotation. |
| BL-M-02 | P0 | Cut power at each stage: page full, erasing the target page, copying/writing the new record | At least one parseable valid record remains; an invalid app is never booted by mistake. |
| BL-M-03 | P1 | Manually corrupt the newest record CRC | The previous valid record is selected; behaviour is safe and updatable. |
| BL-M-04 | P1 | Manually corrupt an entire page | The other page's valid record still boots or enters update mode; no infinite loop. |
| BL-M-05 | P1 | No valid record in either page | Default state=3, HELLO available, a valid package is accepted. |
| BL-M-06 | P1 | Three unconfirmed boots, then a fourth power-on | Does not jump to the app; INFO responds; state/count follow the threshold policy. |
| BL-M-07 | P1 | Call `bl_confirm_boot()` repeatedly with the same bootAttemptId | Decrements only once; no underflow. |
| BL-M-08 | P2 | Sequence close to `UINT32_MAX` (J-Link injection) | Explicitly record the behaviour of the current comparison policy; if correct wraparound cannot be guaranteed, list it as a design limitation. |

## 9. App handoff and ABI tests

| ID | Priority | Steps | Expected result |
| --- | --- | --- | --- |
| BL-A-01 | P0 | Read VTOR, MSP and PRIMASK after a valid app powers on | VTOR=`0x8000`; MSP in app RAM; PRIMASK restored so interrupts can be enabled. |
| BL-A-02 | P0 | Run the app with RF reception, CLI and version queries for at least 30 min | No HardFault; serial and RF work normally. |
| BL-A-03 | P1 | App does not call the confirmation API (test image) | Enters update mode only after 3 attempts. |
| BL-A-04 | P1 | App API magic/version mismatch (test image) | The API wrapper returns safely; no jump to an arbitrary address. |
| BL-A-05 | P1 | Verify the newest metadata state after the `bootloader` CLI | state=2 must be persisted; if appending metadata fails, the CLI must report an error or the device must enter safe update mode. |
| BL-A-06 | P2 | Switch to the bootloader while leftover app input is still on the UART | Bootloader frame parsing must not let leftover app text affect the update; the update client should flush and retry HELLO after entering. |

## 10. Automation design

### 10.1 Host-side test tools

Add pytest or standard-library tests under `tools/tests/`:

- `test_fw_package.py`: positive and negative cases for every header field, length, CRC and padding;
- `test_fw_protocol.py`: COBS round-trip, CRC16, empty payload, maximum payload, malformed frames;
- `raw_update_client.py`: supports a specified sequence, offset, payload, CRC error, dropped ACK, send rate,
  and triggering the relay at chunk N;
- `metadata_decode.py`: decodes both metadata pages from a J-Link dump and outputs JSON/CSV.

Host unit tests must not depend on hardware and run on every commit. Hardware regression should at least
automatically run BL-N-01 to BL-N-04, BL-P-01 to BL-P-07 and BL-U-01 to BL-U-08.

### 10.2 Hardware test orchestration

Every hardware case uses the following fixed structure:

1. Restore a known bootloader with J-Link, then install baseline app A from the `.pkg`;
2. Record `INFO`, version, package hash and the starting metadata dump;
3. Perform a single injection action;
4. Reset, and save `INFO`, the serial output and a metadata dump;
5. Judge "was the app booted incorrectly", "can it still be re-flashed" and "which of the old/new app booted";
6. Restore with valid package A as the baseline for the next case.

A J-Link full chip erase wipes the metadata pages, so "raw app ELF loaded successfully" must not be treated as
successful update recovery. After every J-Link restore, the app must be installed over UART from the `.pkg` to
create valid metadata.

## 11. Defect severity and deliverables

| Level | Definition |
| --- | --- |
| P0 | Executes a corrupted/unvalidated app, cannot recover, flash out of bounds, metadata bricks the device, protocol memory corruption. |
| P1 | Normal update or failure recovery unreliable, wrong state/version, incorrect confirmation count, update needs unexpected manual intervention. |
| P2 | Diagnostics, logging, performance, error text or documented limitations. |

Each test run outputs: test version, bootloader/app package SHA-256, DUT ID, environment version, case ID,
operation timeline, raw serial log, INFO, metadata dump, result, defect ID and recovery result. A case pass-rate
table must be produced before release; no release while any P0/P1 remains open.

## 12. Risks in the current implementation that need focused verification

These are not preset conclusions but verification items that should be executed first for this implementation:

1. The return value of `appendMetadataRecord()` is ignored in several places on the update-request,
   boot-confirmation, BEGIN and END paths. BL-A-05 and BL-M-01/02 must confirm that a full page or a flash write
   failure does not silently lose state.
2. Protocol error frames are currently dropped rather than answered with ERROR, and the client relies on
   timeout retransmission. BL-U-02/03/09 should confirm this cannot cause a permanent hang or an incorrect write.
3. The app and bootloader currently share the UART; leftover binary/text from before the switch may pollute the
   other side's input. BL-A-06 should decide whether the UART needs a flush after switching, or whether the
   client should send a delimiter first when entering the mode.
4. CRC32 verifies integrity only, not origin. This limitation must be explicitly accepted in the product threat
   model and production process, or an authentication mechanism must be added in a later version.
