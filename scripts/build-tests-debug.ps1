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

    # Build only, unless one of the -Run* switches below is specified.
    # testaccessibility queries Qt's in-process QAccessible bridge; the two
    # Windows switches drive the real KeePassXC.exe through Windows UI
    # Automation, the same layer JAWS consumes.
    [switch]$RunAccessibilityTest,
    [switch]$RunWindowsAccessibilityTest,
    [switch]$RunWindowsAccessibilityTreeTest,

    # Pass -v2 to QTest for any test run above.
    [switch]$VerboseTest,

    # Remove the test build directory before configuring. Recommended after
    # a vcpkg reclone/reinstall, since a stale build-tests\CMakeCache.txt
    # can still reference the old, now-deleted vcpkg paths.
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
# Otherwise resolve the parent of the scripts directory as the repository.
# This matches the repository layout:
#   <repo>\scripts\build-tests-debug.ps1
if (-not $Repo) {
    if (Test-Path (Join-Path $ScriptDir "CMakeLists.txt")) {
        $Repo = $ScriptDir
    }
    else {
        $Repo = Split-Path -Parent $ScriptDir
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
