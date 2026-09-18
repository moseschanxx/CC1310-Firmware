#!/usr/bin/env bash
# Build from source without modifying CCS-generated Debug files.
set -euo pipefail
script_dir="$(cd "$(dirname "$0")" && pwd)"
workspace_root="$(cd "${script_dir}/.." && pwd)"
tool_root="${TI_ARM_CGT:-${workspace_root}/toolchains/ti-cgt-arm_18.12.5.LTS}"
sdk_root="${SIMPLELINK_SDK:-/Applications/ti/simplelink_cc13x0_sdk_4_20_02_07}"
output_dir="$script_dir/build"
compiler="$tool_root/bin/armcl"

usage() {
  echo "Usage: $0 [clean]" >&2
}

clean_build() {
  [[ ! -d "$output_dir" ]] || rm -rf -- "$output_dir"
  echo "Removed $output_dir"
}

case "${1:-}" in
  clean)
    clean_build
    exit 0
    ;;
  "")
    ;;
  *)
    usage
    exit 2
    ;;
esac

[[ -x "$compiler" && -d "$sdk_root" ]] || { echo "Missing TI compiler or SDK" >&2; exit 1; }
mkdir -p "$output_dir"
rm -f "$output_dir"/*.obj "$output_dir"/bootloader.out "$output_dir"/bootloader.map
options=(-mv7M3 --code_state=16 --float_support=vfplib -me --include_path="$script_dir"
  --include_path="$sdk_root/source" --include_path="$sdk_root/kernel/nortos"
  --include_path="$sdk_root/kernel/nortos/posix" --include_path="$tool_root/include"
  --define=DeviceFamily_CC13X0 --opt_level=2 --gen_func_subsections=on --display_error_number)
for source in CC1310_LAUNCHXL.c CC1310_LAUNCHXL_fxns.c ccfg.c main_nortos.c bootloader.c; do
  "$compiler" "${options[@]}" -c "$script_dir/$source" --obj_directory="$output_dir"
done
"$compiler" -mv7M3 --code_state=16 --float_support=vfplib -me --define=DeviceFamily_CC13X0 -z --rom_model --warn_sections \
  -m"$output_dir/bootloader.map" -o "$output_dir/bootloader.out" -i"$sdk_root/source" -i"$sdk_root/kernel/nortos" -i"$tool_root/lib" \
  "$output_dir/CC1310_LAUNCHXL.obj" "$output_dir/CC1310_LAUNCHXL_fxns.obj" "$output_dir/ccfg.obj" "$output_dir/main_nortos.obj" "$output_dir/bootloader.obj" \
  "$script_dir/CC1310_LAUNCHXL_NoRTOS.cmd" -lti/display/lib/display.aem3 -lti/grlib/lib/ccs/m3/grlib.a -lthird_party/spiffs/lib/ccs/m3/spiffs_cc26xx.a \
  -lti/drivers/rf/lib/rf_multiMode_cc13x0.aem3 -lti/drivers/lib/drivers_cc13x0.aem3 -llib/nortos_cc13x0.aem3 \
  -lti/devices/cc13x0/driverlib/bin/ccs/driverlib.lib -llibc.a
ARMHEX="${tool_root}/bin/armhex" "${script_dir}/../tools/out_to_hex.sh" \
  "${output_dir}/bootloader.out" "${output_dir}/bootloader.hex"
echo "Built ${output_dir}/bootloader.out and ${output_dir}/bootloader.hex"
