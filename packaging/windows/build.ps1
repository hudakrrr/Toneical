# Toneical - guitar rig
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Builds Toneical on Windows and packs it into one installer:
#   dist\Toneical-<version>-Windows-x64-Setup.exe
#
# Needs (once):  winget install Kitware.CMake Git.Git JRSoftware.InnoSetup
#                a C++ compiler: the free "Build Tools for Visual Studio" (no IDE needed) or
#                Visual Studio 2022/2026 with "Desktop development with C++"
# Easiest: double-click build-windows.bat in the top folder - it installs all of that for you.
#
# Usage (PowerShell, from anywhere):
#   packaging\windows\build.ps1                         # WASAPI / DirectSound
#   packaging\windows\build.ps1 -AsioSdk C:\asiosdk     # + ASIO drivers (Steinberg ASIO SDK)
#   packaging\windows\build.ps1 -Native                 # optimise for this PC's CPU only (AVX2)
#   packaging\windows\build.ps1 -Test                   # also build + run the offline render test

param(
    [string]$AsioSdk = "",
    [switch]$Native,
    [switch]$Test,
    [string]$Version = ""
)
$ErrorActionPreference = "Stop"

$root  = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$build = Join-Path $root "build-windows"
if (-not $Version) { $Version = (Get-Content (Join-Path $root "VERSION")).Trim() }

function Need($cmd, $hint) {
    if (-not (Get-Command $cmd -ErrorAction SilentlyContinue)) { throw "$cmd not found - $hint" }
}
Need cmake "winget install Kitware.CMake"
Need git   "winget install Git.Git"

$iscc = (Get-Command iscc.exe -ErrorAction SilentlyContinue).Source
if (-not $iscc) {
    foreach ($p in @("${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe", "$env:ProgramFiles\Inno Setup 6\ISCC.exe",
                     "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe")) {
        if (Test-Path $p) { $iscc = $p; break }
    }
}
if (-not $iscc) { throw "Inno Setup 6 not found - winget install JRSoftware.InnoSetup" }

Write-Host "== Toneical ${Version}: configuring" -ForegroundColor Cyan
# no -G: CMake takes the newest Visual Studio / Build Tools it finds
$cfg = @("-S", $root, "-B", $build, "-A", "x64",
         "-DTONEICAL_BUILD_TESTS=$(if ($Test) { 'ON' } else { 'OFF' })",
         "-DTONEICAL_NATIVE=$(if ($Native) { 'ON' } else { 'OFF' })")
if ($AsioSdk) { $cfg += "-DTONEICAL_ASIO_SDK_DIR=$((Resolve-Path $AsioSdk).Path -replace '\\','/')" }
cmake @cfg
if ($LASTEXITCODE) { throw "configure failed" }

Write-Host "== building (the first build downloads JUCE and takes a while)" -ForegroundColor Cyan
$targets = @("Toneical_Standalone", "Toneical_VST3")
if ($Test) { $targets += "ToneicalRenderTest" }
cmake --build $build --config Release --parallel --target @targets
if ($LASTEXITCODE) { throw "build failed" }

if ($Test) {
    Write-Host "== offline render test" -ForegroundColor Cyan
    & (Join-Path $build "ToneicalRenderTest_artefacts\Release\ToneicalRenderTest.exe") (Join-Path $build "render-test")
    if ($LASTEXITCODE) { throw "render test failed" }
}

Write-Host "== packing the installer" -ForegroundColor Cyan
& $iscc "/DAppVersion=$Version" "/DBuildDir=$build\Toneical_artefacts\Release" (Join-Path $PSScriptRoot "toneical.iss")
if ($LASTEXITCODE) { throw "Inno Setup failed" }

$setup = Join-Path $root "dist\Toneical-$Version-Windows-x64-Setup.exe"
Write-Host "`nDone: $setup" -ForegroundColor Green
