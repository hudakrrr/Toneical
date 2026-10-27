@echo off
rem Toneical - guitar rig
rem SPDX-License-Identifier: AGPL-3.0-or-later
rem
rem Double-click to build Toneical for Windows. Needs nothing installed: no Visual Studio,
rem no Git, no admin rights. The first run downloads portable build tools (about 1.5 GB,
rem Microsoft's compiler asks you to accept its licence once) into C:\ToneicalBuild.
rem
rem Result in the dist folder: Toneical-<version>-Windows-x64-Setup.exe and a portable .zip
rem
rem   build-windows.bat -Clean                 rebuild from scratch
rem   build-windows.bat -AsioSdk C:\asiosdk    with ASIO drivers

setlocal
title Toneical - Windows build
cd /d "%~dp0"

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0packaging\windows\build.ps1" %*
set "RESULT=%ERRORLEVEL%"

echo.
if not "%RESULT%"=="0" echo  Something went wrong - read the red message above. Running build-windows.bat again continues where it stopped.
pause
exit /b %RESULT%
