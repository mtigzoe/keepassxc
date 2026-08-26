[CmdletBinding()]
param(
    # Expected layout (all sibling folders under one parent):
    #   <parent>\keepassxc\   <- KeePassXC source checkout
    #   <parent>\vcpkg\
    #   <parent>\ruby\        <- created automatically if needed
    # Leave these blank to auto-detect based on the script's own
    # location, or override any of them to point elsewhere.
    [string]$Repo = "",
    [string]$VcpkgRoot = "",
    [string]$RubyRoot = "",

    # Left blank to auto-detect vswhere.exe itself (checked on PATH, then
    # the standard Visual Studio Installer locations). Only used when
    # -VsDevShell is not passed directly.
    [string]$VsWhere = "",

    # Left blank to auto-detect via vswhere.exe. Override to force a
    # specific Visual Studio installation's Launch-VsDevShell.ps1.
    [string]$VsDevShell = "",

    # Left blank to auto-detect via the registry (falling back to the
    # standard install locations). Override to force a specific Windows
    # Kits root. Windows 11 SDKs are still installed under "...Kits\10".
    [string]$WindowsSdkRoot = "",

    # Left blank to auto-detect the newest installed SDK version.
    [string]$WindowsSdkVersion = "",

    # Preserve the existing build directory by default. Use -Clean when
    # you specifically want a completely fresh CMake configuration.
    [switch]$Clean
)

$ErrorActionPreference = "Stop"

# ============================================================
# KeePassXC Windows Debug Build
# Visual Studio + Ninja + vcpkg + Qt + Asciidoctor
# ============================================================

# ============================================================
# Resolve the script's own directory
# ============================================================
# $PSScriptRoot is not reliably populated inside a param() block's
# default value expressions (this depends on how the script was
# launched, e.g. 'powershell -File' vs '.\script.ps1'). Resolve it
# here instead, with fallbacks, after the param block has run.

$ScriptDir =
    if ($PSScriptRoot) { $PSScriptRoot }
    elseif ($PSCommandPath) { Split-Path -Parent $PSCommandPath }
    elseif ($MyInvocation.MyCommand.Path) { Split-Path -Parent $MyInvocation.MyCommand.Path }
    else { (Get-Location).Path }

$ParentDir = Split-Path -Parent $ScriptDir

if (-not $Repo)      { $Repo = Join-Path $ParentDir "keepassxc" }
if (-not $VcpkgRoot) { $VcpkgRoot = Join-Path $ParentDir "vcpkg" }
if (-not $RubyRoot)  { $RubyRoot = Join-Path $ParentDir "ruby" }

# ============================================================
# Validate paths
# ============================================================

if (-not (Test-Path $Repo -PathType Container)) {
    throw "KeePassXC repository was not found: $Repo"
}

if (-not (Test-Path $VcpkgRoot -PathType Container)) {
    throw "vcpkg directory was not found: $VcpkgRoot"
}

# ============================================================
# Locate Visual Studio Developer Shell
# ============================================================

if (-not $VsDevShell) {
    if ($VsWhere) {
        if (-not (Test-Path $VsWhere)) {
            throw "vswhere.exe was not found at the path passed to -VsWhere: $VsWhere"
        }
    } else {
        # vswhere.exe ships with the Visual Studio Installer. Check PATH
        # first (covers e.g. winget/chocolatey installs that register it
        # there), then fall back to the standard installer locations.
        # Program Files (x86) is standard on x64 Windows; ARM64 Windows
        # installs the VS Installer under plain Program Files instead.
        $VsWhereOnPath = Get-Command "vswhere.exe" -ErrorAction SilentlyContinue

        if ($VsWhereOnPath) {
            $VsWhere = $VsWhereOnPath.Source
        } else {
            $VsWhereCandidates = @(
                "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe",
                "${env:ProgramFiles}\Microsoft Visual Studio\Installer\vswhere.exe"
            )

            $VsWhere = $VsWhereCandidates |
                Where-Object { $_ -and (Test-Path $_) } |
                Select-Object -First 1
        }

        if (-not $VsWhere) {
            throw "vswhere.exe could not be found on PATH or in the standard Visual Studio Installer locations.`nPass -VsWhere explicitly if Visual Studio is installed in a non-standard way, or pass -VsDevShell to bypass detection entirely."
        }
    }

    Write-Host ""
    Write-Host "Using vswhere.exe:"
    Write-Host $VsWhere

    # Restrict to actual Visual Studio SKUs (not "*") and require the
    # native C++ toolset component. This avoids other VS-Installer-
    # registered products that share the installer but aren't full VS
    # and don't ship Launch-VsDevShell.ps1 -- e.g. SQL Server Management
    # Studio is built on the VS shell and otherwise gets matched too.
    $VsInstallPath = & $VsWhere -latest -prerelease `
        -products Microsoft.VisualStudio.Product.Community `
                  Microsoft.VisualStudio.Product.Professional `
                  Microsoft.VisualStudio.Product.Enterprise `
                  Microsoft.VisualStudio.Product.BuildTools `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
        -property installationPath

    if (-not $VsInstallPath) {
        throw "vswhere.exe could not find a Visual Studio installation with the C++ (VC.Tools.x86.x64) component. Install the 'Desktop development with C++' workload, or pass -VsDevShell explicitly."
    }

    $VsDevShell = Join-Path $VsInstallPath "Common7\Tools\Launch-VsDevShell.ps1"
}

# ============================================================
# Load Visual Studio environment
# ============================================================

Write-Host ""
Write-Host "============================================================"
Write-Host "Loading Visual Studio x64 environment..."
Write-Host "============================================================"

if (-not (Test-Path $VsDevShell)) {
    throw "Visual Studio Developer PowerShell script was not found: $VsDevShell"
}

Write-Host ""
Write-Host "Using:"
Write-Host $VsDevShell

& $VsDevShell -Arch amd64

if ($LASTEXITCODE -ne 0) {
    throw "Visual Studio Developer environment failed to load."
}

# Visual Studio can modify VCPKG_ROOT. Set it again after loading VS.
$env:VCPKG_ROOT = $VcpkgRoot

# ============================================================
# Check build tools
# ============================================================

Write-Host ""
Write-Host "Checking build tools..."

Write-Host ""
Write-Host "cl:"
where.exe cl

Write-Host ""
Write-Host "ninja:"
where.exe ninja

Write-Host ""
Write-Host "rc:"
where.exe rc

Write-Host ""
Write-Host "mt:"
where.exe mt

# ============================================================
# Configure Windows SDK
# ============================================================

Write-Host ""
Write-Host "Checking Windows SDK..."

if (-not $WindowsSdkRoot) {
    # The Windows 10/11 SDK installer records its install location in the
    # registry under "KitsRoot10" (Windows 11 SDKs are still versioned
    # under "...Windows Kits\10"). The installer is 32-bit, so on 64-bit
    # Windows it writes to the WOW6432Node key; check the native key too
    # in case the SDK was registered under 32-bit or ARM64 Windows.
    $KitsRootRegistryPaths = @(
        "HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows Kits\Installed Roots",
        "HKLM:\SOFTWARE\Microsoft\Windows Kits\Installed Roots"
    )

    foreach ($RegPath in $KitsRootRegistryPaths) {
        $RegValue = Get-ItemProperty -Path $RegPath -Name "KitsRoot10" -ErrorAction SilentlyContinue
        if ($RegValue -and (Test-Path $RegValue.KitsRoot10)) {
            $WindowsSdkRoot = $RegValue.KitsRoot10.TrimEnd('\')
            break
        }
    }

    if (-not $WindowsSdkRoot) {
        # Registry lookup failed; fall back to the standard install
        # locations before giving up.
        $WindowsSdkRootCandidates = @(
            "${env:ProgramFiles(x86)}\Windows Kits\10",
            "${env:ProgramFiles}\Windows Kits\10"
        )

        $WindowsSdkRoot = $WindowsSdkRootCandidates |
            Where-Object { $_ -and (Test-Path $_) } |
            Select-Object -First 1
    }

    if (-not $WindowsSdkRoot) {
        throw "Could not auto-detect an installed Windows 10/11 SDK. Pass -WindowsSdkRoot explicitly."
    }
}

Write-Host ""
Write-Host "Windows SDK root:"
Write-Host $WindowsSdkRoot

if (-not $WindowsSdkVersion) {
    $DetectedSdkVersion = Get-ChildItem "$WindowsSdkRoot\bin" -Directory -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match '^\d+\.\d+\.\d+\.\d+$' } |
        Sort-Object { [version]$_.Name } -Descending |
        Select-Object -First 1

    if (-not $DetectedSdkVersion) {
        throw "Could not auto-detect an installed Windows SDK version under $WindowsSdkRoot\bin. Pass -WindowsSdkVersion explicitly."
    }

    $WindowsSdkVersion = $DetectedSdkVersion.Name
}

Write-Host ""
Write-Host "Windows SDK version:"
Write-Host $WindowsSdkVersion

$env:WindowsSdkDir = "$WindowsSdkRoot\"
$env:WindowsSDKVersion = "$WindowsSdkVersion\"

$SdkBin = "$WindowsSdkRoot\bin\$WindowsSdkVersion\x64"
$SdkLib = "$WindowsSdkRoot\Lib\$WindowsSdkVersion\um\x64"

if (-not (Test-Path "$SdkBin\rc.exe")) {
    throw "rc.exe was not found: $SdkBin\rc.exe"
}

if (-not (Test-Path "$SdkBin\mt.exe")) {
    throw "mt.exe was not found: $SdkBin\mt.exe"
}

if (-not (Test-Path "$SdkLib\kernel32.lib")) {
    throw "kernel32.lib was not found: $SdkLib\kernel32.lib"
}

if (-not (Test-Path "$SdkLib\uuid.lib")) {
    throw "uuid.lib was not found: $SdkLib\uuid.lib"
}

# Make sure Windows SDK tools are first in PATH.
$env:PATH = "$SdkBin;$env:PATH"

# Make sure Windows SDK libraries are available to the linker.
$env:LIB = "$SdkLib;$env:LIB"

Write-Host "Windows SDK is OK."

# ============================================================
# Check vcpkg
# ============================================================

Write-Host ""
Write-Host "============================================================"
Write-Host "Checking vcpkg..."
Write-Host "============================================================"

Write-Host ""
Write-Host "VCPKG_ROOT:"
Write-Host $env:VCPKG_ROOT

$VcpkgExe = "$env:VCPKG_ROOT\vcpkg.exe"
$VcpkgToolchain = "$env:VCPKG_ROOT\scripts\buildsystems\vcpkg.cmake"

if (-not (Test-Path $VcpkgExe)) {
    throw "vcpkg.exe was not found: $VcpkgExe"
}

if (-not (Test-Path $VcpkgToolchain)) {
    throw "vcpkg CMake toolchain was not found: $VcpkgToolchain"
}

Write-Host "vcpkg is OK."

# ============================================================
# Check Qt
# ============================================================

Write-Host ""
Write-Host "============================================================"
Write-Host "Checking Qt..."
Write-Host "============================================================"

$QtRoot = "$env:VCPKG_ROOT\installed\x64-windows"
$QtDir = "$QtRoot\share\Qt6"
$QtConfig = "$QtDir\Qt6Config.cmake"
$QtToolsBin = "$QtRoot\tools\Qt6\bin"
$QtDebugBin = "$QtRoot\debug\bin"
$WinDeployQt = "$QtToolsBin\windeployqt.exe"
$WinDeployQtDebug = "$QtToolsBin\windeployqt.debug.bat"
$QtTranslationsRoot = "$QtRoot\translations\Qt6"
$QtTranslationsCatalog = "$QtTranslationsRoot\catalogs.json"

# Install Qt if Qt6Config.cmake is missing.
if (-not (Test-Path $QtConfig)) {
    Write-Host ""
    Write-Host "Qt6Config.cmake was not found."
    Write-Host "Installing Qt through vcpkg..."
    Write-Host ""

    & $VcpkgExe install qtbase:x64-windows

    if ($LASTEXITCODE -ne 0) {
        throw "vcpkg failed to install qtbase."
    }
}

# KeePassXC requires windeployqt on Windows. vcpkg's feature may require
# rebuilding qtbase and related Qt packages, so --recurse is intentional.
if (-not (Test-Path $WinDeployQt)) {
    Write-Host ""
    Write-Host "windeployqt.exe is missing."
    Write-Host "Installing Qt windeployqt feature through vcpkg."
    Write-Host ""
    Write-Host "Qt may need to be rebuilt with the additional feature."
    Write-Host "This can take some time."
    Write-Host ""

    & $VcpkgExe install "qtbase[windeployqt]:x64-windows" --recurse

    if ($LASTEXITCODE -ne 0) {
        throw "vcpkg failed to install qtbase[windeployqt]."
    }
}

# Qt translations are used by windeployqt during deployment. KeePassXC's
# project manifest does not declare qttranslations because it is a deployment
# dependency for this Windows script, not a library linked by KeePassXC.
# Force classic mode so this package is installed into the same global vcpkg
# tree used by windeployqt. Without --classic, vcpkg detects KeePassXC's
# vcpkg.json and rejects package arguments in manifest mode.
if (-not (Test-Path $QtTranslationsCatalog)) {
    Write-Host ""
    Write-Host "Qt translations were not found."
    Write-Host "Installing qttranslations through vcpkg..."
    Write-Host ""

    & $VcpkgExe install qttranslations:x64-windows --classic --recurse

    if ($LASTEXITCODE -ne 0) {
        throw "vcpkg failed to install qttranslations."
    }
}

if (-not (Test-Path $QtConfig)) {
    throw "Qt6Config.cmake still cannot be found: $QtConfig"
}

if (-not (Test-Path $WinDeployQt)) {
    throw "windeployqt.exe still cannot be found: $WinDeployQt"
}

if (-not (Test-Path $WinDeployQtDebug)) {
    throw "Debug windeployqt wrapper was not found: $WinDeployQtDebug"
}

if (-not (Test-Path "$QtDebugBin\Qt6Cored.dll")) {
    throw "Qt debug binaries were not found: $QtDebugBin"
}

if (-not (Test-Path $QtTranslationsCatalog)) {
    throw "Qt translations catalog was not found: $QtTranslationsCatalog"
}

Write-Host ""
Write-Host "Qt6 found:"
Write-Host $QtConfig

Write-Host ""
Write-Host "Qt debug binaries found:"
Write-Host $QtDebugBin

Write-Host ""
Write-Host "windeployqt found:"
Write-Host $WinDeployQt

Write-Host ""
Write-Host "Debug windeployqt wrapper found:"
Write-Host $WinDeployQtDebug

Write-Host ""
Write-Host "Qt translations found:"
Write-Host $QtTranslationsCatalog

# Add Qt tools to PATH for CMake and this script.
$env:PATH = "$QtToolsBin;$env:PATH"

Write-Host ""
Write-Host "windeployqt version:"
& $WinDeployQt --version

if ($LASTEXITCODE -ne 0) {
    throw "windeployqt was found but failed to run."
}

# ============================================================
# Check Asciidoctor
# ============================================================
# KeePassXC's docs/CMakeLists.txt requires the 'asciidoctor'
# command to be available. Asciidoctor is a Ruby gem.
# ============================================================

Write-Host ""
Write-Host "============================================================"
Write-Host "Checking Asciidoctor..."
Write-Host "============================================================"

$RubyBin = "$RubyRoot\bin"
$RubyExe = "$RubyBin\ruby.exe"
$GemExe = "$RubyBin\gem.cmd"
$AsciidoctorBat = "$RubyBin\asciidoctor.bat"

$ExistingAsciidoctor = Get-Command asciidoctor -ErrorAction SilentlyContinue

if ($ExistingAsciidoctor) {
    Write-Host ""
    Write-Host "asciidoctor is already available on PATH:"
    Write-Host $ExistingAsciidoctor.Source
} else {
    # Install a private Ruby runtime only when Asciidoctor is not already
    # available. This does not modify the system-wide PATH.
    if (-not (Test-Path $RubyExe)) {
        Write-Host ""
        Write-Host "Ruby was not found: $RubyExe"
        Write-Host "Looking up the latest RubyInstaller (x64, without Devkit)..."
        Write-Host ""

        $ReleaseInfo = Invoke-RestMethod `
            -Uri "https://api.github.com/repos/oneclick/rubyinstaller2/releases/latest" `
            -UseBasicParsing

        $RubyAsset = $ReleaseInfo.assets |
            Where-Object { $_.name -match '^rubyinstaller-\d.*-x64\.exe$' } |
            Select-Object -First 1

        if ($null -eq $RubyAsset) {
            throw "Could not find a non-Devkit x64 RubyInstaller release asset."
        }

        $InstallerPath = Join-Path $env:TEMP $RubyAsset.name

        Write-Host "Downloading:"
        Write-Host $RubyAsset.browser_download_url

        Invoke-WebRequest `
            -Uri $RubyAsset.browser_download_url `
            -OutFile $InstallerPath `
            -UseBasicParsing

        # The download carries the internet Mark-of-the-Web. Unblocking
        # it here reduces (but doesn't guarantee removal of) a Windows
        # SmartScreen prompt on first run, since it's a fresh, low-
        # reputation binary as far as Windows is concerned.
        Unblock-File -Path $InstallerPath -ErrorAction SilentlyContinue

        Write-Host ""
        Write-Host "Installing Ruby to $RubyRoot ..."
        Write-Host "Private install: no file associations and no system-wide PATH change."
        Write-Host ""
        Write-Host "If this appears to hang, check for a hidden Windows SmartScreen"
        Write-Host "or User Account Control prompt (Alt+Tab) and approve it."
        Write-Host ""

        $InstallArgs = @(
            "/VERYSILENT",
            "/SUPPRESSMSGBOXES",
            "/DIR=`"$RubyRoot`"",
            "/TASKS=noassocfiles,nomodpath"
        )

        $RubyInstall = Start-Process -FilePath $InstallerPath -ArgumentList $InstallArgs -Wait -PassThru

        if ($RubyInstall.ExitCode -ne 0) {
            throw "RubyInstaller failed with exit code $($RubyInstall.ExitCode)."
        }
    }

    if (-not (Test-Path $RubyExe)) {
        throw "Ruby still cannot be found: $RubyExe"
    }

    Write-Host ""
    Write-Host "Ruby found:"
    Write-Host $RubyExe

    # Add Ruby to this PowerShell process only.
    $env:PATH = "$RubyBin;$env:PATH"

    if (-not (Test-Path $AsciidoctorBat)) {
        Write-Host ""
        Write-Host "Installing the asciidoctor gem..."
        Write-Host ""

        & $GemExe install asciidoctor --no-document

        if ($LASTEXITCODE -ne 0) {
            throw "gem failed to install asciidoctor."
        }
    }

    if (-not (Test-Path $AsciidoctorBat)) {
        throw "asciidoctor still cannot be found: $AsciidoctorBat"
    }

    Write-Host ""
    Write-Host "asciidoctor found:"
    Write-Host $AsciidoctorBat
}

# If an existing system Asciidoctor was found, no PATH modification is
# necessary. Otherwise RubyBin was added above.
Write-Host ""
Write-Host "asciidoctor version:"
& asciidoctor --version

if ($LASTEXITCODE -ne 0) {
    throw "asciidoctor was found but failed to run."
}

# ============================================================
# Enter KeePassXC repository
# ============================================================

Set-Location $Repo

Write-Host ""
Write-Host "Repository:"
Write-Host (Get-Location)

# ============================================================
# Clean build (optional)
# ============================================================

if ($Clean) {
    Write-Host ""
    Write-Host "============================================================"
    Write-Host "Removing previous build directory..."
    Write-Host "============================================================"

    if (Test-Path ".\build") {
        Remove-Item ".\build" -Recurse -Force
    }
} else {
    Write-Host ""
    Write-Host "Keeping existing build directory."
    Write-Host "Use -Clean for a completely fresh CMake configuration."
}

# ============================================================
# Configure CMake
# ============================================================

Write-Host ""
Write-Host "============================================================"
Write-Host "Configuring KeePassXC..."
Write-Host "============================================================"

# KeePassXC invokes WINDEPLOYQT_EXE from its Windows post-build rule.
# vcpkg provides a Debug wrapper that correctly resolves Qt's debug DLLs
# from debug\bin instead of looking for Qt6*Debug DLLs in the release bin.
# KeePassXC's WITH_TESTS option defaults to ON; disable tests for this
# application-focused Windows Debug/accessibility build so test deployment
# steps cannot introduce unnecessary file-lock failures.
cmake -S . -B build `
    -G Ninja `
    -DCMAKE_BUILD_TYPE=Debug `
    -DCMAKE_TOOLCHAIN_FILE="$VcpkgToolchain" `
    -DQt6_DIR="$QtDir" `
    -DWINDEPLOYQT_EXE="$WinDeployQtDebug" `
    -DWITH_TESTS=OFF

if ($LASTEXITCODE -ne 0) {
    throw "CMake configuration failed."
}

# ============================================================
# Build KeePassXC
# ============================================================

Write-Host ""
Write-Host "============================================================"
Write-Host "Building KeePassXC..."
Write-Host "============================================================"

# vcpkg stores Debug Qt DLLs under debug\bin. Keep that directory first
# in PATH for any Qt tools that resolve runtime dependencies through PATH.
$env:PATH = "$QtDebugBin;$QtToolsBin;$env:PATH"

cmake --build build --parallel

if ($LASTEXITCODE -ne 0) {
    throw "KeePassXC build failed."
}

# ============================================================
# Find KeePassXC executable
# ============================================================

Write-Host ""
Write-Host "============================================================"
Write-Host "Build completed."
Write-Host "============================================================"

$KeePassXC = Get-ChildItem ".\build" `
    -Recurse `
    -Filter "keepassxc.exe" `
    -File `
    -ErrorAction SilentlyContinue |
    Select-Object -First 1

if ($null -eq $KeePassXC) {
    throw "Could not find keepassxc.exe."
}

# ============================================================
# Launch KeePassXC
# ============================================================

Write-Host ""
Write-Host "KeePassXC executable:"
Write-Host $KeePassXC.FullName

Write-Host ""
Write-Host "Launching KeePassXC..."

Start-Process $KeePassXC.FullName

Write-Host ""
Write-Host "KeePassXC has been launched."
