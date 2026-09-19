@echo off
rem Program the CC1310F128 bootloader through a SEGGER J-Link probe.
rem Windows equivalent of flash_jlink.sh; SEGGER J-Link Commander is JLink.exe on Windows.
rem Python is not needed by this script.
rem
rem This intentionally performs a chip erase: after programming the bootloader,
rem install the application again with the UART OTA update tool.
setlocal EnableExtensions DisableDelayedExpansion
set "script_dir=%~dp0"
set "script_dir=%script_dir:~0,-1%"
set "image=%script_dir%\build\bootloader.out"
set "jlink_serial="
set "work_dir="

rem Options take their value as the next argument or attached to the letter (-s123, -f"C:\a b\x.out"),
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
if "%letter%"=="-f" goto :optionImage
goto :unknownOption
:optionSerial
set "jlink_serial=%value%"
goto :nextOption
:optionImage
set "image=%value%"
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

for %%I in ("%image%") do set "image_abs=%%~fI"
rem Like the .sh [[ -f ]] test, a directory does not count as an image.
if exist "%image_abs%\" goto :imageMissing
if exist "%image_abs%" goto :imageOk
:imageMissing
setlocal EnableDelayedExpansion
>&2 echo Bootloader image not found: !image_abs!
endlocal
>&2 echo Build it first with: build.bat
exit /b 1
:imageOk

call :resolveJLink || exit /b 127
if not defined JLINK_SPEED set "JLINK_SPEED=1000"

rem The command file lives in a private work directory (mkdir is atomic, so concurrent runs
rem cannot share names).
call :makeWorkDirectory || goto :fail
set "command_file=%work_dir%\commands.jlink"
> "%command_file%" echo connect
>> "%command_file%" echo erase
>> "%command_file%" echo loadfile "%image_abs%"
>> "%command_file%" echo r
>> "%command_file%" echo g
>> "%command_file%" echo exit

rem The command-file path stays inside its own quotes on this line, so & or ^ in it cannot reach cmd.exe.
set jlink_args=-Device CC1310F128 -If cJTAG -Speed %JLINK_SPEED% -AutoConnect 1 -JTAGConf -1,-1 -NoGui 1 -CommandFile "%command_file%"
if defined jlink_serial set jlink_args=%jlink_args% -USB %jlink_serial%

setlocal EnableDelayedExpansion
echo Programming !image_abs! via J-Link (CC1310F128, cJTAG, !JLINK_SPEED! kHz) ...
echo Using J-Link Commander: !jlink_exe!
endlocal
call :runJLink
call :removeTemporaries
if not "%status%"=="0" goto :jlinkFailed
echo Bootloader flash completed and target restarted.
exit /b 0

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
echo Usage: %~nx0 [-s JLINK_SERIAL] [-f IMAGE]
echo.
echo Program a CC1310F128 bootloader over cJTAG.
echo.
echo Options:
echo   -s JLINK_SERIAL  Select a specific J-Link probe serial number.
echo   -f IMAGE         ELF/OUT image to program (default: build\bootloader.out).
echo   -h               Show this help.
echo.
echo Environment:
echo   JLINK_BIN        J-Link Commander executable (default: JLink.exe).
echo   JLINK_SPEED      cJTAG clock in kHz (default: 1000).
echo.
echo The script erases the whole device before programming.  Reinstall the
echo application afterward with the bootloader UART update client.
exit /b 0
