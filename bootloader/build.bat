@echo off
rem Build the CC1310F128 bootloader from source without modifying CCS-generated Debug files.
rem Windows equivalent of build.sh.  TI_ARM_CGT and SIMPLELINK_SDK override the tool locations
rem resolved by ..\tools\ti_env.bat; Python is not needed for this build.
rem
rem Usage: build.bat [clean]
setlocal EnableExtensions DisableDelayedExpansion
set "script_dir=%~dp0"
set "script_dir=%script_dir:~0,-1%"
for %%I in ("%script_dir%\..") do set "workspace_root=%%~fI"
set "output_dir=%script_dir%\build"

if "%~1"=="clean" goto :clean
if not "%~1"=="" goto :usage

call "%%workspace_root%%\tools\ti_env.bat"
if errorlevel 1 exit /b 1

if not exist "%output_dir%\" mkdir "%output_dir%"
if not exist "%output_dir%\" goto :fail
rem Remove stale objects and images; the extension is compared on the long file name.
for %%F in ("%output_dir%\*") do if /i "%%~xF"==".obj" del /q "%%~fF"
del /q "%output_dir%\bootloader.out" "%output_dir%\bootloader.map" 2>nul

rem Every path stays inside its own quotes on this line, so & or ^ in a path cannot reach cmd.exe.
set options=-mv7M3 --code_state=16 --float_support=vfplib -me --include_path="%script_dir%" --include_path="%SIMPLELINK_SDK%\source" --include_path="%SIMPLELINK_SDK%\kernel\nortos" --include_path="%SIMPLELINK_SDK%\kernel\nortos\posix" --include_path="%TI_ARM_CGT%\include" --define=DeviceFamily_CC13X0 --opt_level=2 --gen_func_subsections=on --display_error_number

call :compile CC1310_LAUNCHXL.c || goto :fail
call :compile CC1310_LAUNCHXL_fxns.c || goto :fail
call :compile ccfg.c || goto :fail
call :compile main_nortos.c || goto :fail
call :compile bootloader.c || goto :fail

echo Linking bootloader.out
"%TI_ARMCL%" -mv7M3 --code_state=16 --float_support=vfplib -me --define=DeviceFamily_CC13X0 -z --rom_model --warn_sections ^
    -m"%output_dir%\bootloader.map" -o "%output_dir%\bootloader.out" ^
    -i"%SIMPLELINK_SDK%\source" -i"%SIMPLELINK_SDK%\kernel\nortos" -i"%TI_ARM_CGT%\lib" ^
    "%output_dir%\CC1310_LAUNCHXL.obj" "%output_dir%\CC1310_LAUNCHXL_fxns.obj" "%output_dir%\ccfg.obj" ^
    "%output_dir%\main_nortos.obj" "%output_dir%\bootloader.obj" ^
    "%script_dir%\CC1310_LAUNCHXL_NoRTOS.cmd" ^
    -lti/display/lib/display.aem3 -lti/grlib/lib/ccs/m3/grlib.a -lthird_party/spiffs/lib/ccs/m3/spiffs_cc26xx.a ^
    -lti/drivers/rf/lib/rf_multiMode_cc13x0.aem3 -lti/drivers/lib/drivers_cc13x0.aem3 -llib/nortos_cc13x0.aem3 ^
    -lti/devices/cc13x0/driverlib/bin/ccs/driverlib.lib -llibc.a || goto :fail

set "ARMHEX=%TI_ARMHEX%"
call "%%workspace_root%%\tools\out_to_hex.bat" "%%output_dir%%\bootloader.out" "%%output_dir%%\bootloader.hex" || goto :fail
setlocal EnableDelayedExpansion
echo Built !output_dir!\bootloader.out and !output_dir!\bootloader.hex
endlocal
exit /b 0

:compile
echo Compiling %~1
"%TI_ARMCL%" %options% -c "%script_dir%\%~1" --obj_directory="%output_dir%" || exit /b
exit /b 0

:usage
>&2 echo Usage: %~nx0 [clean]
exit /b 2

:clean
if exist "%output_dir%\" rd /s /q "%output_dir%"
setlocal EnableDelayedExpansion
echo Removed !output_dir!
endlocal
exit /b 0

:fail
set "status=%ERRORLEVEL%"
if "%status%"=="0" set "status=1"
>&2 echo Bootloader build failed.
exit /b %status%
