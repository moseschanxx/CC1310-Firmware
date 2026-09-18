#!/usr/bin/env bash
# Program the CC1310F128 bootloader through a SEGGER J-Link probe.
#
# This intentionally performs a chip erase: after programming the bootloader,
# install the application again with the UART OTA update tool.
set -euo pipefail

usage() {
    cat <<'EOF'
Usage: flash-jlink.sh [-s JLINK_SERIAL] [-f IMAGE]

Program a CC1310F128 bootloader over cJTAG.

Options:
  -s JLINK_SERIAL  Select a specific J-Link probe serial number.
  -f IMAGE         ELF/OUT image to program (default: build/bootloader.out).
  -h               Show this help.

Environment:
  JLINK_BIN        J-Link Commander executable (default: JLinkExe).
  JLINK_SPEED      cJTAG clock in kHz (default: 1000).

The script erases the whole device before programming. Reinstall the
application afterward with the bootloader UART update client.
EOF
}

script_dir=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
image="${script_dir}/build/bootloader.out"
jlink_serial=""

while getopts ":s:f:h" option; do
    case "${option}" in
        s) jlink_serial="${OPTARG}" ;;
        f) image="${OPTARG}" ;;
        h)
            usage
            exit 0
            ;;
        *)
            usage >&2
            exit 2
            ;;
    esac
done

if [[ ! -f "${image}" ]]; then
    echo "Bootloader image not found: ${image}" >&2
    echo "Build it first with: bash build_bootloader.sh" >&2
    exit 1
fi

jlink_bin="${JLINK_BIN:-JLinkExe}"
jlink_speed="${JLINK_SPEED:-1000}"
image_abs=$(cd "$(dirname "${image}")" && pwd)/$(basename "${image}")
command_file=$(mktemp "${TMPDIR:-/tmp}/cc1310-bootloader-jlink.XXXXXX")
trap 'rm -f "${command_file}"' EXIT

{
    printf '%s\n' 'connect' 'erase'
    printf 'loadfile "%s"\n' "${image_abs}"
    printf '%s\n' 'r' 'g' 'exit'
} > "${command_file}"

jlink_args=(
    -Device CC1310F128
    -If cJTAG
    -Speed "${jlink_speed}"
    -AutoConnect 1
    -JTAGConf -1,-1
    -NoGui 1
    -CommandFile "${command_file}"
)

if [[ -n "${jlink_serial}" ]]; then
    jlink_args+=(-USB "${jlink_serial}")
fi

echo "Programming ${image_abs} via J-Link (CC1310F128, cJTAG, ${jlink_speed} kHz) ..."
"${jlink_bin}" "${jlink_args[@]}"
echo "Bootloader flash completed and target restarted."
