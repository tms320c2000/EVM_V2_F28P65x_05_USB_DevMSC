@echo off
chcp 65001 > nul
title F28P659 EVM V2 Live Telemetry Monitor
color 0A

echo ========================================================
echo  SyncWorks TMS320F28P659 EVM V2 Live Telemetry Monitor
echo  Press Ctrl+C to exit.
echo ========================================================
echo.

:find_drive
set DRIVE=
for %%d in (D E F G H I) do (
    if exist %%d:\STATUS.TXT (
        set DRIVE=%%d:
        goto found
    )
)
echo [!] Waiting for F28P659 USB Drive to be mounted...
timeout /t 2 > nul
goto find_drive

:found
:loop
cls
if not exist %DRIVE%\STATUS.TXT (
    echo [!] Drive disconnected! Reconnecting...
    goto find_drive
)
type %DRIVE%\STATUS.TXT
echo.
echo [Updated live from DSP RAM - Press Ctrl+C to stop]
timeout /t 1 > nul
goto loop
