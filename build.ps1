# Build Pinol from source and open it, on Windows 10/11 (x64). Run from build.cmd (double-click), build.sh (Git Bash)
# or PowerShell: powershell -ExecutionPolicy Bypass -File build.ps1 [options]
#
#   --no-install            do not install anything (the requirements are there)
#   --no-run                build only
#   --accept-ndi-license    include NDI: you accept the NDI SDK License Agreement (https://ndi.video/sdk/)
#   --no-ndi                without NDI, without asking
#   --accept-touchengine-license  include TouchEngine: you have accepted its SDK and TouchDesigner terms
#   --no-touchengine        without TouchEngine, without asking
#   --clean                 start from an empty build folder
#   --debug                 a Debug build
#
# It installs with winget what is missing: Git, CMake, Ninja, Python, LLVM (clang, for OMT), the .NET 8 SDK and the
# Visual Studio 2022 Build Tools (C++). Qt (with aqtinstall) and FFmpeg (an LGPL build, checksummed) go to build-deps\,
# at the versions the CI uses (.github/workflows/build.yml).
$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot

$Install = $true; $Run = $true; $Clean = $false; $Ndi = ""
$Tox = ""; $Type = "Release"
foreach ($a in $args) {
    switch ($a) {
        "--no-install" { $Install = $false }
        "--no-run" { $Run = $false }
        "--clean" { $Clean = $true }
        "--debug" { $Type = "Debug" }
        "--accept-ndi-license" { $Ndi = "ON" }
        "--no-ndi" { $Ndi = "OFF" }
        "--accept-touchengine-license" { $Tox = "ON" }
        "--no-touchengine" { $Tox = "OFF" }
        { $_ -in "-h", "--help" } { Get-Content $PSCommandPath -TotalCount 15 | ForEach-Object { $_ -replace '^# ?', '' }; exit 0 }
        default { Write-Host "Unknown option: $a (see build.ps1 --help)" -ForegroundColor Red; exit 2 }
    }
}

function Step($text) { Write-Host "`n==> $text" -ForegroundColor Cyan }
function Fail($text) { Write-Host "`nError: $text" -ForegroundColor Red; exit 1 }
function Have($name) { [bool](Get-Command $name -ErrorAction SilentlyContinue) }
function Native($what) { if ($LASTEXITCODE -ne 0) { Fail "$what failed (exit code $LASTEXITCODE)" } }
# What winget installed is on the PATH of new windows only: read it again
function RefreshPath {
    $env:Path = [Environment]::GetEnvironmentVariable("Path", "Machine") + ";" + [Environment]::GetEnvironmentVariable("Path", "User")
    foreach ($d in "$env:ProgramFiles\LLVM\bin", "$env:ProgramFiles\dotnet", "$env:ProgramFiles\CMake\bin") {
        if ((Test-Path $d) -and ($env:Path -notlike "*$d*")) { $env:Path += ";$d" }
    }
}

# --- Requirements
if ($Install) {
    $packages = @(
        @{ Id = "Git.Git"; Cmd = "git" },
        @{ Id = "Kitware.CMake"; Cmd = "cmake" },
        @{ Id = "Ninja-build.Ninja"; Cmd = "ninja" },
        @{ Id = "Python.Python.3.12"; Cmd = "py" },
        @{ Id = "LLVM.LLVM"; Cmd = "clang++" },
        @{ Id = "Microsoft.DotNet.SDK.8"; Cmd = "dotnet" }
    )
    $missing = @($packages | Where-Object { -not (Have $_.Cmd) -and -not ($_.Cmd -eq "py" -and (Have python)) })
    if ($missing) {
        if (-not (Have winget)) {
            Fail "winget is needed to install $($missing.Id -join ', ') (App Installer, from the Microsoft Store); or install them yourself and use --no-install"
        }
        Step "Installing what is missing with winget"
        foreach ($p in $missing) {
            Write-Host "  $($p.Id)"
            winget install --id $p.Id -e --source winget --accept-package-agreements --accept-source-agreements --silent
            RefreshPath
        }
    }
}
RefreshPath
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
function FindVs {
    if (-not (Test-Path $vswhere)) { return $null }
    & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
}
$vs = FindVs
if (-not $vs) {
    if (-not $Install) { Fail "Visual Studio 2022 (or its Build Tools) with the C++ tools is needed" }
    Step "Installing the Visual Studio 2022 Build Tools (C++), a few GB: this takes a while"
    winget install --id Microsoft.VisualStudio.2022.BuildTools -e --source winget --accept-package-agreements --accept-source-agreements `
        --override "--add Microsoft.VisualStudio.Workload.VCTools --includeRecommended --passive --wait"
    $vs = FindVs
    if (-not $vs) { Fail "the C++ build tools are not found after their installation: run build again in a new window" }
}
foreach ($c in "git", "cmake", "ninja") { if (-not (Have $c)) { Fail "$c is not installed (open a new window if it was just installed)" } }
# Python by its executable's path: PowerShell names are case-insensitive, a function "py" would call itself
$launcher = Get-Command py -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
$pythonExe = Get-Command python -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
if (-not $launcher -and -not $pythonExe) { Fail "Python 3 is not installed" }
function Invoke-Python {
    if ($launcher) { & $launcher.Source -3 @args } else { & $pythonExe.Source @args }
}

# The compiler's environment (cl, link, the Windows SDK), for x64
Step "Visual Studio: $vs"
Import-Module (Join-Path $vs "Common7\Tools\Microsoft.VisualStudio.DevShell.dll")
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments "-arch=x64 -host_arch=x64" | Out-Null
Set-Location $PSScriptRoot

# --- The versions the CI builds with
$workflow = Get-Content .github\workflows\build.yml -Raw
function FromWorkflow($pattern, $what) {
    $m = [regex]::Match($workflow, $pattern)
    if (-not $m.Success) { Fail "$what not found in .github/workflows/build.yml" }
    $m.Groups[1].Value
}
$qtVersion = FromWorkflow "install-qt-action@v\d+\s+with:\s+version:\s*'([^']+)'" "Qt version"
$qtArch = FromWorkflow "arch:\s*'(win64_msvc[^']+)'" "Qt architecture"
$ffZip = FromWorkflow "FFMPEG_ZIP:\s*(\S+)" "FFmpeg archive"
$ffRelease = FromWorkflow "FFMPEG_RELEASE:\s*(\S+)" "FFmpeg release"
$ffSha = FromWorkflow "FFMPEG_SHA256:\s*(\S+)" "FFmpeg checksum"
$deps = Join-Path $PSScriptRoot "build-deps"
New-Item -ItemType Directory -Force $deps | Out-Null

# --- Qt (with Qt Multimedia, and Qt WebEngine for the Web Page generator: it needs Qt WebChannel and Qt Positioning),
# through aqtinstall
$qtDir = Join-Path $deps "Qt\$qtVersion\$($qtArch -replace '^win64_', '')"
if (-not (Test-Path "$qtDir\bin\qmake.exe")) {
    if (-not $Install) { Fail "Qt $qtVersion is not in $qtDir" }
    Step "Installing Qt $qtVersion ($qtArch, with Qt Multimedia and Qt WebEngine) in build-deps\Qt"
    Invoke-Python -m pip install --user --upgrade aqtinstall; Native "pip install aqtinstall"
    Invoke-Python -m aqt install-qt windows desktop $qtVersion $qtArch -m qtmultimedia qtwebengine qtwebchannel qtpositioning -O (Join-Path $deps "Qt"); Native "Qt installation"
}

# --- FFmpeg: the LGPL shared build the CI uses, checksummed
$ffDir = Join-Path $deps $ffZip
if (-not (Test-Path "$ffDir\bin")) {
    if (-not $Install) { Fail "FFmpeg is not in $ffDir" }
    Step "Downloading FFmpeg ($ffZip, LGPL)"
    $zip = Join-Path $deps "ffmpeg.zip"
    try {
        Invoke-WebRequest "https://github.com/BtbN/FFmpeg-Builds/releases/download/$ffRelease/$ffZip.zip" -OutFile $zip -UseBasicParsing
    } catch {
        Fail "FFmpeg download failed ($($_.Exception.Message)). The pinned build may have been removed from BtbN's releases: update FFMPEG_* in .github/workflows/build.yml"
    }
    if ((Get-FileHash $zip -Algorithm SHA256).Hash -ne $ffSha) { Fail "FFmpeg archive checksum mismatch" }
    Expand-Archive $zip -DestinationPath $deps -Force
    Remove-Item $zip
}

# --- Sources: the submodules (third_party\)
if (Test-Path .git) {
    Step "Fetching the submodules"
    git submodule update --init --recursive; Native "git submodule update"
} elseif (-not (Test-Path third_party\miniaudio\miniaudio.h)) {
    Fail "third_party\ is empty: clone the repository with git (git clone --recurse-submodules https://github.com/PixelStereo/Pinol.git)"
}

if (-not $Tox) {
    if (-not [Console]::IsInputRedirected) {
        Write-Host "TouchEngine SDK: licenses/touchengine.txt (https://derivative.ca/UserGuide/TouchEngine)"
        $answer = Read-Host "Have you accepted the TouchDesigner licence and do you accept the SDK terms? [y/N]"
        $Tox = if ($answer -match '^[Yy]') { "ON" } else { "OFF" }
    } else { $Tox = "OFF" }
}

# --- NDI: only with your agreement to the NDI SDK licence
if (-not $Ndi) {
    Write-Host "`nNDI (video over the network) comes from the NDI SDK, under the NDI SDK License Agreement:"
    Write-Host "  https://ndi.video/sdk/"
    $answer = Read-Host "Do you accept it, to include NDI in Pinol? [y/N]"
    $Ndi = if ($answer -match '^[YyOo]') { "ON" } else { "OFF" }
}

# --- Build
if ($Clean -and (Test-Path build)) { Remove-Item -Recurse -Force build }
$cmakeArgs = @("-S", ".", "-B", "build", "-G", "Ninja", "-DCMAKE_BUILD_TYPE=$Type", "-DCMAKE_PREFIX_PATH=$qtDir",
               "-DPINOL_ACCEPT_TOUCHENGINE_LICENSE=$Tox", "-DFFMPEG_ROOT=$ffDir", "-DPINOL_BUNDLE_RUNTIMES=ON", "-DPINOL_ACCEPT_NDI_SDK_LICENSE=$Ndi")
Step "Configuring (cmake $($cmakeArgs -join ' '))"
cmake @cmakeArgs; Native "CMake configure"
Step "Building"
cmake --build build; Native "Build"
# Qt's libraries and plugins next to the program (FFmpeg's are copied by the build)
$deployArgs = @(if ($Type -eq "Debug") { "--debug" } else { "--release" }) + @("--no-compiler-runtime", "--no-ffmpeg", "-multimedia", "build\Pinol.exe")
& "$qtDir\bin\windeployqt.exe" @deployArgs | Out-Null; Native "windeployqt"

# --- Open
$exe = Join-Path $PSScriptRoot "build\Pinol.exe"
if (-not (Test-Path $exe)) { Fail "build\Pinol.exe was not built" }
Write-Host "`nPinol is built: $exe" -ForegroundColor Green
if ($Run) { Start-Process $exe -WorkingDirectory (Join-Path $PSScriptRoot "build") }
