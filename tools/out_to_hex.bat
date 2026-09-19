@echo off
rem Convert a TI ARM linker .out file to an Intel HEX file for CC13xx flashing.
rem Windows equivalent of out_to_hex.sh.
rem
rem Usage: out_to_hex.bat INPUT.out OUTPUT.hex
rem
rem armhex.exe is taken from ARMHEX, else TI_ARM_CGT_ROOT\bin or TI_ARM_CGT\bin, else PATH.
setlocal EnableExtensions DisableDelayedExpansion

if "%~2"=="" goto :usage
if not "%~3"=="" goto :usage
set "input_out=%~f1"
set "output_hex=%~f2"
if /i not "%~x1"==".out" goto :badInput
if /i not "%~x2"==".hex" goto :badOutput
if exist "%input_out%\" goto :missingInput
if not exist "%input_out%" goto :missingInput

rem Note: cmd.exe variable names are case-insensitive, so the local must not be called "armhex".
set "armhex_exe="
if defined ARMHEX set "armhex_exe=%ARMHEX:"=%"
if not defined armhex_exe if defined TI_ARM_CGT_ROOT set "armhex_exe=%TI_ARM_CGT_ROOT:"=%\bin\armhex.exe"
if not defined armhex_exe if defined TI_ARM_CGT set "armhex_exe=%TI_ARM_CGT:"=%\bin\armhex.exe"
if not defined armhex_exe for %%P in (armhex.exe) do set "armhex_exe=%%~$PATH:P"
if not defined armhex_exe goto :noArmhex
if not exist "%armhex_exe%" if exist "%armhex_exe%.exe" set "armhex_exe=%armhex_exe%.exe"
if not exist "%armhex_exe%" goto :noArmhex

"%armhex_exe%" --intel --byte --memwidth=8 --romwidth=8 --outfile="%output_hex%" "%input_out%" || exit /b 1
setlocal EnableDelayedExpansion
echo Created: !output_hex!
endlocal
exit /b 0

:usage
>&2 echo Usage: %~nx0 INPUT.out OUTPUT.hex
exit /b 2

:badInput
set "shown=%~1"
setlocal EnableDelayedExpansion
>&2 echo error: input must be a TI linker .out file: !shown!
endlocal
exit /b 2

:badOutput
set "shown=%~2"
setlocal EnableDelayedExpansion
>&2 echo error: output must have a .hex suffix: !shown!
endlocal
exit /b 2

:missingInput
setlocal EnableDelayedExpansion
>&2 echo error: input .out file not found: !input_out!
endlocal
exit /b 1

:noArmhex
>&2 echo error: TI armhex.exe not found. Set ARMHEX or TI_ARM_CGT_ROOT.
exit /b 1
