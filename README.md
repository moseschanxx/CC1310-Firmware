# CC1310 Unified RX/TX Firmware

Unified wireless firmware for the TI CC1310F128, a resident UART bootloader, and matching host tools. A single application image runs as either RX or TX, selected at boot from bootloader metadata; it supports J-Link factory flashing, UART OTA updates and role switching.

> This project targets the CC1310F128; the Flash layout, CCFG, RF parameters and pin configuration are all hardware specific. Confirm the target hardware and local radio regulations before flashing or changing any wireless parameter.

## Key features

- **Unified application image**: the application links at `0x00008000` and metadata selects RX or TX; anything not explicitly TX falls back safely to RX.
- **RX/TX radio**: RX continuously receives proprietary RF data and maintains a queue and statistics; TX can send sync-time or frame-sync packets from the UART CLI.
- **Resident bootloader**: lives in Flash `0x00000000–0x00007FFF` and handles application validation, boot role, boot confirmation and UART updates.
- **Power-loss-safe metadata**: metadata is a dual-page append-only journal; on an interrupted update or abnormal boot the device conservatively stays in UART update mode.
- **UART OTA**: CRC32-protected `.pkg` update packages; the host tool supports querying, flashing and switching between the RX/TX roles.
- **J-Link recovery / production flashing**: writes the bootloader, application, CCFG and valid metadata in one pass.
- **Serial CLI and measurement interface**: the default UART is `115200 8N1`, and DIO1 can output TX/RX timing pulses for latency measurement.

The current RF configuration is **433.000 MHz, 50 kBaud, 2-GFSK, -10 dBm**. It lives in `firmware/smartrf_settings/`.

## Layout

| Path | Description |
| --- | --- |
| `firmware/` | Unified RX/TX application, UART CLI, radio logic, linker script and build/flash scripts. |
| `bootloader/` | NoRTOS bootloader, CCFG, metadata journal, image validation and UART update protocol. |
| `tools/` | OTA package, UART update, J-Link metadata and test tools. |
| `analysis/` | Radio latency capture data and analysis scripts. |
| `tirtos_builds_CC1310_LAUNCHXL_release_ccs/` | CCS/TI-RTOS generated configuration used by the firmware build. |
| `toolchains/` | In-repo TI ARM compiler toolchain; `ti-cgt-arm_18.12.5.LTS` is used by default. |

## Build dependencies

Required:

- TI ARM CGT `18.12.5.LTS`, by default at `toolchains/ti-cgt-arm_18.12.5.LTS`.
- SimpleLink CC13x0 SDK `4.20.02.07`, by default at `/Applications/ti/simplelink_cc13x0_sdk_4_20_02_07`.
- Python 3 (for package generation, verification and the OTA tools).
- SEGGER J-Link Software (only needed for J-Link flashing; `JLinkExe` should be on `PATH`).

`SIMPLELINK_SDK` and `TI_ARM_CGT` override the default paths:

```sh
TI_ARM_CGT="$PWD/toolchains/ti-cgt-arm_18.12.5.LTS" \
SIMPLELINK_SDK=/path/to/simplelink_cc13x0_sdk_4_20_02_07 \
./firmware/build.sh
```

`FIRMWARE_VERSION` in `firmware/firmware_build.h` is the single source of the release version and must be incremented for every release. The build script encodes `major.minor.patch` as the 32-bit value `0x00MMmmpp`: the top byte is reserved as 0, and `major`, `minor` and `patch` each take one byte (each in the range 0–255), so `0.2.0` encodes as `0x00000200`. The OTA header stores this 4-byte value, and both the host tools and the device `info` display it as `major.minor.patch`. The build script refuses to run in an environment that lacks the compiler, HEX tool or SDK.

## Building

Run from the workspace root:

```sh
./bootloader/build.sh
./firmware/build.sh
python3 -m unittest discover -s tools/tests -v
```

Build outputs:

| File | Purpose |
| --- | --- |
| `bootloader/build/bootloader.out` | Bootloader image. |
| `firmware/boot_build/nonrom_test/firmware.out` | Application image linked at `0x00008000`. |
| `firmware/boot_build/nonrom_test/firmware.hex` | Address-preserving Intel HEX for flashing the application with J-Link. |
| `firmware/boot_build/nonrom_test/firmware.pkg` | CRC32-protected UART OTA package with target ID `CC1M`. |

Verify the OTA package:

```sh
python3 tools/fw_package.py --verify firmware/boot_build/nonrom_test/firmware.pkg
```

Clean regenerable artifacts:

```sh
./bootloader/build.sh clean
./firmware/build.sh clean
```

## J-Link factory flashing or recovery

> **Warning: this performs a full chip erase.** It wipes the existing application, metadata and any other Flash data.

After the builds above, choose the boot role explicitly:

```sh
./firmware/flash_all_jlink.sh -r rx
# or
./firmware/flash_all_jlink.sh -r tx
```

Specify a serial number in multi-probe setups:

```sh
./firmware/flash_all_jlink.sh -s 123456789 -r rx
```

The script erases the chip, flashes the bootloader, flashes the application HEX from `0x00008000`, and writes metadata matching the verified package. Do not flash `firmware.hex` on its own or flash `firmware.out` as a zero-address image: the application carries no CCFG and needs valid metadata to boot.

Wiring and troubleshooting are covered in [firmware/build_flash.md](firmware/build_flash.md).

## UART OTA update

Once the device is in bootloader update mode, flash the package with the host tool and choose a role:

```sh
python3 tools/fw_update.py --port /dev/cu.usbserial-XXXX flash \
  --package firmware/boot_build/nonrom_test/firmware.pkg --role rx
```

Switch the role of an installed, valid image without re-sending the application:

```sh
python3 tools/fw_update.py --port /dev/cu.usbserial-XXXX set-role --role tx
```

Query the device status:

```sh
python3 tools/fw_update.py --port /dev/cu.usbserial-XXXX info
```

For example, `info` prints `target=CC1M(0x4343314D)`,
`version=0.2.0(0x00000200)` and `role=rx(1)`; the state is likewise given
with both its name and raw value, as in `state=update_requested(2)`.

The bootloader verifies the package header, target ID, application address and CRC32. It uses a single App slot and supports neither resumable transfers nor A/B rollback; an interrupted update, an invalid image, or three consecutive unconfirmed boots put the device into UART update mode.

## Security and hardware boundaries

- The OTA CRC32 only guarantees integrity; it **provides no origin authentication or encryption**. Anyone with physical access to the update UART can flash a self-built image with a correct CRC, so physical access to that interface must be controlled in deployment.
- The bootloader exclusively owns `0x00000000–0x00007FFF`, the metadata pages at `0x6000/0x7000` and the CCFG. The application may only use `0x00008000–0x0001EFFF`.
- Any change to the Flash layout, CCFG, RF frequency/power, modulation or pin assignment must be reviewed together across the bootloader, firmware, host tools and hardware validation flow.

## Further documentation

- [Firmware design](firmware/design.md)
- [RX data flow](firmware/RF_PACKET_DATA_FLOW.md)
- [Firmware CLI](firmware/cli.md)
- [Bootloader design and OTA protocol](bootloader/BOOTLOADER_DESIGN.md)
- [J-Link build and flashing guide](firmware/build_flash.md)
