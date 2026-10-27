@echo off
rem Toneical - guitar rig
rem SPDX-License-Identifier: AGPL-3.0-or-later
rem
rem Double-click to build the Windows installer:  dist\Toneical-<version>-Windows-x64-Setup.exe
rem
rem No Visual Studio needed. Whatever is missing is installed with winget (it asks first):
rem   - Build Tools for Visual Studio 2022: Microsoft's command-line C++ compiler, no IDE
rem   - CMake, Git (JUCE is downloaded during the first build), Inno Setup (makes the setup .exe)
rem
rem Options are passed on to packaging\windows\build.ps1, e.g.:
rem   build-windows.bat -Test                  also run the offline render test
rem   build-windows.bat -AsioSdk C:\asiosdk    with ASIO drivers (Steinberg ASIO SDK)

setlocal EnableExtensions
title Toneical - build the Windows installer
cd /d "%~dp0"

echo.
echo  ===== Toneical: Windows installer build =====
echo.

where winget >nul 2>nul
if errorlevel 1 (
    echo  winget was not found. It comes with Windows 10/11 as "App Installer":
    echo  install or update "App Installer" from the Microsoft Store, then run this again.
    goto :fail
)

rem tools installed during this run aren't on this window's PATH yet - add their usual folders
set "PATH=%ProgramFiles%\CMake\bin;%ProgramFiles%\Git\cmd;%PATH%"

rem ---- what's missing? --------------------------------------------------------------------
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "MISSING="
call :hasCompiler || set "MISSING=%MISSING% [C++ Build Tools, ~3 GB]"
where cmake >nul 2>nul || set "MISSING=%MISSING% [CMake]"
where git >nul 2>nul || set "MISSING=%MISSING% [Git]"
call :hasInno || set "MISSING=%MISSING% [Inno Setup]"

if defined MISSING (
    echo  Missing:%MISSING%
    echo.
    choice /C YN /M "  Install them now with winget"
    if errorlevel 2 goto :fail
    echo.
)

call :hasCompiler
if errorlevel 1 (
    echo  Installing Build Tools for Visual Studio 2022 with the C++ compiler.
    echo  This downloads about 3 GB and takes a while; Windows asks for permission once.
    winget install --id Microsoft.VisualStudio.2022.BuildTools -e --source winget ^
        --accept-package-agreements --accept-source-agreements ^
        --override "--passive --wait --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
    call :hasCompiler
    if errorlevel 1 (
        echo  The C++ Build Tools are still not found. If the installer asked for a restart,
        echo  restart Windows and run this again.
        goto :fail
    )
)
call :ensureTool cmake Kitware.CMake || goto :fail
call :ensureTool git Git.Git || goto :fail
call :hasInno
if errorlevel 1 (
    winget install --id JRSoftware.InnoSetup -e --source winget --silent --accept-package-agreements --accept-source-agreements
    call :hasInno
    if errorlevel 1 ( echo  Inno Setup could not be installed. & goto :fail )
)

rem ---- build ---------------------------------------------------------------------------------
echo.
echo  All tools found - building. The first build downloads JUCE and takes several minutes.
echo.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0packaging\windows\build.ps1" %*
if errorlevel 1 goto :fail

echo.
echo  ===== Done: the installer is in the "dist" folder =====
start "" "%~dp0dist"
pause
exit /b 0

:fail
echo.
echo  ===== The build did not finish (see the messages above) =====
pause
exit /b 1

rem ---- helpers -----------------------------------------------------------------------------
:hasCompiler
if not exist "%VSWHERE%" exit /b 1
set "VSPATH="
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
if defined VSPATH exit /b 0
exit /b 1

:hasInno
if exist "%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe" exit /b 0
if exist "%ProgramFiles%\Inno Setup 6\ISCC.exe" exit /b 0
if exist "%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe" exit /b 0
where iscc >nul 2>nul && exit /b 0
exit /b 1

:ensureTool
where %1 >nul 2>nul && exit /b 0
echo  Installing %2 ...
winget install --id %2 -e --source winget --silent --accept-package-agreements --accept-source-agreements
where %1 >nul 2>nul && exit /b 0
echo  %1 was installed but this window can't see it yet: close it and double-click build-windows.bat again.
exit /b 1
