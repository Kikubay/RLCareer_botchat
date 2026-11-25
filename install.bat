@echo off
rem botchat installer: run it after extracting the zip (or next to botchat.dll).
rem Copies the plugin where it belongs and enables it in BakkesMod, checking each step.
setlocal
title botchat installer
set "SRC=%~dp0"
set "BAKKES=%APPDATA%\bakkesmod\bakkesmod"

echo.
echo  botchat installer
echo  ----------------
echo.

if not exist "%SRC%botchat.dll" (
    echo  [ERROR] botchat.dll is missing next to this file.
    echo          Build it with build.bat or extract the zip first.
    goto :fail
)
if not exist "%BAKKES%\plugins" (
    echo  [ERROR] BakkesMod not found ^(%BAKKES%^).
    echo          Install BakkesMod and start Rocket League once with it, then run this again.
    goto :fail
)
rem The game keeps the plugin open: the copy would fail while it runs.
tasklist /fi "imagename eq RocketLeague.exe" 2>nul | find /i "RocketLeague.exe" >nul && (
    echo  [ERROR] Rocket League is running. Close it and run this again.
    goto :fail
)

copy /y "%SRC%botchat.dll" "%BAKKES%\plugins\" >nul || (
    echo  [ERROR] Could not copy botchat.dll to %BAKKES%\plugins
    goto :fail
)
echo  [OK] botchat.dll copied to %BAKKES%\plugins

set "CFG=%BAKKES%\cfg\plugins.cfg"
if not exist "%BAKKES%\cfg" mkdir "%BAKKES%\cfg"
findstr /i /x /c:"plugin load botchat" "%CFG%" >nul 2>nul && (
    echo  [OK] botchat already enabled in plugins.cfg
) || (
    rem New line first: the file may not end with one.
    (echo.& echo plugin load botchat)>>"%CFG%" || (
        echo  [ERROR] Could not write to %CFG%
        goto :fail
    )
    echo  [OK] botchat enabled in plugins.cfg
)

echo.
echo  Done. Start Rocket League and play a career match with bots.
echo  Open the BakkesMod menu with F2 and select the Botchat settings tab.
echo.
pause
exit /b 0

:fail
echo.
echo  Installation not finished. Check the messages above and run this again.
echo.
pause
exit /b 1
