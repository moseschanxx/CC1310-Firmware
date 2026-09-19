@echo off
rem Build the combined firmware for the resident UART bootloader at 0x8000.
rem Windows equivalent of build.sh.  Tool locations come from ..\tools\ti_env.bat and can be
rem overridden with TI_ARM_CGT, SIMPLELINK_SDK, XDCTOOLS and PYTHON.  The perl edits that
rem build.sh applies to the XDC-generated files are performed by ..\tools\build_support.py.
rem Exit codes follow build.sh: 2 for a usage or FIRMWARE_VERSION error, 1 for missing tools,
rem and a failing tool's own exit code for every other step.
rem
rem Usage: build.bat [clean]
setlocal EnableExtensions DisableDelayedExpansion
set "script_dir=%~dp0"
set "script_dir=%script_dir:~0,-1%"
for %%I in ("%script_dir%\..") do set "root=%%~fI"
set "version_header=%script_dir%\firmware_build.h"
set "build_dir=%script_dir%\boot_build\nonrom_test"
set "cfg=%script_dir%\boot_build\configPkg_nonrom5"
set "generated_cfg=%root%\tirtos_builds_CC1310_LAUNCHXL_release_ccs\Debug\configPkg"
set "config_source=%build_dir%\boot_release_cfg.c"
set "out_file=%build_dir%\firmware.out"
set "hex_file=%build_dir%\firmware.hex"
set "ota_package=%build_dir%\firmware.pkg"
set "support=%root%\tools\build_support.py"
set "tmp_cmd=%build_dir%\firmware_nonrom.tmp.cmd"
set "tmp_compiler_opt=%build_dir%\firmware_compiler.tmp.opt"
set "tmp_version=%build_dir%\firmware_version.tmp.txt"
set "pushed="

if "%~1"=="clean" goto :clean
if not "%~1"=="" goto :usage
if defined FW_PACKAGE_VERSION goto :legacyVersion

call "%%root%%\tools\ti_env.bat" --xdctools --python
if errorlevel 1 exit /b 1

if not exist "%build_dir%\" mkdir "%build_dir%"
if not exist "%build_dir%\" goto :fail

rem FIRMWARE_VERSION "major.minor.patch" -> "major.minor.patch <code> 0x<code>"; exit 2 if invalid.
call %%PYTHON%% "%%support%%" version "%%version_header%%" > "%tmp_version%" || goto :fail
set "firmware_version="
for /f "usebackq tokens=1-3" %%a in ("%tmp_version%") do (
    set "firmware_version=%%a"
    set "package_version=%%b"
    set "package_version_hex=%%c"
)
del /q "%tmp_version%" 2>nul
if not defined firmware_version goto :fail

if exist "%cfg%\compiler.opt" goto :haveCfg
if not exist "%generated_cfg%\" goto :missingGeneratedCfg
xcopy /e /i /h /q /y "%generated_cfg%" "%cfg%" >nul || goto :fail
if exist "%cfg%\package\cfg\release_pem3.c" copy /y "%cfg%\package\cfg\release_pem3.c" "%cfg%\package\cfg\boot_release_pem3.c" >nul
:haveCfg
if not exist "%cfg%\linker.cmd" goto :incompleteCfg
if not exist "%cfg%\compiler.opt" goto :incompleteCfg
if not exist "%cfg%\package\cfg\boot_release_pem3.c" goto :incompleteCfg

rem Point every generated path at this PC, rebase the linker command file to 0x8000 and patch
rem the generated configuration source (the perl steps of build.sh).
call %%PYTHON%% "%%support%%" linker-cmd --input "%%cfg%%\linker.cmd" --output "%%tmp_cmd%%" ^
    --sdk "%%SIMPLELINK_SDK%%" --xdctools "%%XDCTOOLS%%" --cgt "%%TI_ARM_CGT%%" --cfg "%%cfg%%" ^
    --boot-config-object "%%build_dir%%\boot_release_cfg.obj" ^
    --boot-config-sysbios "%%cfg%%\package\cfg\boot_release_pem3.src\sysbios\sysbios.aem3" || goto :fail
call %%PYTHON%% "%%support%%" compiler-opt --input "%%cfg%%\compiler.opt" --output "%%tmp_compiler_opt%%" ^
    --sdk "%%SIMPLELINK_SDK%%" --xdctools "%%XDCTOOLS%%" --cgt "%%TI_ARM_CGT%%" --cfg "%%cfg%%" || goto :fail
call %%PYTHON%% "%%support%%" config-source --input "%%cfg%%\package\cfg\boot_release_pem3.c" --output "%%config_source%%" || goto :fail

pushd "%build_dir%" || goto :fail
set "pushed=1"

rem Every path stays inside its own quotes on this line, so & or ^ in a path cannot reach cmd.exe.
set flags=-mv7M3 --code_state=16 --float_support=vfplib -me --include_path="%script_dir%" --include_path="%SIMPLELINK_SDK%\source\ti\posix\ccs" --include_path="%TI_ARM_CGT%\include" --cmd_file="%tmp_compiler_opt%" --define=DeviceFamily_CC13X0 --define=FIRMWARE_VERSION_CODE=%package_version%

"%TI_ARMCL%" %flags% -c "%script_dir%\CC1310_LAUNCHXL.c" "%script_dir%\CC1310_LAUNCHXL_fxns.c" ^
    "%script_dir%\RFQueue.c" "%script_dir%\cli_core.c" "%script_dir%\cli_radio.c" ^
    "%script_dir%\cli_bootloader.c" "%script_dir%\cli_task_cc1310.c" "%script_dir%\main_tirtos.c" ^
    "%script_dir%\firmware.c" "%script_dir%\firmware_mode.c" "%script_dir%\firmware_startup.c" ^
    "%script_dir%\firmware_tx.c" "%script_dir%\rf_packet_queue.c" "%script_dir%\i2c_slave.c" ^
    "%script_dir%\boot_api.c" "%script_dir%\boot_nonrom_startup.c" ^
    "%script_dir%\smartrf_settings\smartrf_settings.c" || goto :fail
"%TI_ARMCL%" %flags% -c "%config_source%" --obj_directory="%build_dir%" --output_file=boot_release_cfg.obj || goto :fail
"%TI_ARMCL%" -mv7M3 --code_state=16 --float_support=vfplib -me -z --rom_model -m firmware.map ^
    -i"%SIMPLELINK_SDK%\source" -i"%SIMPLELINK_SDK%\kernel\tirtos\packages" -i"%TI_ARM_CGT%\lib" ^
    -o firmware.out CC1310_LAUNCHXL.obj CC1310_LAUNCHXL_fxns.obj RFQueue.obj cli_core.obj cli_radio.obj ^
    cli_bootloader.obj cli_task_cc1310.obj main_tirtos.obj firmware.obj firmware_mode.obj ^
    firmware_startup.obj firmware_tx.obj rf_packet_queue.obj i2c_slave.obj boot_api.obj ^
    boot_nonrom_startup.obj smartrf_settings.obj "%script_dir%\boot_app.cmd" ^
    -lti/display/lib/display.aem3 -lti/grlib/lib/ccs/m3/grlib.a ^
    -lthird_party/spiffs/lib/ccs/m3/spiffs_cc26xx.a -lti/drivers/rf/lib/rf_multiMode_cc13x0.aem3 ^
    -lti/drivers/lib/drivers_cc13x0.aem3 -lti/dpl/lib/dpl_cc13x0.aem3 -l"%tmp_cmd%" ^
    -lti/devices/cc13x0/driverlib/bin/ccs/driverlib.lib -llibc.a || goto :fail
"%TI_ARMHEX%" --byte --memwidth=8 --romwidth=8 --intel --outfile=firmware.hex firmware.out || goto :fail
call %%PYTHON%% "%%root%%\tools\ihex_to_bin.py" --input firmware.hex --output firmware.bin --base 0x8000 --end 0x1f000 || goto :fail
call %%PYTHON%% "%%root%%\tools\fw_package.py" --input firmware.bin --output firmware.pkg --target-id 0x4343314d --version %%package_version%% || goto :fail
popd
set "pushed="

call :requireArtifact out_file || goto :fail
call :requireArtifact hex_file || goto :fail
call :requireArtifact ota_package || goto :fail
call %%PYTHON%% "%%root%%\tools\fw_package.py" --verify "%%ota_package%%" || goto :fail
call :removeTemporaries
setlocal EnableDelayedExpansion
echo Firmware OUT: !out_file!
echo Firmware HEX: !hex_file!
echo Firmware OTA package: !ota_package! (version !firmware_version!, code !package_version_hex!)
endlocal
exit /b 0

:requireArtifact
rem %1 names the variable holding the path (passing the path itself through CALL would
rem re-expand any %% it contains).  The artifact must exist and be non-empty.
call set "artifact=%%%~1%%"
if not exist "%artifact%" goto :artifactMissing
for %%A in ("%artifact%") do if %%~zA EQU 0 goto :artifactMissing
exit /b 0
:artifactMissing
setlocal EnableDelayedExpansion
>&2 echo Expected firmware artifact is missing or empty: !artifact!
endlocal
exit /b 1

:removeTemporaries
del /q "%tmp_cmd%" "%tmp_compiler_opt%" "%tmp_version%" 2>nul
exit /b 0

:usage
>&2 echo Usage: %~nx0 [clean]
exit /b 2

:clean
rem Like build.sh: delete generated objects, images, maps and packages in the build directory
rem only and keep the versioned boot_release_cfg.c.  The extension is compared on the long
rem file name so 8.3 short names cannot widen the match.
if not exist "%build_dir%\" goto :cleanDone
for %%F in ("%build_dir%\*") do for %%E in (.obj .out .map .hex .bin .pkg .d .d_raw) do if /i "%%~xF"=="%%E" del /q "%%~fF"
call :removeTemporaries
:cleanDone
setlocal EnableDelayedExpansion
echo Removed generated artifacts from !build_dir!
endlocal
exit /b 0

:legacyVersion
setlocal EnableDelayedExpansion
>&2 echo FW_PACKAGE_VERSION is no longer supported; update FIRMWARE_VERSION in !version_header!
endlocal
exit /b 2

:missingGeneratedCfg
setlocal EnableDelayedExpansion
>&2 echo Missing TI-RTOS configPkg: !generated_cfg!
endlocal
exit /b 1

:incompleteCfg
setlocal EnableDelayedExpansion
>&2 echo Incomplete non-ROM TI-RTOS configuration: !cfg!
endlocal
exit /b 1

:fail
set "status=%ERRORLEVEL%"
if "%status%"=="0" set "status=1"
if defined pushed popd
call :removeTemporaries
>&2 echo Firmware build failed.
exit /b %status%
