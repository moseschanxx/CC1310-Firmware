#!/usr/bin/env bash
# Build the combined firmware for the resident UART bootloader at 0x8000.
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
root="$(cd "${script_dir}/.." && pwd)"
package_version="${FW_PACKAGE_VERSION:-110}"
build_dir="${script_dir}/boot_build/nonrom_test"
tool_root="${TI_ARM_CGT:-${root}/toolchains/ti-cgt-arm_18.12.5.LTS}"
sdk="${SIMPLELINK_SDK:-/Applications/ti/simplelink_cc13x0_sdk_4_20_02_07}"
tool="${tool_root}/bin/armcl"
hex="${tool_root}/bin/armhex"
cfg="${script_dir}/boot_build/configPkg_nonrom5"
generated_cfg="${root}/tirtos_builds_CC1310_LAUNCHXL_release_ccs/Debug/configPkg"
config_source="${build_dir}/boot_release_cfg.c"
out_file="${build_dir}/firmware.out"
hex_file="${build_dir}/firmware.hex"
ota_package="${build_dir}/firmware.pkg"

usage() {
    echo "Usage: ${0##*/} [clean]" >&2
}

cleanBuild() {
    if [[ -d "${build_dir}" ]]; then
        find "${build_dir}" -maxdepth 1 -type f \( \
            -name '*.obj' -o -name '*.out' -o -name '*.map' -o -name '*.hex' \
            -o -name '*.bin' -o -name '*.pkg' -o -name '*.d' -o -name '*.d_raw' \
        \) -delete
    fi
    echo "Removed generated artifacts from ${build_dir}"
}

case "${1:-}" in
    clean)
        cleanBuild
        exit 0
        ;;
    "")
        ;;
    *)
        usage
        exit 2
        ;;
esac

[[ -x "${tool}" && -x "${hex}" && -d "${sdk}" ]] || {
    echo "Missing TI compiler, HEX tool, or SDK (TI_ARM_CGT=${tool_root}, SIMPLELINK_SDK=${sdk})" >&2
    exit 1
}

mkdir -p "${build_dir}"
if [[ ! -f "${cfg}/compiler.opt" ]]; then
    [[ -d "${generated_cfg}" ]] || { echo "Missing TI-RTOS configPkg: ${generated_cfg}" >&2; exit 1; }
    cp -R "${generated_cfg}" "${cfg}"
    [[ ! -f "${cfg}/package/cfg/release_pem3.c" ]] || cp "${cfg}/package/cfg/release_pem3.c" "${cfg}/package/cfg/boot_release_pem3.c"
fi
[[ -f "${cfg}/linker.cmd" && -f "${cfg}/compiler.opt" && -f "${cfg}/package/cfg/boot_release_pem3.c" ]] || {
    echo "Incomplete non-ROM TI-RTOS configuration: ${cfg}" >&2; exit 1;
}

tmp_cmd="$(mktemp /private/tmp/firmware_nonrom.XXXXXX.cmd)"
tmp_compiler_opt="$(mktemp /private/tmp/firmware_compiler.XXXXXX.opt)"
trap 'rm -f "${tmp_cmd}" "${tmp_compiler_opt}"' EXIT
perl -pe 's/0x0000([0-9A-Fa-f]{4})/sprintf("0x%08X", hex($&)+0x8000)/eg; s/(\.resetVecs: load > )0x[0-9A-Fa-f]+/${1}0x00008000/' "${cfg}/linker.cmd" > "${tmp_cmd}"
perl -pi -e 's%/rfPacketRx/%/firmware/%g' "${tmp_cmd}"
BOOT_CONFIG_OBJECT="${build_dir}/boot_release_cfg.obj" \
BOOT_CONFIG_SYSBIOS="${cfg}/package/cfg/boot_release_pem3.src/sysbios/sysbios.aem3" \
    perl -pi -e 's{-l"[^"]*/(?:boot_)?release_pem3\.oem3"}{q(-l") . $ENV{BOOT_CONFIG_OBJECT} . q(")}e; s{-l"[^"]*/boot_release_pem3\.src/sysbios/sysbios\.aem3"}{q(-l") . $ENV{BOOT_CONFIG_SYSBIOS} . q(")}e' "${tmp_cmd}"
TOOL_ROOT="${tool_root}" perl -pe 's%-I[^[:space:]"]*/ti-cgt-arm_18\.12\.5\.LTS/include%q(-I) . $ENV{TOOL_ROOT} . q(/include)%eg' \
    "${cfg}/compiler.opt" > "${tmp_compiler_opt}"

cd "${build_dir}"
perl -pe 's/extern Void _c_int00\(\);/extern Void ResetISR();\nextern Void _c_int00();/; s/\(UInt32\)\(&_c_int00\),/(UInt32)\(&ResetISR),/; s/Void ResetISR \(\)/Void generatedResetISR ()/' \
    "${cfg}/package/cfg/boot_release_pem3.c" > "${config_source}"
flags=(-mv7M3 --code_state=16 --float_support=vfplib -me --include_path="${script_dir}" --include_path="${sdk}/source/ti/posix/ccs" --include_path="${tool_root}/include" --cmd_file="${tmp_compiler_opt}" --define=DeviceFamily_CC13X0)
"${tool}" "${flags[@]}" -c "${script_dir}/CC1310_LAUNCHXL.c" "${script_dir}/CC1310_LAUNCHXL_fxns.c" "${script_dir}/RFQueue.c" "${script_dir}/cli_core.c" "${script_dir}/cli_radio.c" "${script_dir}/cli_bootloader.c" "${script_dir}/cli_task_cc1310.c" "${script_dir}/main_tirtos.c" "${script_dir}/firmware.c" "${script_dir}/firmware_mode.c" "${script_dir}/firmware_tx.c" "${script_dir}/rf_packet_queue.c" "${script_dir}/boot_api.c" "${script_dir}/boot_nonrom_startup.c" "${script_dir}/smartrf_settings/smartrf_settings.c"
"${tool}" "${flags[@]}" -c "${config_source}" --obj_directory="${build_dir}" --output_file=boot_release_cfg.obj
"${tool}" -mv7M3 --code_state=16 --float_support=vfplib -me -z --rom_model -m firmware.map -i"${sdk}/source" -i"${sdk}/kernel/tirtos/packages" -i"${tool_root}/lib" -o firmware.out CC1310_LAUNCHXL.obj CC1310_LAUNCHXL_fxns.obj RFQueue.obj cli_core.obj cli_radio.obj cli_bootloader.obj cli_task_cc1310.obj main_tirtos.obj firmware.obj firmware_mode.obj firmware_tx.obj rf_packet_queue.obj boot_api.obj boot_nonrom_startup.obj smartrf_settings.obj "${script_dir}/boot_app.cmd" -lti/display/lib/display.aem3 -lti/grlib/lib/ccs/m3/grlib.a -lthird_party/spiffs/lib/ccs/m3/spiffs_cc26xx.a -lti/drivers/rf/lib/rf_multiMode_cc13x0.aem3 -lti/drivers/lib/drivers_cc13x0.aem3 -lti/dpl/lib/dpl_cc13x0.aem3 -l"${tmp_cmd}" -lti/devices/cc13x0/driverlib/bin/ccs/driverlib.lib -llibc.a
"${hex}" --byte --memwidth=8 --romwidth=8 --intel --outfile=firmware.hex firmware.out
python3 "${root}/tools/ihex_to_bin.py" --input firmware.hex --output firmware.bin --base 0x8000 --end 0x1f000
python3 "${root}/tools/fw_package.py" --input firmware.bin --output firmware.pkg --target-id 0x4343314d --version "${package_version}"

for artifact in "${out_file}" "${hex_file}" "${ota_package}"; do
    [[ -s "${artifact}" ]] || { echo "Expected firmware artifact is missing: ${artifact}" >&2; exit 1; }
done
python3 "${root}/tools/fw_package.py" --verify "${ota_package}"
echo "Firmware OUT: ${out_file}"
echo "Firmware HEX: ${hex_file}"
echo "Firmware OTA package: ${ota_package} (version ${package_version})"
