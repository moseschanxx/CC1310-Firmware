################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Each subdirectory must supply rules for building sources it contributes
%.obj: ../%.c $(GEN_OPTS) | $(GEN_FILES) $(GEN_MISC_FILES)
	@echo 'Arm Compiler - building file: "$<"'
	"/Users/xiulin/ti/ti-cgt-arm_18.12.5.LTS/bin/armcl" -mv7M3 --code_state=16 --float_support=vfplib -me --include_path="/Users/xiulin/workspace_ccstheia/bootloader" --include_path="/Applications/ti/simplelink_cc13x0_sdk_4_20_02_07/source" --include_path="/Applications/ti/simplelink_cc13x0_sdk_4_20_02_07/kernel/nortos" --include_path="/Applications/ti/simplelink_cc13x0_sdk_4_20_02_07/kernel/nortos/posix" --include_path="/Users/xiulin/ti/ti-cgt-arm_18.12.5.LTS/include" --define=DeviceFamily_CC13X0 -g --diag_warning=225 --diag_warning=255 --diag_wrap=off --display_error_number --gen_func_subsections=on --preproc_with_compile --preproc_dependency="$(basename $(<F)).d_raw" $(GEN_OPTS__FLAG) "$<"
	@echo 'Finished building: "$<"'
	@echo ' '


