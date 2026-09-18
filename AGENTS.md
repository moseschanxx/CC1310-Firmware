# Workspace Guidelines

## Repository layout

This directory is a workspace containing several independent Git repositories; the workspace root is
not itself a Git repository. Run Git commands from the relevant directory or use `git -C <directory>`.

| Directory | Responsibility |
| --- | --- |
| `firmware/` | Unified CC1310F128 application. Bootloader metadata selects RX or TX at startup. The app links at `0x00008000` and uses custom, non-ROM SYS/BIOS. |
| `bootloader/` | NoRTOS resident UART bootloader, CCFG owner, App verification, metadata journal, role handoff, and J-Link recovery. |
| `tools/` | Python package/protocol/OTA tools, J-Link metadata helper, and host/hardware test scripts. |
| `analysis/` | Captured latency data and analysis scripts. Each capture directory may carry its own requirements. |
| `tirtos_builds_CC1310_LAUNCHXL_release_ccs/` | Shared, CCS-generated TI-RTOS configuration consumed by the firmware build. |

In `firmware/`, RX is implemented in `firmware.c` and `rf_packet_queue.c`; TX is in
`firmware_tx.c`; the common CLI is in `cli_*.c`; board support is in `Board.h` and
`CC1310_LAUNCHXL.*`; radio settings are in `smartrf_settings/`. The boot application linker map is
`firmware/boot_app.cmd`. In `bootloader/`, the primary implementation is `bootloader.c`, with its
Flash placement in `CC1310_LAUNCHXL_NoRTOS.cmd`.

## Build and verification

The supported target is CC1310F128. The current scripts expect TI ARM CGT 18.12.5.LTS and
SimpleLink CC13x0 SDK 4.20.02.07. `bootloader/build.sh` accepts `TI_ARM_CGT` and `SIMPLELINK_SDK`
overrides; `firmware/build.sh` currently uses the workspace-adjacent TI compiler and the SDK path
encoded in that script.

From the workspace root:

```sh
./bootloader/build.sh
./firmware/build.sh
python3 -m unittest discover -s tools/tests -v
```

The firmware build creates these release inputs under `firmware/boot_build/nonrom_test/`:

- `firmware.out` — linked application at `0x00008000`
- `firmware.hex` — address-preserving Intel HEX
- `firmware.pkg` — CRC32-protected UART OTA package, target ID `0x4343314D` (`CC1M`)

Use a new monotonically increasing semantic `FIRMWARE_VERSION` in
`firmware/firmware_build.h` for releases. The build encodes it as `0x00MMmmpp`
(one byte each for major, minor, and patch; the most-significant byte is reserved)
in the package header and verifies the generated package;
it can be checked again with:

```sh
python3 tools/fw_package.py --verify firmware/boot_build/nonrom_test/firmware.pkg
```

There are no target-side unit tests. For firmware changes, build the affected image, run the Python
tests when package/protocol tooling is touched, then validate on hardware: selected RX/TX role,
UART CLI, RF behavior, boot confirmation, and DIO timing where applicable.

## Flashing and OTA safety

`./firmware/flash_all_jlink.sh -r rx|tx` performs a **full-chip erase** and then programs the
bootloader, the `0x8000` app HEX, and verified metadata. It is for factory deployment or recovery.
Specify `-s <JLINK_SERIAL>` when more than one probe is attached. `bootloader/flash_jlink.sh` also
performs a full-chip erase and leaves the application to be reinstalled afterward.

Do not flash `firmware.out` as a zero-address standalone image, and do not flash `firmware.hex` alone:
the app has no CCFG, starts at `0x00008000`, and needs valid boot metadata. Field updates use
`tools/fw_update.py` with `firmware.pkg`; both `flash` and `set-role` require a deliberate `rx` or
`tx` role choice. Treat all probe programming, OTA updates, Flash layout changes, CCFG changes, RF
frequency/power changes, and pin reassignment as hardware-affecting work.

## Generated files and source ownership

Do not hand-edit CCS-generated files under `firmware/Debug/`,
`tirtos_builds_CC1310_LAUNCHXL_release_ccs/Debug/`, or generated XDC/TI-RTOS outputs in
`firmware/boot_build/**/configPkg*` and `firmware/boot_build/nonrom_test/boot_release_cfg.c`.
Change source, `boot_build/boot_release.cfg`, or the build scripts instead. Build outputs such as
`.obj`, `.out`, `.hex`, `.map`, `.bin`, and `.pkg` are generated artifacts unless a repository
explicitly tracks a snapshot.

The bootloader owns Flash `0x00000000–0x00007FFF`, metadata pages at `0x6000` and `0x7000`, and CCFG
at the top of Flash. The application owns `0x00008000–0x0001EFFF`. Preserve this ABI, the fixed
bootloader API address `0x00005000`, and handoff RAM at `0x20004F00` unless the bootloader, app,
tools, metadata format, and documentation are updated together.

## Code and documentation conventions

Keep C compatible with TI ARM CGT and adjacent code: four-space indentation, braces on the same line
as control statements, `lowerCamelCase` for functions/local variables, and `UPPER_SNAKE_CASE` for
macros. Preserve clear comments around RF, interrupt, Flash, and boot-handoff assumptions.

Update the applicable documentation with behavior changes:

- `firmware/cli.md` for CLI behavior and output.
- `firmware/design.md`, `firmware/build_flash.md`, and `firmware/RF_PACKET_DATA_FLOW.md` for firmware
  architecture, build, and data-flow changes.
- `bootloader/BOOTLOADER_DESIGN.md` for metadata, protocol, image-validation, or handoff changes.
- Test plans/reports only with evidence from the corresponding run; do not relabel historical results
  as current coverage.

Keep commits focused and use concise imperative summaries consistent with local history, for example
`Merged rx and tx firmware` or `Updated build scripts`. In a change spanning repositories, inspect and
commit each repository independently; do not assume one root-level commit can capture the workspace.
