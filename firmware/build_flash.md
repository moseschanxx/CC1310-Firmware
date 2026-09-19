# Flashing the CC1310F128 with J-Link

`flash_all_jlink.sh` uses a SEGGER J-Link over two-wire cJTAG to fully flash the bootloader, the unified application and valid metadata. This is for first deployment or recovery, not for field OTA updates.

## Build dependencies

Building the CC1310F128 firmware needs two TI components: the in-repo ARM compiler toolchain and an externally installed SimpleLink CC13x0 SDK. They serve different purposes and cannot substitute for each other.

- The default ARM toolchain is the in-workspace `toolchains/ti-cgt-arm_18.12.5.LTS`, which provides `armcl`, `armhex`, the runtime libraries and the compiler headers. Override it with `TI_ARM_CGT=/path/to/ti-cgt-arm_18.12.5.LTS`.
- `simplelink_cc13x0_sdk_4_20_02_07` is installed by default at `/Applications/ti/simplelink_cc13x0_sdk_4_20_02_07`. It provides the TI-RTOS/NoRTOS sources and headers for the CC1310 platform plus the RF, DPL, driverlib, Display, GRLIB, SPIFFS and other libraries. Override it with `SIMPLELINK_SDK=/path/to/simplelink_cc13x0_sdk_4_20_02_07`.

For example, when the SDK is installed elsewhere:

```sh
SIMPLELINK_SDK=/opt/ti/simplelink_cc13x0_sdk_4_20_02_07 \
    ./firmware/build.sh
```

The build script checks for `armcl` and `armhex` in the toolchain and for the SDK root at the start. Without the SDK, even with the ARM compiler present, compilation or linking cannot complete because the CC1310 platform headers and libraries are missing.

## Wiring

The target board and the J-Link must share a ground, and the target must be powered on its own; `VTref` is only used by the J-Link to detect the target I/O level.

| J-Link ARM 20-pin | CC1310 target | Notes |
| --- | --- | --- |
| 1 `VTref` | Target VDD | Level reference; 1.8–3.8 V for the CC1310. |
| 7 `TMS/SWDIO` | `DIO` / `TMSC` | cJTAG bidirectional data line. |
| 9 `TCK/SWCLK` | `TCKC` | cJTAG clock. |
| 15 `nRESET` | `RESET_N` | Recommended; makes recovering the connection easier. |
| Any GND | GND | Required. |

`DIO` is not the separate TDI/TDO of four-wire JTAG. Use 1000 kHz at first; if the connection is unstable, slow down with `JLINK_SPEED=100`.

## Build and flash

Build the bootloader and application from the workspace root:

```sh
./bootloader/build.sh
./firmware/build.sh
```

Clean the regenerable build artifacts and rebuild:

```sh
./bootloader/build.sh clean
./firmware/build.sh clean
./bootloader/build.sh
./firmware/build.sh
```

`bootloader/build.sh clean` deletes the dedicated `bootloader/build/` output directory. `firmware/build.sh clean` deletes only the object files, images, map files and OTA package under `firmware/boot_build/nonrom_test/`, keeping the version-controlled TI-RTOS configuration sources.

Then choose the boot role and flash. RX is the default role:

```sh
./firmware/flash_all_jlink.sh -r rx
./firmware/flash_all_jlink.sh -r tx
```

Specify the probe serial number:

```sh
./firmware/flash_all_jlink.sh -s 123456789 -r rx
```

The script uses these defaults:

- bootloader: `bootloader/build/bootloader.out`
- app HEX: `firmware/boot_build/nonrom_test/firmware.hex`
- app package: `firmware/boot_build/nonrom_test/firmware.pkg`

These paths can be overridden with `-b`, `-a` and `-p`. `-a` and `-p` must be a matching pair produced by the same build: the script verifies that the HEX content exactly matches the package's CRC/length, then writes metadata according to `-r`. The metadata decides whether the unified application boots as RX or TX.

## Behaviour and limitations

The script invokes `JLinkExe` with `CC1310F128`, `cJTAG` and `-ExitOnError 1`, and in order:

1. Connects and **erases the whole chip**;
2. Flashes the bootloader OUT;
3. Flashes the application HEX starting at `0x00008000`;
4. Writes the package-verified boot metadata;
5. Resets and runs.

The full chip erase wipes the existing application, metadata and any other Flash data. Do not flash `firmware.hex` on its own: without valid metadata the bootloader will not boot the application. Do not flash `firmware.out` as a zero-address standalone image either; the application contains no CCFG and its vector table is at `0x00008000`.

## Updating firmware.pkg over the bootloader UART

Field upgrades use the `firmware.pkg` produced by the build; they need no J-Link and neither erase the chip nor
rewrite the bootloader. The upgrade replaces the App slot (`0x00008000–0x0001EFFF`); if the transfer is
interrupted, the package fails validation or the image CRC does not match, the bootloader does not boot the
incomplete application and stays in UART update mode.

Build the application first and optionally verify the package on the host:

```sh
./firmware/build.sh
python3 tools/fw_package.py --verify firmware/boot_build/nonrom_test/firmware.pkg
```

A running application must first request entry into the bootloader through its UART CLI:

```text
bootloader
OK rebooting_to_bootloader
```

The application resets after the metadata write succeeds. If the target has no valid application or the last
update did not complete, the bootloader enters update mode directly after reset without needing that CLI
command.

Once you know the serial device name, use `fw_update.py info` to verify that it really is the bootloader
responding. The default baud rate is 115200; on macOS the serial port is usually named `/dev/cu.usbserial-XXXX`:

```sh
python3 tools/fw_update.py --port /dev/cu.usbserial-XXXX info
```

Typical output looks like the following, where `target=CC1M(0x4343314D)` and `state=update_requested(2)` show
that the target is correct and the application has requested an update:

```text
INFO target=CC1M(0x4343314D) state=update_requested(2) max_size=94208 image_size=... version=... role=rx(1)
```

When sending a package, the post-update boot role must be chosen explicitly. The following command updates and boots as RX:

```sh
python3 tools/fw_update.py --port /dev/cu.usbserial-XXXX flash \
    --package firmware/boot_build/nonrom_test/firmware.pkg --role rx
```

Change `--role rx` to `--role tx` to choose TX. The tool first verifies the package header, target ID and image
size, then transfers in 128-byte stop-and-wait chunks; the target verifies the written App CRC32 before
returning `COMPLETE` and resetting automatically. After seeing `COMPLETE`, wait for the application to reboot,
then confirm the update and role with the application CLI's `version`, `help` and the corresponding RX/TX
functions.

To only switch the role of an already installed, valid image, do not resend the package. Enter `bootloader`
from the application CLI, wait for it to reset into the updater, then run:

```sh
python3 tools/fw_update.py --port /dev/cu.usbserial-XXXX set-role --role tx
```

`set-role` only updates metadata and resets automatically after returning `COMPLETE`; it requires the target to
already hold an application image with a valid CRC and vector table. If the update fails or no `INFO` arrives on
the serial port, do not fall back to flashing `firmware.hex` on its own; check the UART wiring/port/baud rate
and, if necessary, use the J-Link full recovery flow described above.

## Troubleshooting

1. Install the SEGGER J-Link Software Pack and make sure `JLinkExe` is on `PATH`; otherwise set `JLINK_BIN=/path/to/JLinkExe`.
2. Check the target power, `VTref`, the common ground and the DIO/TCKC wiring.
3. If it cannot connect, use `JLINK_SPEED=100 ./firmware/flash_all_jlink.sh -r rx`, and start the script while holding `RESET_N`, then release it.
4. Treat the flash as successful only when the script returns 0; it first verifies that the application matches the OTA package.

For field updates use the UART OTA client and `firmware.pkg` described above rather than the J-Link full-erase flow.
