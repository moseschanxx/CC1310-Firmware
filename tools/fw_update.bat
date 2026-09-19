@echo off
rem Run the CC1310 UART OTA updater (fw_update.py) with the Windows Python launcher.
rem
rem Usage: fw_update.bat --port COM4 flash --package firmware\boot_build\nonrom_test\firmware.pkg --role tx
rem        fw_update.bat --port COM4 set-role --role rx
rem        fw_update.bat --port auto info
rem
rem PYTHON overrides the interpreter (default: "py -3" when the launcher exists, else "python";
rem a path with spaces must keep its quotes).  pyserial is required: py -3 -m pip install pyserial
setlocal EnableExtensions DisableDelayedExpansion
if defined PYTHON goto :pythonDone
where /q py.exe
if not errorlevel 1 (set "PYTHON=py -3") else (set "PYTHON=python")
:pythonDone
call %PYTHON% -c "import serial" >nul 2>nul || goto :noPyserial
call %PYTHON% "%~dp0fw_update.py" %*
exit /b %ERRORLEVEL%

:noPyserial
>&2 echo pyserial is required for the UART updater; install it with: %PYTHON% -m pip install pyserial
exit /b 1
