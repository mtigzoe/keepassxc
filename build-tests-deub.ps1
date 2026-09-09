[CmdletBinding()]
param(
    # Expected layout:
    #   <parent>\keepassxc\   <- KeePassXC source checkout
    #   <parent>\vcpkg\
    #
    # The script can also be placed directly in the KeePassXC repository.
    [string]$Repo = "",
    [string]$VcpkgRoot = "",

    # Separate build tree so the normal build-debug.ps1 tree remains
    # application-only (WITH_TESTS=OFF).
    [string]$BuildDir = "",

    # Build only, unless -RunAccessibilityTest is specified.
    [switch]$RunAccessibilityTest,

    # Pass -v2 to QTest when running testaccessibility.
    [switch]$VerboseTest,

    # Remove the test build directory before configuring.
    [switch]$Clean
)

$ErrorActionPreference = "Stop"

# ============================================================
# KeePassXC Windows Debug Test Build
# Visual Studio + Ninja + vcpkg + Qt
# ============================================================

# ============================================================
# Resolve script directory
# ============================================================

$ScriptDir =
    if ($PSScriptRoot) {
        $PSScriptRoot
    }
    elseif ($PSCommandPath) {
        Split-Path -Parent $PSCommandPath
    }
    elseif ($MyInvocation.MyCommand.Path) {
        Split-Path -Parent $MyInvocation.MyCommand.Path
    }
    else {
        (Get-Location).Path
    }

# If the script is in the repository root, use that directory.
# Otherwise preserve the same sibling-folder layout used by
# build-debug.ps1.
if (-not $Repo) {
    if (Test-Path (Join-Path $ScriptDir "CMakeLists.txt")) {
        $Repo = $ScriptDir
    }
    else {
        $Repo = Join-Path (Split-Path -Parent $ScriptDir) "keepassxc"
    }
}

if (-not $VcpkgRoot) {
    $ParentDir = Split-Path -Parent $Repo
    $VcpkgRoot = Join-Path $ParentDir "vcpkg"
}

if (-not $BuildDir) {
    $BuildDir = Join-Path $Repo "build-tests"
}

# ============================================================
# Validate paths
# ============================================================

if (-not (Test-Path $Repo -PathType Container)) {
    throw "KeePassXC repository was not found: $Repo"
}

if (-not (Test-Path (Join-Path $Repo "CMakeLists.txt") -PathType Leaf)) {
    throw "CMakeLists.txt was not found in the repository: $Repo"
}

if (-not (Test-Path $VcpkgRoot -PathType Container)) {
    throw "vcpkg directory was not found: $VcpkgRoot"
}

$VcpkgExe = Join-Path $VcpkgRoot "vcpkg.exe"
$VcpkgToolchain = Join-Path $VcpkgRoot "scripts\buildsystems\vcpkg.cmake"

if (-not (Test-Path $VcpkgExe)) {
    throw "vcpkg.exe was not found: $VcpkgExe"
}

if (-not (Test-Path $VcpkgToolchain)) {
    throw "vcpkg CMake toolchain was not found: $VcpkgToolchain"
}

# ============================================================
# Locate Visual Studio
# ============================================================

Write-Host ""
Write-Host "============================================================"
Write-Host "Locating Visual Studio..."
Write-Host "============================================================"

$VsWhere = Get-Command "vswhere.exe" -ErrorAction SilentlyContinue

if ($VsWhere) {
    $VsWherePath = $VsWhere.Source
}
else {
    $VsWhereCandidates = @(
        "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe",
        "${env:ProgramFiles}\Microsoft Visual Studio\Installer\vswhere.exe"
    )

    $VsWherePath = $VsWhereCandidates |
        Where-Object { $_ -and (Test-Path $_) } |
        Select-Object -First 1
}

if (-not $VsWherePath) {
    throw "vswhere.exe could not be found."
}

$VsInstallPath = & $VsWherePath `
    -latest `
    -prerelease `
    -products Microsoft.VisualStudio.Product.Community `
              Microsoft.VisualStudio.Product.Professional `
              Microsoft.VisualStudio.Product.Enterprise `
              Microsoft.VisualStudio.Product.BuildTools `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath

if (-not $VsInstallPath) {
    throw "Visual Studio with the C++ workload could not be found."
}

$VsDevShell = Join-Path `
    $VsInstallPath `
    "Common7\Tools\Launch-VsDevShell.ps1"

if (-not (Test-Path $VsDevShell)) {
    throw "Visual Studio Developer PowerShell was not found: $VsDevShell"
}

Write-Host ""
Write-Host "Visual Studio:"
Write-Host $VsInstallPath

# ============================================================
# Load Visual Studio x64 environment
# ============================================================

Write-Host ""
Write-Host "Loading Visual Studio x64 environment..."

& $VsDevShell -Arch amd64

if ($LASTEXITCODE -ne 0) {
    throw "Visual Studio Developer environment failed to load."
}

# Visual Studio can modify VCPKG_ROOT, so restore the intended one.
$env:VCPKG_ROOT = $VcpkgRoot

# ============================================================
# Check Ninja
# ============================================================

Write-Host ""
Write-Host "Checking Ninja..."

$Ninja = Get-Command ninja.exe -ErrorAction SilentlyContinue

if (-not $Ninja) {
    throw "ninja.exe could not be found on PATH."
}

Write-Host $Ninja.Source

# ============================================================
# Locate Qt through vcpkg
# ============================================================

Write-Host ""
Write-Host "============================================================"
Write-Host "Checking Qt..."
Write-Host "============================================================"

$QtRoot = Join-Path $VcpkgRoot "installed\x64-windows"
$QtDir = Join-Path $QtRoot "share\Qt6"
$QtConfig = Join-Path $QtDir "Qt6Config.cmake"
$QtToolsBin = Join-Path $QtRoot "tools\Qt6\bin"
$QtDebugBin = Join-Path $QtRoot "debug\bin"

if (-not (Test-Path $QtConfig)) {
    throw "Qt6Config.cmake was not found: $QtConfig`nRun your normal build-debug.ps1 first so vcpkg can install Qt."
}

if (-not (Test-Path $QtDebugBin)) {
    throw "Qt debug binaries were not found: $QtDebugBin"
}

$QtCoreDll = Join-Path $QtDebugBin "Qt6Cored.dll"

if (-not (Test-Path $QtCoreDll)) {
    throw "Qt6Cored.dll was not found: $QtCoreDll"
}

Write-Host ""
Write-Host "Qt:"
Write-Host $QtConfig

Write-Host ""
Write-Host "Qt debug binaries:"
Write-Host $QtDebugBin

# Make Qt tools and debug DLLs available.
$env:PATH = "$QtDebugBin;$QtToolsBin;$env:PATH"

# ============================================================
# Enter repository
# ============================================================

Set-Location $Repo

Write-Host ""
Write-Host "Repository:"
Write-Host (Get-Location)

Write-Host ""
Write-Host "Test build directory:"
Write-Host $BuildDir

# ============================================================
# Clean test build
# ============================================================

if ($Clean) {
    Write-Host ""
    Write-Host "============================================================"
    Write-Host "Removing previous test build..."
    Write-Host "============================================================"

    if (Test-Path $BuildDir) {
        Remove-Item $BuildDir -Recurse -Force
    }
}

# ============================================================
# Configure CMake
# ============================================================

Write-Host ""
Write-Host "============================================================"
Write-Host "Configuring KeePassXC accessibility test build..."
Write-Host "============================================================"

cmake -S $Repo -B $BuildDir `
    -G Ninja `
    -DCMAKE_BUILD_TYPE=Debug `
    -DCMAKE_TOOLCHAIN_FILE="$VcpkgToolchain" `
    -DQt6_DIR="$QtDir" `
    -DWITH_TESTS=ON `
    -DWITH_GUI_TESTS=ON `
    -DKPXC_FEATURE_NETWORK=OFF `
    -DKPXC_FEATURE_UPDATES=OFF `
    -DKPXC_FEATURE_DOCS=OFF

if ($LASTEXITCODE -ne 0) {
    throw "CMake configuration failed."
}

# ============================================================
# Build accessibility test targets
# ============================================================

Write-Host ""
Write-Host "============================================================"
Write-Host "Building accessibility tests..."
Write-Host "============================================================"

$Targets = @(
    "testaccessibility",
    "testwindowsaccessibility",
    "testwindowsaccessibilitytree"
)

foreach ($Target in $Targets) {
    Write-Host ""
    Write-Host "------------------------------------------------------------"
    Write-Host "Building target: $Target"
    Write-Host "------------------------------------------------------------"

    cmake --build $BuildDir --target $Target --parallel

    if ($LASTEXITCODE -ne 0) {
        throw "Build failed for target: $Target"
    }
}

# ============================================================
# Locate test executables
# ============================================================

Write-Host ""
Write-Host "============================================================"
Write-Host "Accessibility test executables"
Write-Host "============================================================"

$TestExecutables = @(
    "testaccessibility.exe",
    "testwindowsaccessibility.exe",
    "testwindowsaccessibilitytree.exe"
)

$FoundExecutables = @{}

foreach ($Executable in $TestExecutables) {
    $Found = Get-ChildItem $BuildDir `
        -Recurse `
        -Filter $Executable `
        -File `
        -ErrorAction SilentlyContinue |
        Select-Object -First 1

    if ($Found) {
        $FoundExecutables[$Executable] = $Found.FullName
        Write-Host ""
        Write-Host "$Executable:"
        Write-Host $Found.FullName
    }
    else {
        throw "Could not find $Executable under $BuildDir"
    }
}

# ============================================================
# Optionally run testaccessibility
# ============================================================

if ($RunAccessibilityTest) {
    $TestExecutable = $FoundExecutables["testaccessibility.exe"]

    Write-Host ""
    Write-Host "============================================================"
    Write-Host "Running Qt accessibility tests..."
    Write-Host "============================================================"

    # Match the environment used by the Windows accessibility CI job.
    $env:QT_QPA_PLATFORM = "windows"
    $env:QT_ACCESSIBILITY = "1"

    if ($VerboseTest) {
        Write-Host ""
        Write-Host "Running testaccessibility with QTest -v2..."
        & $TestExecutable -v2
    }
    else {
        & $TestExecutable
    }

    if ($LASTEXITCODE -ne 0) {
        throw "testaccessibility failed with exit code $LASTEXITCODE."
    }

    Write-Host ""
    Write-Host "testaccessibility passed."
}

# ============================================================
# Complete
# ============================================================

Write-Host ""
Write-Host "============================================================"
Write-Host "Accessibility test build completed."
Write-Host "============================================================"

Write-Host ""
Write-Host "Build directory:"
Write-Host $BuildDir

Write-Host ""
Write-Host "Available accessibility tests:"

foreach ($Executable in $FoundExecutables.Keys) {
    Write-Host $FoundExecutables[$Executable]
}

if (-not $RunAccessibilityTest) {
    Write-Host ""
    Write-Host "To run the Qt accessibility test with detailed QTest output:"
    Write-Host ""
    Write-Host ".\build-tests-debug.ps1 -RunAccessibilityTest -VerboseTest"
}
