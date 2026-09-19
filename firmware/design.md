# CC1310 Combined Firmware

This is the unified application image for the CC1310F128: the same program enters the RX or TX role at boot according to the bootloader metadata. It is not the standalone Packet RX image from the TI example projects.

The application is launched by the resident bootloader, linked at `0x00008000`, and uses a custom, non-ROM SYS/BIOS library. The bootloader reserves `0x00000000–0x00007FFF`, the metadata pages and the CCFG; the application must not overwrite those regions directly.

## Features and configuration

- RX: continuous proprietary RF reception; data is stored in a 32-deep application mailbox, with statistics and optional per-packet serial output.
- TX: sends 30-byte sync-time or frame-sync packets from the CLI.
- Boot role: runs TX when the metadata says `tx`, otherwise defaults to RX.
- RF settings live in `smartrf_settings/smartrf_settings.c`: currently **433.000 MHz, 50 kBaud, 2-GFSK, -10 dBm**. Assess the hardware capability and local radio regulations before changing the frequency, power or modulation parameters.
- UART CLI: `Board_UART0`, 115200 8N1; see [`cli.md`](cli.md) for the commands.
- I2C slave: I2C0, 7-bit address `0x2A`, SCL/DIO16, SDA/DIO17. Both pins are configured with internal pull-ups, open-drain and 2 mA drive; without external pull-ups it is only suitable for short, low-capacitance buses. The interface is managed directly by driverlib and a SYS/BIOS Hwi and cannot be shared with the TI master-only `I2C` driver; see [`i2c_slave.md`](i2c_slave.md) for the full protocol.
- DIO1 outputs timing pulses by default during TX execution and RX handling, for latency measurement.

## Build

The build depends on TI ARM CGT, SimpleLink CC13x0 SDK 4.20.02.07 and XDCtools/TI-RTOS. The script uses the in-repo `toolchains/ti-cgt-arm_18.12.5.LTS` by default; the default SDK path is `/Applications/ti/simplelink_cc13x0_sdk_4_20_02_07`, overridable with `SIMPLELINK_SDK`. The compiler directory can also be overridden with `TI_ARM_CGT`.

Run in `firmware/`:

```sh
./build.sh
```

`FIRMWARE_VERSION` in `firmware_build.h` is the single source of the release version for the CLI and the OTA package. The build script encodes `major.minor.patch` as the 32-bit value `0x00MMmmpp`: the top byte is reserved as 0, and `major`, `minor` and `patch` each take one byte (each in the range 0–255); this is written into the OTA package header. Host verification and the device `info` both display it as `major.minor.patch`; increment this semantic version for every release. On success it produces:

| File | Purpose |
| --- | --- |
| `boot_build/nonrom_test/firmware.out` | Linked application ELF/OUT. |
| `boot_build/nonrom_test/firmware.hex` | Intel HEX starting at address `0x00008000`. |
| `boot_build/nonrom_test/firmware.pkg` | UART OTA package with target ID `0x4343314D` (`CC1M`). |

The script verifies the generated OTA package. It can also be verified separately:

```sh
python3 ../tools/fw_package.py --verify boot_build/nonrom_test/firmware.pkg
```

## Runtime model

The main radio Task has priority 2; the CLI, the RX `packet_print` and the I2C `i2c_slave` worker all have priority 1. The CLI stack is 2048 bytes and the I2C worker stack is 1024 bytes. The RX role creates the radio, CLI and packet-print Tasks; the TX role creates the radio and CLI Tasks; the I2C worker starts regardless of role. The I2C Hwi only handles byte transfer and record enqueueing, and the UART output for `ipc dump` is done by the worker. The `stack` CLI command shows each Task's stack high-water mark as measured evidence before shrinking SRAM.

The startup code first masks external interrupts left over from the bootloader, then completes the application's vector and driver initialization, so that stale peripheral interrupts cannot fire before the new handlers are installed. The application calls `bl_confirm_boot()` only after RF initialization has completed, the UART CLI is open and the command table is installed. To perform an OTA update, use the CLI `bootloader` command to request a reset into the bootloader; see [`build_flash.md`](build_flash.md) for the flashing and recovery flow.

## Layout

| Path | Contents |
| --- | --- |
| `firmware.c` | RX radio Task. |
| `firmware_tx.c` | TX radio Task and TX CLI. |
| `rf_packet_queue.c` | RX mailbox, statistics and per-packet output. |
| `cli_*.c` | Serial CLI, role commands and bootloader command. |
| `boot_app.cmd` | Flash/SRAM layout for the bootloader application. |
| `boot_build/boot_release.cfg` | Non-ROM SYS/BIOS configuration. |
| `smartrf_settings/` | Radio parameters exported from SmartRF. |
