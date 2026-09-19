@echo off
rem Resolve the TI toolchain, SimpleLink SDK, XDCtools and Python for the Windows build scripts.
rem
rem This file is CALLed by bootloader\build.bat and firmware\build.bat; it is not run on its own.
rem It sets these variables in the caller's environment and returns 0, or prints what is missing
rem and returns 1:
rem
rem   TI_ARM_CGT      TI ARM CGT 18.12.5.LTS root.  Default: toolchains\ti-cgt-arm_18.12.5.LTS when
rem                   it holds Windows binaries, else the CCS installation under X:\ti\ccs*.
rem   SIMPLELINK_SDK  SimpleLink CC13x0 SDK 4.20.02.07.  Default: X:\ti\simplelink_cc13x0_sdk_4_20_02_07
rem   XDCTOOLS        XDCtools 3.51.03.28 core.  Default: X:\ti\xdctools_3_51_03_28_core
rem   PYTHON          Python 3 command.  Default: "py -3" when the Python launcher exists, else "python".
rem   TI_ARMCL        Full path of armcl.exe (derived from TI_ARM_CGT).
rem   TI_ARMHEX       Full path of armhex.exe (derived from TI_ARM_CGT).
rem
rem Pre-set TI_ARM_CGT, SIMPLELINK_SDK or XDCTOOLS to override the defaults; surrounding quotes and
rem trailing backslashes are removed, an empty value counts as unset, and & ^ ( ) and spaces are
rem fine (a value containing % or ! is not supported).  PYTHON is used verbatim: keep the quotes
rem around a path with spaces, e.g. set PYTHON="C:\Program Files\Python312\python.exe", and do not
rem quote "py -3".  Drives C:, D: and E: are searched.  The compiler and the SDK are always
rem required; pass --xdctools and/or --python to require those too (the firmware build needs
rem both, the bootloader build neither).
rem
rem Usage: call "%~dp0..\tools\ti_env.bat" [--xdctools] [--python]

for %%I in ("%~dp0..") do set "_ti_env_root=%%~fI"
set "_ti_env_need_xdc="
set "_ti_env_need_python="
:ti_env_options
if "%~1"=="" goto :ti_env_options_done
if /i "%~1"=="--xdctools" set "_ti_env_need_xdc=1"
if /i "%~1"=="--python" set "_ti_env_need_python=1"
shift
goto :ti_env_options
:ti_env_options_done

rem --- Overrides: strip quotes and trailing backslashes; an empty value means "not set" --------
call :ti_env_normalize TI_ARM_CGT
call :ti_env_normalize SIMPLELINK_SDK
call :ti_env_normalize XDCTOOLS

rem --- TI ARM compiler ------------------------------------------------------------------------
if defined TI_ARM_CGT goto :ti_env_cgt_done
if exist "%_ti_env_root%\toolchains\ti-cgt-arm_18.12.5.LTS\bin\armcl.exe" set "TI_ARM_CGT=%_ti_env_root%\toolchains\ti-cgt-arm_18.12.5.LTS"
if not defined TI_ARM_CGT call :ti_env_probe_cgt 2>nul
if not defined TI_ARM_CGT set "TI_ARM_CGT=%_ti_env_root%\toolchains\ti-cgt-arm_18.12.5.LTS"
:ti_env_cgt_done
set "TI_ARMCL=%TI_ARM_CGT%\bin\armcl.exe"
set "TI_ARMHEX=%TI_ARM_CGT%\bin\armhex.exe"

rem --- SimpleLink SDK and XDCtools ------------------------------------------------------------
if not defined SIMPLELINK_SDK call :ti_env_probe_dir SIMPLELINK_SDK "ti\simplelink_cc13x0_sdk_4_20_02_07" source 2>nul
if not defined SIMPLELINK_SDK set "SIMPLELINK_SDK=C:\ti\simplelink_cc13x0_sdk_4_20_02_07"
if not defined XDCTOOLS call :ti_env_probe_dir XDCTOOLS "ti\xdctools_3_51_03_28_core" packages 2>nul
if not defined XDCTOOLS set "XDCTOOLS=C:\ti\xdctools_3_51_03_28_core"

rem --- Python 3 -------------------------------------------------------------------------------
rem "python3" is deliberately not tried: on Windows it is normally the Microsoft Store stub.
if defined PYTHON goto :ti_env_python_done
where /q py.exe
if not errorlevel 1 (set "PYTHON=py -3") else (set "PYTHON=python")
:ti_env_python_done

rem --- Validation -----------------------------------------------------------------------------
set "_ti_env_missing="
if not exist "%TI_ARMCL%" set "_ti_env_missing=%_ti_env_missing% armcl.exe"
if not exist "%TI_ARMHEX%" set "_ti_env_missing=%_ti_env_missing% armhex.exe"
if not exist "%SIMPLELINK_SDK%\source\" set "_ti_env_missing=%_ti_env_missing% SDK"
if defined _ti_env_need_xdc if not exist "%XDCTOOLS%\packages\" set "_ti_env_missing=%_ti_env_missing% XDCtools"
if defined _ti_env_need_python call :ti_env_check_python
if not defined _ti_env_missing goto :ti_env_ok
setlocal EnableDelayedExpansion
>&2 echo Missing!_ti_env_missing!. Checked TI_ARM_CGT=!TI_ARM_CGT!, SIMPLELINK_SDK=!SIMPLELINK_SDK!, XDCTOOLS=!XDCTOOLS!, PYTHON=!PYTHON!
endlocal
>&2 echo Set the corresponding environment variable to the correct location and retry.
call :ti_env_cleanup
exit /b 1

:ti_env_ok
call :ti_env_cleanup
exit /b 0

rem --- Subroutines ----------------------------------------------------------------------------
:ti_env_check_python
rem CALL is needed in case PYTHON is a batch shim (pyenv-win installs python.bat).
call %PYTHON% -c "import sys; raise SystemExit(0 if sys.version_info >= (3, 6) else 1)" >nul 2>nul
if not "%ERRORLEVEL%"=="0" set "_ti_env_missing=%_ti_env_missing% Python3"
exit /b 0

:ti_env_probe_cgt
rem Look for the compiler inside a CCS installation (X:\ti\ccs*) or beside it (X:\ti).
for %%D in (C D E) do (
    for /d %%C in (%%D:\ti\ccs*) do (
        if not defined TI_ARM_CGT if exist "%%~fC\ccs\tools\compiler\ti-cgt-arm_18.12.5.LTS\bin\armcl.exe" set "TI_ARM_CGT=%%~fC\ccs\tools\compiler\ti-cgt-arm_18.12.5.LTS"
        if not defined TI_ARM_CGT if exist "%%~fC\tools\compiler\ti-cgt-arm_18.12.5.LTS\bin\armcl.exe" set "TI_ARM_CGT=%%~fC\tools\compiler\ti-cgt-arm_18.12.5.LTS"
        if not defined TI_ARM_CGT if exist "%%~fC\ti-cgt-arm_18.12.5.LTS\bin\armcl.exe" set "TI_ARM_CGT=%%~fC\ti-cgt-arm_18.12.5.LTS"
    )
    if not defined TI_ARM_CGT if exist "%%D:\ti\ti-cgt-arm_18.12.5.LTS\bin\armcl.exe" set "TI_ARM_CGT=%%D:\ti\ti-cgt-arm_18.12.5.LTS"
)
exit /b 0

:ti_env_probe_dir
rem %1 variable to set, %2 path below the drive root, %3 subdirectory that must exist inside it.
for %%D in (C D E) do if not defined %~1 if exist "%%D:\%~2\%~3\" set "%~1=%%D:\%~2"
exit /b 0

:ti_env_normalize
rem Strip quotes and every trailing backslash from the named override; an empty result unsets it.
rem Delayed expansion keeps & and ^ in the value intact (a value containing ! is not supported).
if not defined %~1 exit /b 0
setlocal EnableDelayedExpansion
set "_ti_env_value=!%~1!"
set "_ti_env_value=!_ti_env_value:"=!"
:ti_env_normalize_strip
if not defined _ti_env_value goto :ti_env_normalize_unset
if not "!_ti_env_value:~-1!"=="\" goto :ti_env_normalize_keep
set "_ti_env_value=!_ti_env_value:~0,-1!"
goto :ti_env_normalize_strip
:ti_env_normalize_keep
endlocal & set "%~1=%_ti_env_value%"
exit /b 0
:ti_env_normalize_unset
endlocal & set "%~1="
exit /b 0

:ti_env_cleanup
set "_ti_env_need_xdc="
set "_ti_env_need_python="
set "_ti_env_root="
set "_ti_env_missing="
set "_ti_env_value="
exit /b 0
