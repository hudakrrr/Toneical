# Toneical - guitar rig
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# Builds Toneical for Windows. Started by build-windows.bat (double-click it).
#
# Nothing is installed on the system and no admin rights are needed. Everything the build
# needs is downloaded once, as portable copies, into C:\ToneicalBuild\tools:
#   - Microsoft's C++ compiler + Windows SDK (via PortableBuildTools; you accept Microsoft's licence)
#   - CMake, Ninja, Inno Setup
# Results go to the project's dist\ folder:
#   Toneical-<version>-Windows-x64-Setup.exe     installer (app, VST3 plugin, presets)
#   Toneical-<version>-Windows-x64-portable.zip  the same files, no installation needed
#
# Options:  build-windows.bat -Clean                 rebuild from scratch (keeps the downloaded tools)
#           build-windows.bat -AsioSdk C:\asiosdk    with ASIO drivers (Steinberg ASIO SDK)

param(
    [string]$AsioSdk = "",
    [switch]$Clean
)

$ErrorActionPreference = "Continue"     # native tools report through exit codes, checked below
$ProgressPreference = "SilentlyContinue"
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$CMakeVersion = "3.31.6"
$CMakeUrl     = "https://github.com/Kitware/CMake/releases/download/v$CMakeVersion/cmake-$CMakeVersion-windows-x86_64.zip"
$NinjaUrl     = "https://github.com/ninja-build/ninja/releases/download/v1.12.1/ninja-win.zip"
$PbtUrl       = "https://github.com/Data-Oriented-House/PortableBuildTools/releases/download/v2.10.2/PortableBuildTools.exe"
$InnoUrl      = "https://github.com/jrsoftware/issrc/releases/download/is-6_7_3/innosetup-6.7.3.exe"

function Step ($msg) { Write-Host ""; Write-Host "==> $msg" -ForegroundColor Cyan }
function Note ($msg) { Write-Host "    $msg" }
function Fail ($msg)
{
    Write-Host ""
    Write-Host "BUILD FAILED: $msg" -ForegroundColor Red
    exit 1
}

function Download ($url, $file)
{
    Note "downloading $([IO.Path]::GetFileName($file)) ..."
    for ($try = 1; $try -le 3; $try++)
    {
        Remove-Item $file -Force -ErrorAction SilentlyContinue
        if (Get-Command curl.exe -ErrorAction SilentlyContinue)
        {
            & curl.exe -L --fail --silent --show-error --retry 3 -o $file $url
        }
        else
        {
            try { Invoke-WebRequest -UseBasicParsing -Uri $url -OutFile $file } catch { Note $_.Exception.Message }
        }
        if ((Test-Path $file) -and (Get-Item $file).Length -gt 0) { return }
        Note "download failed, retrying ($try/3)..."
        Start-Sleep -Seconds 3
    }
    Fail "could not download $url`n    Check the internet connection (or a firewall / proxy) and run build-windows.bat again."
}

function Unzip ($zip, $dest)
{
    Remove-Item $dest -Recurse -Force -ErrorAction SilentlyContinue
    try { Expand-Archive -Path $zip -DestinationPath $dest -Force -ErrorAction Stop }
    catch { Fail "could not unpack $zip ($($_.Exception.Message))" }
    Remove-Item $zip -Force -ErrorAction SilentlyContinue
}

# ---------------------------------------------------------------------------------------------
Write-Host ""
Write-Host "  Toneical - Windows build" -ForegroundColor Green
Write-Host "  ------------------------"

$project = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
if (-not (Test-Path (Join-Path $project "CMakeLists.txt")) -or -not (Test-Path (Join-Path $project "VERSION")))
{
    Fail "the project files aren't next to build-windows.bat. If you downloaded a zip, extract the WHOLE folder first."
}
$version = (Get-Content (Join-Path $project "VERSION") -Raw).Trim()

if ($env:PROCESSOR_ARCHITECTURE -ne "AMD64" -and $env:PROCESSOR_ARCHITEW6432 -ne "AMD64" -and $env:PROCESSOR_ARCHITECTURE -ne "ARM64")
{
    Fail "this needs 64-bit Windows 10 or 11."
}

# A short folder without spaces: avoids Windows' 260-character path limit, special characters in
# user names, and OneDrive syncing the build files.
$work = $null
foreach ($candidate in @("C:\ToneicalBuild", (Join-Path $env:LOCALAPPDATA "ToneicalBuild")))
{
    try
    {
        New-Item -ItemType Directory -Force -Path $candidate -ErrorAction Stop | Out-Null
        $probe = Join-Path $candidate "write-test.tmp"
        Set-Content -Path $probe -Value "ok" -ErrorAction Stop
        Remove-Item $probe -Force
        $work = $candidate
        break
    }
    catch { }
}
if (-not $work) { Fail "can't create a work folder (tried C:\ToneicalBuild and $env:LOCALAPPDATA\ToneicalBuild)." }

$tools = Join-Path $work "tools"
$src   = Join-Path $work "src"
$build = Join-Path $work "build"
$dist  = Join-Path $project "dist"
New-Item -ItemType Directory -Force -Path $tools, $dist | Out-Null
Note "Toneical $version"
Note "work folder: $work"

$drive = Get-PSDrive -Name ($work.Substring(0, 1)) -ErrorAction SilentlyContinue
if ($drive -and $drive.Free -lt 8GB)
{
    Fail "only $([math]::Round($drive.Free / 1GB, 1)) GB free on drive $($drive.Name):. The first build needs about 8 GB."
}

# ---- tools ------------------------------------------------------------------------------------
Step "Build tools"

$cmake = Join-Path $tools "cmake\bin\cmake.exe"
if (-not (Test-Path $cmake))
{
    $zip = Join-Path $tools "cmake.zip"
    Download $CMakeUrl $zip
    Unzip $zip (Join-Path $tools "cmake-unpack")
    Remove-Item (Join-Path $tools "cmake") -Recurse -Force -ErrorAction SilentlyContinue
    Move-Item (Join-Path $tools "cmake-unpack\cmake-$CMakeVersion-windows-x86_64") (Join-Path $tools "cmake")
    Remove-Item (Join-Path $tools "cmake-unpack") -Recurse -Force -ErrorAction SilentlyContinue
    if (-not (Test-Path $cmake)) { Fail "CMake didn't unpack correctly. Delete $tools\cmake and try again." }
}
Note "CMake: ok"

$ninja = Join-Path $tools "ninja\ninja.exe"
if (-not (Test-Path $ninja))
{
    $zip = Join-Path $tools "ninja.zip"
    Download $NinjaUrl $zip
    Unzip $zip (Join-Path $tools "ninja")
    if (-not (Test-Path $ninja)) { Fail "Ninja didn't unpack correctly. Delete $tools\ninja and try again." }
}
Note "Ninja: ok"

$msvc = Join-Path $tools "msvc"
$devcmd = Join-Path $msvc "devcmd.bat"
if (-not (Test-Path $devcmd))
{
    Remove-Item $msvc -Recurse -Force -ErrorAction SilentlyContinue   # a half-finished earlier attempt
    $pbt = Join-Path $tools "PortableBuildTools.exe"
    if (-not (Test-Path $pbt)) { Download $PbtUrl $pbt }
    Write-Host ""
    Write-Host "    Next: Microsoft's C++ compiler and Windows SDK (about 1 GB download, 3 GB on disk)." -ForegroundColor Yellow
    Write-Host "    They come under Microsoft's licence, which it asks you to accept: press Y, then Enter." -ForegroundColor Yellow
    Write-Host ""
    & $pbt "target=x64" "host=x64" "env=none" "path=$msvc"
    if (-not (Test-Path $devcmd))
    {
        Remove-Item $msvc -Recurse -Force -ErrorAction SilentlyContinue
        Fail "the C++ compiler wasn't installed (licence not accepted, or the download was interrupted). Run build-windows.bat again."
    }
}
Note "C++ compiler: ok"

$iscc = Join-Path $tools "innosetup\ISCC.exe"
if (-not (Test-Path $iscc))
{
    $setup = Join-Path $tools "innosetup-setup.exe"
    Download $InnoUrl $setup
    # portable, current-user install into the tools folder: no admin, no Start menu entries
    Start-Process -FilePath $setup -Wait -ArgumentList @(
        "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART", "/CURRENTUSER", "/PORTABLE=1",
        "/DIR=`"$(Join-Path $tools 'innosetup')`"")
    Remove-Item $setup -Force -ErrorAction SilentlyContinue
}
if (Test-Path $iscc) { Note "Inno Setup: ok" } else { Note "Inno Setup: not available - you'll get the portable zip, but no installer" }

# the compiler's environment (what Visual Studio's "developer prompt" would set)
foreach ($line in (& cmd.exe /c "call `"$devcmd`" >nul 2>&1 && set"))
{
    $eq = $line.IndexOf("=")
    if ($eq -gt 0) { Set-Item -Path ("env:" + $line.Substring(0, $eq)) -Value $line.Substring($eq + 1) }
}
foreach ($tool in @("cl.exe", "link.exe", "rc.exe", "mt.exe"))
{
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue))
    {
        Fail "$tool is missing from the compiler folder. Delete $msvc and run build-windows.bat again."
    }
}

# ---- sources ----------------------------------------------------------------------------------
Step "Copying the sources to $src"
& robocopy.exe $project $src /MIR /XD .git build build-windows dist /NFL /NDL /NJH /NJS /NP | Out-Null
if ($LASTEXITCODE -ge 8) { Fail "could not copy the project to $src (robocopy error $LASTEXITCODE)." }

# ---- build ------------------------------------------------------------------------------------
if ($Clean) { Remove-Item $build -Recurse -Force -ErrorAction SilentlyContinue }

$configure = @("-S", $src, "-B", $build, "-G", "Ninja",
               "-DCMAKE_MAKE_PROGRAM=$ninja",
               "-DCMAKE_BUILD_TYPE=Release",
               "-DCMAKE_C_COMPILER=cl", "-DCMAKE_CXX_COMPILER=cl",
               "-DTONEICAL_BUILD_TESTS=OFF", "-DTONEICAL_NATIVE=OFF")
if ($AsioSdk)
{
    if (-not (Test-Path (Join-Path $AsioSdk "common\iasiodrv.h"))) { Fail "-AsioSdk: no common\iasiodrv.h in $AsioSdk" }
    $configure += "-DTONEICAL_ASIO_SDK_DIR=$((Resolve-Path $AsioSdk).Path -replace '\\', '/')"
}

Step "Configuring (the first time this downloads JUCE and the amp-modelling library)"
& $cmake @configure
if ($LASTEXITCODE -ne 0)
{
    Note "configuring failed - retrying once from a clean build folder..."
    Remove-Item $build -Recurse -Force -ErrorAction SilentlyContinue
    & $cmake @configure
    if ($LASTEXITCODE -ne 0) { Fail "CMake could not configure the project (see the messages above)." }
}

Step "Compiling (the first build takes 5-20 minutes, later ones are much faster)"
& $cmake --build $build --target Toneical_Standalone Toneical_VST3
if ($LASTEXITCODE -ne 0) { Fail "compiling failed (see the first 'error' above)." }

$art = Join-Path $build "Toneical_artefacts\Release"
$exe = Join-Path $art "Standalone\Toneical.exe"
$vst = Join-Path $art "VST3\Toneical.vst3"
if (-not (Test-Path $exe)) { Fail "the build finished but $exe is missing." }

# ---- package ----------------------------------------------------------------------------------
Step "Packaging"
$name = "Toneical-$version-Windows-x64"
$portable = Join-Path $work $name
Remove-Item $portable -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $portable | Out-Null
Copy-Item $exe $portable
if (Test-Path $vst) { Copy-Item $vst (Join-Path $portable "Toneical.vst3") -Recurse }
foreach ($doc in @("README.md", "LICENSE", "THIRD_PARTY.md")) { Copy-Item (Join-Path $project $doc) $portable }
$zipOut = Join-Path $dist "$name-portable.zip"
Remove-Item $zipOut -Force -ErrorAction SilentlyContinue
Compress-Archive -Path (Join-Path $portable "*") -DestinationPath $zipOut
Note "portable: $zipOut"

$setupOut = Join-Path $dist "$name-Setup.exe"
if (Test-Path $iscc)
{
    & $iscc "/Q" "/O$dist" "/DAppVersion=$version" "/DBuildDir=$art" (Join-Path $src "packaging\windows\toneical.iss")
    if ($LASTEXITCODE -eq 0 -and (Test-Path $setupOut)) { Note "installer: $setupOut" }
    else { Note "the installer step failed - the portable zip above works without installation" }
}

Write-Host ""
Write-Host "  Done!  Your files are in: $dist" -ForegroundColor Green
Start-Process explorer.exe $dist
exit 0
