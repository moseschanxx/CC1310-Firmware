#!/usr/bin/env bash
# Convert a TI ARM linker .out file to an Intel HEX file for CC13xx flashing.
# Usage: ./out_to_hex.sh INPUT.out OUTPUT.hex

set -euo pipefail

if [[ $# -ne 2 ]]; then
    echo "Usage: $(basename "$0") INPUT.out OUTPUT.hex" >&2
    exit 2
fi

input_out="$1"
output_hex="$2"

[[ "${input_out}" == *.out ]] || {
    echo "error: input must be a TI linker .out file: ${input_out}" >&2
    exit 2
}
[[ "${output_hex}" == *.hex ]] || {
    echo "error: output must have a .hex suffix: ${output_hex}" >&2
    exit 2
}

if [[ ! -f "${input_out}" ]]; then
    echo "error: input .out file not found: ${input_out}" >&2
    exit 1
fi

if [[ -n "${ARMHEX:-}" ]]; then
    armhex="${ARMHEX}"
elif [[ -n "${TI_ARM_CGT_ROOT:-}" ]]; then
    armhex="${TI_ARM_CGT_ROOT}/bin/armhex"
else
    armhex="$(command -v armhex || true)"
fi

if [[ -z "${armhex}" || ! -x "${armhex}" ]]; then
    echo "error: TI armhex not found. Set ARMHEX or TI_ARM_CGT_ROOT." >&2
    exit 1
fi

"${armhex}" --intel --byte --memwidth=8 --romwidth=8 \
    --outfile="${output_hex}" "${input_out}"

echo "Created: ${output_hex}"
