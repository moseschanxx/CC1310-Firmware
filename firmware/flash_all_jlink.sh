#!/usr/bin/env bash
# Program the complete CC1310F128 image: bootloader plus combined application.
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: flash_all_jlink.sh [-s JLINK_SERIAL] [-b BOOTLOADER_OUT] [-a APP_HEX] [-p APP_PACKAGE] [-r rx|tx]

Erase the CC1310F128 and program both images over cJTAG.

Options:
  -s JLINK_SERIAL  Select a specific J-Link probe serial number.
  -b BOOTLOADER_OUT Bootloader ELF/OUT image.
                    Default: bootloader/build/bootloader.out
  -a APP_HEX        App Intel HEX image with records beginning at 0x00008000.
                    Default: firmware/boot_build/nonrom_test/firmware.hex
  -p APP_PACKAGE    Matching CRC32 OTA package used to create valid boot metadata.
                    Default: firmware/boot_build/nonrom_test/firmware.pkg
  -r ROLE           Startup role recorded in metadata: rx (default) or tx.
  -h                Show this help.

Environment:
  JLINK_BIN         J-Link Commander executable (default: JLinkExe).
  JLINK_SPEED       cJTAG clock in kHz (default: 1000).

The script performs a chip erase. It verifies that APP_HEX exactly matches
APP_PACKAGE, then writes a valid metadata record so the bootloader can start
the app. Do not use this path for field updates; use the UART OTA client.
EOF
}

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
repo_root=$(cd "${script_dir}/.." && pwd)
bootloader_image="${repo_root}/bootloader/build/bootloader.out"
app_image="${script_dir}/boot_build/nonrom_test/firmware.hex"
app_package="${script_dir}/boot_build/nonrom_test/firmware.pkg"
jlink_serial=""
role="rx"

while getopts ":s:b:a:p:r:h" option; do
    case "${option}" in
        s) jlink_serial="${OPTARG}" ;;
        b) bootloader_image="${OPTARG}" ;;
        a) app_image="${OPTARG}" ;;
        p) app_package="${OPTARG}" ;;
        r) role="${OPTARG}" ;;
        h)
            usage
            exit 0
            ;;
        :)
            echo "Missing value for -${OPTARG}" >&2
            usage >&2
            exit 2
            ;;
        *)
            usage >&2
            exit 2
            ;;
    esac
done

[[ "${role}" == "rx" || "${role}" == "tx" ]] || { echo "ROLE must be rx or tx" >&2; exit 2; }

for image in "${bootloader_image}" "${app_image}" "${app_package}"; do
    [[ -f "${image}" ]] || {
        echo "Firmware image not found: ${image}" >&2
        exit 1
    }
done

jlink_bin="${JLINK_BIN:-JLinkExe}"
jlink_speed="${JLINK_SPEED:-1000}"
command -v "${jlink_bin}" >/dev/null || {
    echo "Cannot find ${jlink_bin}. Install J-Link or set JLINK_BIN." >&2
    exit 127
}

bootloader_abs=$(cd "$(dirname "${bootloader_image}")" && pwd)/$(basename "${bootloader_image}")
app_abs=$(cd "$(dirname "${app_image}")" && pwd)/$(basename "${app_image}")
package_abs=$(cd "$(dirname "${app_package}")" && pwd)/$(basename "${app_package}")
for image in "${bootloader_abs}" "${app_abs}" "${package_abs}"; do
    [[ "${image}" != *$'\n'* && "${image}" != *'"'* ]] || {
        echo "Firmware path must not contain a quote or newline." >&2
        exit 2
    }
done

# macOS mktemp requires the XXXXXX template at the end, while J-Link uses the
# .hex suffix to select the parser. Reserve a unique base name, then use its
# sibling file with the required suffix.
# Keep this path short: J-Link Commander truncates long macOS TMPDIR paths
# when parsing a loadfile command.
metadata_base=$(mktemp "/tmp/cc1310-metadata.XXXXXX")
metadata_hex="${metadata_base}.hex"
rm -f "${metadata_base}"
python3 "${repo_root}/tools/fw_metadata_hex.py" --package "${package_abs}" \
    --app-hex "${app_abs}" --role "${role}" --output "${metadata_hex}"

command_file=$(mktemp "${TMPDIR:-/tmp}/cc1310-flash-all-jlink.XXXXXX")
trap 'rm -f "${command_file}" "${metadata_base}" "${metadata_hex}"' EXIT

# The bootloader OUT loads at flash zero. The app uses Intel HEX rather than
# TI COFF OUT so J-Link receives its explicit 0x8000 record addresses.
# The verified metadata record is required after a chip erase before the
# bootloader will start that raw app image.
{
    printf '%s\n' 'connect' 'erase'
    printf 'loadfile "%s"\n' "${bootloader_abs}"
    printf 'loadfile "%s"\n' "${app_abs}"
    printf 'loadfile "%s"\n' "${metadata_hex}"
    printf '%s\n' 'r' 'g' 'exit'
} > "${command_file}"

jlink_args=(
    -Device CC1310F128
    -If cJTAG
    -Speed "${jlink_speed}"
    -AutoConnect 1
    -JTAGConf -1,-1
    -ExitOnError 1
    -NoGui 1
    -CommandFile "${command_file}"
)
[[ -n "${jlink_serial}" ]] && jlink_args+=(-USB "${jlink_serial}")

echo "Programming bootloader: ${bootloader_abs}"
echo "Programming combined app HEX: ${app_abs} (starts at 0x00008000)"
echo "Programming validated boot metadata: role=${role}, package=${package_abs}"
"${jlink_bin}" "${jlink_args[@]}"
echo "Bootloader and combined app flashed; target restarted."
