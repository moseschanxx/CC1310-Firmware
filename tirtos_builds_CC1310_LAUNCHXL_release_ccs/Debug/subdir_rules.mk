################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Each subdirectory must supply rules for building sources it contributes
build-1528256765:
	@$(MAKE) --no-print-directory -Onone -f subdir_rules.mk build-1528256765-inproc

build-1528256765-inproc: ../release.cfg
	@echo 'XDCtools - building file: "$<"'
	"/Applications/ti/xdctools_3_51_03_28_core/xs" --xdcpath="/Applications/ti/simplelink_cc13x0_sdk_4_20_02_07/source;/Applications/ti/simplelink_cc13x0_sdk_4_20_02_07/kernel/tirtos/packages;" xdc.tools.configuro -o configPkg -t ti.targets.arm.elf.M3 -p ti.platforms.simplelink:CC1310F128 -r release -c "/Users/xiulin/ti/ti-cgt-arm_18.12.5.LTS" --compileOptions "-DDeviceFamily_CC13X0" "$<"
	@echo 'Finished building: "$<"'
	@echo ' '

configPkg/linker.cmd: build-1528256765 ../release.cfg
configPkg/compiler.opt: build-1528256765
configPkg: build-1528256765


