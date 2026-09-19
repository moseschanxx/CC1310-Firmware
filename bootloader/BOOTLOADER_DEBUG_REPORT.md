# RX Bootloader / App Update Bring-up Issue Analysis Report

## 1. Goal and scope

This work implemented and verified the serial update chain for the CC1310 RX firmware: the bootloader loads the
app located at `0x8000`; the app requests an update via the CLI `bootloader` command; a Python client sends an
update package with a CRC32 header. The bootloader maintains dual-page metadata and an unconfirmed boot counter,
and the app confirms the boot once RF initialization succeeds. The TX project is outside the scope of this change.

## 2. Final result

The RX device currently runs app v108 (51,224 bytes, CRC32 `27C75B4A`). The following chain has been verified
on real hardware:

1. The app's serial `help` returns the command list correctly;
2. Entering `bootloader` on the app returns `OK rebooting_to_bootloader`;
3. The bootloader's `HELLO/INFO` returns `state=2` (explicit update request);
4. `tools/fw_update.py` fully writes and verifies v108 over `/dev/cu.usbserial-A50285BI`,
   returning `COMPLETE`;
5. The bootloader automatically boots the updated app, and the CLI responds normally again.

## 3. Key issues and how they were located

### 3.1 The direct jump did not restore the interrupt state

Early on the bootloader could jump to the app, but the application inherited the `PRIMASK=1` set by the
bootloader. That prevents the application's interrupts and RTOS scheduling from working. The fix is to switch
the vector table, clear the NVIC pending bits and then execute `CPUcpsie()` before jumping to the app, so the
handoff is equivalent to entering the app after a reset.

### 3.2 ROM SYS/BIOS does not support relocating the app to 0x8000

The original app used ROM SYS/BIOS. Some of its startup and runtime entry points implicitly assume the image
lives at flash address zero; after relocation this caused HardFaults and abnormal ROM call paths. A non-ROM
custom SYS/BIOS library was therefore generated for the boot app, together with a separate app linker command
file:

| Region | Address/size | Purpose |
| --- | --- | --- |
| bootloader | `0x0000–0x5FFF` | Boot, metadata, update protocol |
| metadata pages | `0x6000`, `0x7000` | A/B append records |
| app slot | `0x8000–0x1EFFF` | Updatable app |
| SRAM_APP | `0x20000000–0x20004DFF` | App data, heap, stack |

The generated vector table's reset entry was replaced with a clear `ResetISR()` wrapper that then calls the TI
runtime `_c_int00()`; this avoids depending on a vector table at address zero.

### 3.3 Root cause of the `pthread_create()` HardFault

The symptom was a HardFault inside `pthread_create()` in the app:

- Boot markers had passed `Board_initGeneral()`, queue initialization and pthread attribute setup;
- CFSR was `0x00008600` and BFAR was `0x3EF0E49D`;
- After correctly decoding the exception stack frame, PC was `0x856A`, i.e. `IHeap_alloc()` reading the heap
  object's function table;
- RAM `0x20002F00` should have held `0x00013C20` but actually contained random data.

It was first mistaken for a pthread stack or Mailbox parameter problem. Further inspection of the map and Intel
HEX showed that `.cinit` had zero length while `.data` was emitted by `armhex` as RAM-address records. The
package tool only extracts `0x8000–0x1EFFF`, so those RAM records never enter the update package, and the
app's global initialization data was never copied from flash to RAM.

The root cause was that the custom linker command lacked the **linker-level** `--rom_model`. That option must
come after `-z`; placed before it, it is treated as a compiler option and ignored. After the fix the map shows:

```text
.cinit  0x00014260  length 0x280
.data   0x20002C18  UNINITIALIZED
```

`.cinit` now contains the compressed load image of `.data` and the `.bss` zero-fill record, so `_c_int00()`
can complete C runtime initialization correctly at app startup.

### 3.4 No UART response

After fixing data initialization the app reached `BIOS_start()` with no further HardFault, but the CLI did not
respond. Investigation showed that `PIN_init()` and `Board_initHook()` had been temporarily skipped during
debugging. Restoring these two standard board initialization steps brought the UART pins and peripheral state
back, and the CLI worked normally.

## 4. Implementation and recovery strategy

The bootloader lives in `bootloader/bootloader.c` and keeps as little state as possible:

- The package header contains magic, format version, target ID, app address, length, version, image CRC32 and
  header CRC32; the CC1310 has no SHA-256/Ed25519 requirement, so this project uses CRC32 for integrity checks.
- `UPDATE_IN_PROGRESS` metadata is written before receiving; if an erase/program fails or power is lost, the
  next boot continues in update mode instead of starting an incomplete app.
- On completion the app CRC32 in flash is verified before `VALID_APPLICATION` metadata is written.
- `unconfirmedBootCount` is incremented before every app boot; the app's
  `bl_confirm_boot()` decrements it only after RF initialization succeeds. When the failure threshold is
  reached or the app is invalid, the bootloader refuses to boot the app and enters update mode.
- The app calls `confirmBoot()` and `requestUpdate()` through the bootloader API at a fixed address.

## 5. Verification method and artifacts

Build commands:

```bash
bash rfPacketRx/build_boot_nonrom_test.sh 108
python3 tools/fw_package.py --verify \
  rfPacketRx/boot_build/nonrom_test/rfPacketRx_boot_nonrom.pkg
```

Update command:

```bash
python3 tools/fw_update.py --port /dev/cu.usbserial-A50285BI flash \
  --package rfPacketRx/boot_build/nonrom_test/rfPacketRx_boot_nonrom.pkg
```

The final package is `rfPacketRx/boot_build/nonrom_test/rfPacketRx_boot_nonrom.pkg`.
When flashing the bootloader, note that the J-Link script performs a full chip erase and therefore also wipes
the `0x6000/0x7000` metadata. Raw J-Link flashing of the app does not create valid metadata; after flashing the
bootloader, the app package must be written through the Python update flow.

## 6. Follow-up recommendations

- Promote `build_boot_nonrom_test.sh` from its experimental name to the official boot app build entry point,
  and script the custom SYS/BIOS generation step so it no longer depends on an existing `configPkg_nonrom5`
  directory.
- Add automated serial regression for the update protocol: invalid headers, wrong CRC, interrupted transfers,
  power-loss recovery, failure threshold and app confirmation counting.
- Turn HardFault CFSR/BFAR and automatic stack-frame capture into an optional diagnostic feature instead of
  relying solely on manual J-Link reads.
