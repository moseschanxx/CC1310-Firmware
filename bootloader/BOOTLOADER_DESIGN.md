# CC1310 Unified Firmware Bootloader Design

## Scope and security boundary

This bootloader targets the CC1310F128. It boots from Flash `0x00000000` and installs and launches the unified
application image at `0x00008000` over UART. The application package's target ID is fixed at `0x4343314D`
(`CC1M`); the same image runs as RX or TX at boot according to the role selected in metadata.

The design uses a single App slot rather than A/B application slots. It provides header, metadata and image
CRC32 integrity checks that guard against accidental transfer or Flash corruption, but it **provides no origin
authentication**. Anyone with physical access to the update UART can install a self-built image with a correct
CRC; physical access to the update interface must be controlled in deployment.

## Flash and SRAM layout

| Region | Address range | Size | Owner |
| --- | --- | ---: | --- |
| Bootloader | `0x00000000–0x00005FFF` | 24 KiB | Bootloader |
| Metadata A | `0x00006000–0x00006FFF` | 4 KiB | Bootloader |
| Metadata B | `0x00007000–0x00007FFF` | 4 KiB | Bootloader |
| App slot | `0x00008000–0x0001EFFF` | 92 KiB | Unified firmware |
| CCFG/reserved | `0x0001F000–0x0001FFFF` | 4 KiB | Bootloader |

The bootloader is the only image that contains `ccfg.c` / `.ccfg`. The application is linked with
`boot_app.cmd`, with Flash starting at `0x00008000` and a length of `0x17000`, and must not include `ccfg.c`.
The SRAM available to the App is `0x20000000–0x20004DFF`; `0x20004E00–0x20004EFF` is used for boot debugging
and `0x20004F00–0x20004FFF` for the boot handoff.

## Persistent metadata

Metadata is an append-only journal written to the A/B pages. Each record is 48 bytes, and `recordCrc32` is
programmed last; an incomplete record caused by power loss is ignored because its CRC is invalid.

```c
typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint16_t formatVersion;          /* currently 2 */
    uint16_t recordSize;
    uint32_t sequenceNumber;
    uint32_t state;
    uint32_t imageSize;
    uint32_t imageCrc32;
    uint32_t firmwareVersion;
    uint32_t appRole;                /* unset=0, rx=1, tx=2 */
    uint32_t unconfirmedBootCount;
    uint32_t bootAttemptId;
    uint32_t confirmedBootAttemptId;
    uint32_t recordCrc32;
} BootMetadata;
```

The state values are:

| Value | State | Meaning |
| ---: | --- | --- |
| 1 | `VALID_APPLICATION` | Bootable when both the vector table and the image CRC are valid. |
| 2 | `UPDATE_REQUESTED` | The application requested an update; the UART updater is entered after reset. |
| 3 | `UPDATE_IN_PROGRESS` | An update started or failed; booting the App is forbidden. |

On read, the newest CRC-valid record across both pages is selected. When both pages are full, the bootloader
erases the page that does not hold the newest record and then writes the new record, so the other page always
retains the newest valid record until the new write completes.

## Boot decision, confirmation and handoff

The decision after every reset is:

```text
metadata is not VALID_APPLICATION     -> UART update mode
unconfirmedBootCount >= 3             -> UART update mode
App vector table or image CRC invalid -> UART update mode
otherwise                             -> record an unconfirmed boot and jump to the App
```

Before jumping, the bootloader increments `unconfirmedBootCount` and `bootAttemptId`, and writes the following
at `0x20004F00`:

```c
typedef struct {
    uint32_t magic;
    uint32_t bootAttemptId;
    uint32_t appRole;
} BootHandoff;
```

The application reads its role from the handoff; only `tx` explicitly selects TX, and every other value falls
back to RX. The application calls `bl_confirm_boot()` after its critical UART/RF initialization succeeds; the
bootloader API appends a confirmation record only for a matching, not-yet-confirmed `bootAttemptId`, and
decrements the unconfirmed count by one. A reset or power loss before confirmation keeps that count; failures
therefore only make the system enter update mode more conservatively.

Before jumping, the bootloader verifies that the MSP lies within `[0x20000000, 0x20005000)`, that the ResetISR
is a Thumb address inside the App slot, and recomputes the image CRC32. It then stops SysTick, disables and
clears NVIC interrupts, points VTOR at `0x00008000`, restores PRIMASK, loads the App MSP and jumps to the
ResetISR. Restoring PRIMASK is necessary: otherwise the bootloader's interrupts-disabled state leaks into the
TI-RTOS App.

## Bootloader API

The application may only call the bootloader through the ABI at the fixed Flash address `0x00005000`. The
current ABI version is 2; both functions return `0` when metadata was successfully persisted and non-zero on
failure:

```c
typedef struct {
    uint32_t magic;                  /* BLAP */
    uint16_t version;                /* currently 2 */
    uint16_t reserved;
    int (*confirmBoot)(uint32_t bootAttemptId);
    int (*requestUpdate)(void);
} BootloaderApi;
```

`boot_api.c` verifies the API magic and version before calling these functions, and returns safely on a
mismatch or a metadata write failure. The CLI `bootloader` command resets only after `UPDATE_REQUESTED` was
written successfully; on failure it keeps the current application running and returns an error.

## Package format

An update package is a 32-byte header followed by the raw App bytes. The header is not written to the App slot,
and the App vector table is always at `0x00008000`.

```c
typedef struct __attribute__((packed)) {
    uint32_t magic;                  /* FWPK */
    uint16_t formatVersion;          /* 1 */
    uint16_t headerSize;             /* 32 */
    uint32_t targetId;               /* must be CC1M / 0x4343314D */
    uint32_t applicationAddress;     /* must be 0x00008000 */
    uint32_t imageSize;              /* non-zero, 4-byte aligned and <= 0x17000 */
    uint32_t firmwareVersion;
    uint32_t imageCrc32;
    uint32_t headerCrc32;            /* IEEE CRC32 of the first 28 bytes */
} FirmwarePackageHeader;
```

Once `BEGIN` passes the header check, the bootloader first appends `UPDATE_IN_PROGRESS` and only then erases
the App slot. `END` appends `VALID_APPLICATION` only after all data has been written and the App Flash CRC32
matches `imageCrc32`. Any header error, write error, timeout, CRC mismatch or reset causes the next boot to
enter UART update mode.

## UART protocol v2

The UART runs at 115200 8N1 in binary mode. Each frame is COBS-encoded and terminated with `0x00`; the decoded
layout is:

```text
protocolVersion:u8 | type:u8 | sequence:u16 | payloadLength:u16 |
payload | frameCrc16:u16
```

The protocol version is 2; the frame CRC is CRC-16/CCITT-FALSE, while packages and images use IEEE CRC32.

| Message | Direction | Payload |
| --- | --- | --- |
| `HELLO` / `INFO` | Host → target / target → host | Empty / target, state, slot size, image size, version, role (24 B). The version is sent as a `u32` in `0x00MMmmpp` form and displayed by the host as `major.minor.patch`. |
| `BEGIN` / `READY` | Host → target / target → host | `FwPackageHeader + role:u32` / starting offset. |
| `DATA` / `ACK` / `NACK` | Host / target | `offset:u32 + 1..128 B` (4-byte aligned) / next expected offset. |
| `END` / `COMPLETE` | Host / target | Empty / empty. |
| `SET_ROLE` / `COMPLETE` | Host / target | role:u32 of `rx` or `tx` / empty. |
| `ERROR` | target | Empty. |

Transfer is stop-and-wait. For an exact duplicate `DATA` whose data was already written successfully, the
bootloader returns the current `ACK`; a future offset gets a `NACK`. Resuming across a reset is not supported:
after an interruption, send `BEGIN` again and transfer the whole image.

`SET_ROLE` only applies to the currently valid App and does not rewrite the image; it appends metadata, updates
the role and resets. The host tool invokes it with `fw_update.py set-role --role rx|tx`.

## Build, deployment and update

Build the unified application:

```sh
./firmware/build.sh
python3 tools/fw_package.py --verify firmware/boot_build/nonrom_test/firmware.pkg
```

Update over UART and choose the boot role at the same time:

```sh
python3 tools/fw_update.py --port /dev/cu.usbserial-XXXX flash \
  --package firmware/boot_build/nonrom_test/firmware.pkg --role rx
```

Switch the role of an installed, valid image:

```sh
python3 tools/fw_update.py --port /dev/cu.usbserial-XXXX set-role --role tx
```

For first deployment or recovery, use a full J-Link flash:

```sh
./bootloader/build.sh
./firmware/build.sh
./firmware/flash_all_jlink.sh -r rx
```

A full flash performs a chip erase and then flashes the bootloader, the `0x8000` application HEX and valid
metadata. Flashing only the application HEX is not enough: without valid metadata the bootloader will not boot
it. Field upgrades should use the `firmware.pkg` UART flow and avoid a J-Link full erase.

## Validation priorities

1. Normal RX and TX boot, `bl_confirm_boot()` and confirmation across consecutive reboots.
2. The `bootloader` CLI request, a full UART update, `set-role` and metadata role persistence.
3. Wrong target/header/image CRC, out-of-range or unaligned data, duplicate DATA and lost ACK.
4. Power-loss recovery at each stage: metadata write, App erase, data programming and the final VALID commit.
5. Consistency of metadata, role, vector table and application CRC after a full J-Link recovery.
