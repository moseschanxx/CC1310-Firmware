@echo off
rem Program the complete CC1310F128 image: bootloader plus combined application.
rem Windows equivalent of flash_all_jlink.sh; SEGGER J-Link Commander is JLink.exe on Windows.
setlocal EnableExtensions DisableDelayedExpansion
set "script_dir=%~dp0"
set "script_dir=%script_dir:~0,-1%"
for %%I in ("%script_dir%\..") do set "repo_root=%%~fI"
set "bootloader_image=%repo_root%\bootloader\build\bootloader.out"
set "app_image=%script_dir%\boot_build\nonrom_test\firmware.hex"
set "app_package=%script_dir%\boot_build\nonrom_test\firmware.pkg"
set "jlink_serial="
set "role=rx"
set "work_dir="

rem Options take their value as the next argument or attached to the letter (-rtx, -b"C:\a b\x.out"),
rem like getopts; quotes around a value are removed either way.  Only a missing argument counts as a
rem missing value, so -s "" is accepted.  Unknown options and positional arguments are rejected.
:parseOptions
set option=%1
if not defined option goto :optionsDone
set "option=%option:"=%"
if not defined option goto :unknownOption
if "%option%"=="/?" goto :help
set "letter=%option:~0,2%"
if "%letter%"=="-h" goto :help
set "value=%option:~2%"
set "consumed=1"
if defined value goto :haveValue
set "consumed=2"
set value=%2
if not defined value goto :missingValue
set "value=%value:"=%"
:haveValue
if "%letter%"=="-s" goto :optionSerial
if "%letter%"=="-b" goto :optionBootloader
if "%letter%"=="-a" goto :optionApp
if "%letter%"=="-p" goto :optionPackage
if "%letter%"=="-r" goto :optionRole
goto :unknownOption
:optionSerial
set "jlink_serial=%value%"
goto :nextOption
:optionBootloader
set "bootloader_image=%value%"
goto :nextOption
:optionApp
set "app_image=%value%"
goto :nextOption
:optionPackage
set "app_package=%value%"
goto :nextOption
:optionRole
set "role=%value%"
goto :nextOption
:nextOption
shift
if "%consumed%"=="2" shift
goto :parseOptions
:unknownOption
setlocal EnableDelayedExpansion
>&2 echo Unknown option: !option!
endlocal
call :usage 1>&2
exit /b 2
:missingValue
>&2 echo Missing value for %letter%
call :usage 1>&2
exit /b 2
:optionsDone

if "%role%"=="rx" goto :roleOk
if "%role%"=="tx" goto :roleOk
>&2 echo ROLE must be rx or tx
exit /b 2
:roleOk

for %%I in ("%bootloader_image%") do set "bootloader_abs=%%~fI"
for %%I in ("%app_image%") do set "app_abs=%%~fI"
for %%I in ("%app_package%") do set "package_abs=%%~fI"
call :requireImage bootloader_abs || exit /b 1
call :requireImage app_abs || exit /b 1
call :requireImage package_abs || exit /b 1

rem "python3" is deliberately not tried: on Windows it is normally the Microsoft Store stub.
if defined PYTHON goto :pythonDone
where /q py.exe
if not errorlevel 1 (set "PYTHON=py -3") else (set "PYTHON=python")
:pythonDone

call :resolveJLink || exit /b 127
if not defined JLINK_SPEED set "JLINK_SPEED=1000"

rem All temporaries live in a private work directory (mkdir is atomic, so concurrent runs cannot
rem share names).  J-Link selects its file parser by suffix, so the metadata image ends in .hex.
call :makeWorkDirectory || goto :fail
set "metadata_hex=%work_dir%\metadata.hex"
set "command_file=%work_dir%\commands.jlink"
rem CALL (needed for batch-file Python shims) doubles ^ in already-expanded text, so the path
rem variables are written as %%name%% and expand in CALL's own pass instead.
call %%PYTHON%% "%%repo_root%%\tools\fw_metadata_hex.py" --package "%%package_abs%%" --app-hex "%%app_abs%%" --role %%role%% --output "%%metadata_hex%%" || goto :fail

rem The bootloader OUT loads at flash zero.  The app uses Intel HEX rather than TI COFF OUT so
rem J-Link receives its explicit 0x8000 record addresses.  The verified metadata record is
rem required after a chip erase before the bootloader will start that raw app image.
> "%command_file%" echo connect
>> "%command_file%" echo erase
>> "%command_file%" echo loadfile "%bootloader_abs%"
>> "%command_file%" echo loadfile "%app_abs%"
>> "%command_file%" echo loadfile "%metadata_hex%"
>> "%command_file%" echo r
>> "%command_file%" echo g
>> "%command_file%" echo exit

rem The command-file path stays inside its own quotes on this line, so & or ^ in it cannot reach cmd.exe.
set jlink_args=-Device CC1310F128 -If JTAG -Speed %JLINK_SPEED% -AutoConnect 1 -JTAGConf -1,-1 -ExitOnError 1 -NoGui 1 -CommandFile "%command_file%"
if defined jlink_serial set jlink_args=%jlink_args% -USB %jlink_serial%

setlocal EnableDelayedExpansion
echo Programming bootloader: !bootloader_abs!
echo Programming combined app HEX: !app_abs! (starts at 0x00008000)
echo Programming validated boot metadata: role=!role!, package=!package_abs!
echo Using J-Link Commander: !jlink_exe!
endlocal
call :runJLink
call :removeTemporaries
if not "%status%"=="0" goto :jlinkFailed
echo Bootloader and combined app flashed; target restarted.
exit /b 0

:requireImage
rem %1 names the variable holding the path (passing the path itself through CALL would
rem re-expand any %% it contains).  Like the .sh [[ -f ]] test, a directory does not count.
call set "image_path=%%%~1%%"
if exist "%image_path%\" goto :imageMissing
if exist "%image_path%" exit /b 0
:imageMissing
setlocal EnableDelayedExpansion
>&2 echo Firmware image not found: !image_path!
endlocal
exit /b 1

:resolveJLink
rem JLINK_BIN names J-Link Commander (default JLink.exe).  A bare name is searched on PATH; when
rem JLINK_BIN is not set, the newest SEGGER installation under Program Files is tried as well.
set "jlink_exe="
set "jlink_user="
if defined JLINK_BIN set "jlink_user=1"
if not defined JLINK_BIN set "JLINK_BIN=JLink.exe"
set "JLINK_BIN=%JLINK_BIN:"=%"
if exist "%JLINK_BIN%\" goto :jlinkNotFound
if exist "%JLINK_BIN%" set "jlink_exe=%JLINK_BIN%"
if not defined jlink_exe for %%P in ("%JLINK_BIN%") do set "jlink_exe=%%~$PATH:P"
if not defined jlink_exe for %%P in ("%JLINK_BIN%.exe") do set "jlink_exe=%%~$PATH:P"
if not defined jlink_exe if not defined jlink_user call :probeJLinkIn "%ProgramFiles%"
if not defined jlink_exe if not defined jlink_user call :probeJLinkIn "%ProgramFiles(x86)%"
if defined jlink_exe exit /b 0
:jlinkNotFound
setlocal EnableDelayedExpansion
>&2 echo Cannot find !JLINK_BIN!. Install J-Link or set JLINK_BIN.
endlocal
exit /b 1

:probeJLinkIn
rem Newest SEGGER\JLink* directory (by modification time) that contains JLink.exe.
if "%~1"=="" exit /b 0
if not exist "%~1\SEGGER\" exit /b 0
for /f "usebackq delims=" %%J in (`dir /b /ad /o-d "%~1\SEGGER\JLink*" 2^>nul`) do if not defined jlink_exe if exist "%~1\SEGGER\%%J\JLink.exe" set "jlink_exe=%~1\SEGGER\%%J\JLink.exe"
exit /b 0

:runJLink
rem J-Link Commander is an .exe and is started directly; CALL would re-expand %% in its
rem arguments.  A batch stub (used by the tests) must be CALLed so that control returns.
for %%J in ("%jlink_exe%") do set "jlink_ext=%%~xJ"
if /i "%jlink_ext%"==".bat" goto :runJLinkScript
if /i "%jlink_ext%"==".cmd" goto :runJLinkScript
"%jlink_exe%" %jlink_args%
set "status=%ERRORLEVEL%"
exit /b 0
:runJLinkScript
call "%jlink_exe%" %jlink_args%
set "status=%ERRORLEVEL%"
exit /b 0

:makeWorkDirectory
rem mkdir fails when the name is already taken, which makes the claim atomic.  %RANDOM% is
rem seeded per second, so centiseconds from %TIME% are mixed in; the name stays short because
rem J-Link Commander truncates long paths.  Like mktemp, an unusable TEMP is an error rather
rem than something to create on the fly.
if not defined TEMP goto :workDirectoryFailed
if not exist "%TEMP%\" goto :workDirectoryFailed
set /a attempt=0
:makeWorkDirectoryRetry
set /a attempt+=1
if %attempt% GTR 50 goto :workDirectoryFailed
set "candidate=%TEMP%\cc1310-flash-%RANDOM%%TIME:~-2%%RANDOM%"
mkdir "%candidate%" 2>nul || goto :makeWorkDirectoryRetry
set "work_dir=%candidate%"
exit /b 0
:workDirectoryFailed
setlocal EnableDelayedExpansion
>&2 echo Cannot create a work directory under TEMP=!TEMP!
endlocal
exit /b 1

:removeTemporaries
if defined work_dir rd /s /q "%work_dir%" 2>nul
exit /b 0

:jlinkFailed
>&2 echo J-Link Commander failed with exit code %status%
exit /b %status%

:fail
call :removeTemporaries
>&2 echo Flashing failed.
exit /b 1

:help
call :usage
exit /b 0

:usage
echo Usage: %~nx0 [-s JLINK_SERIAL] [-b BOOTLOADER_OUT] [-a APP_HEX] [-p APP_PACKAGE] [-r rx^|tx]
echo.
echo Erase the CC1310F128 and program both images over JTAG.
echo.
echo Options:
echo   -s JLINK_SERIAL   Select a specific J-Link probe serial number.
echo   -b BOOTLOADER_OUT Bootloader ELF/OUT image.
echo                     Default: bootloader\build\bootloader.out
echo   -a APP_HEX        App Intel HEX image with records beginning at 0x00008000.
echo                     Default: firmware\boot_build\nonrom_test\firmware.hex
echo   -p APP_PACKAGE    Matching CRC32 OTA package used to create valid boot metadata.
echo                     Default: firmware\boot_build\nonrom_test\firmware.pkg
echo   -r ROLE           Startup role recorded in metadata: rx (default) or tx.
echo   -h                Show this help.
echo.
echo Environment:
echo   JLINK_BIN         J-Link Commander executable (default: JLink.exe).
echo   JLINK_SPEED       JTAG clock in kHz (default: 1000).
echo   PYTHON            Python 3 command (default: "py -3", else "python").
echo.
echo The script performs a chip erase.  It verifies that APP_HEX exactly matches
echo APP_PACKAGE, then writes a valid metadata record so the bootloader can start
echo the app.  Do not use this path for field updates; use the UART OTA client.
exit /b 0
