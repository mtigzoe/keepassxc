[CmdletBinding()]
param(
    # Left blank to use the VS Code CMake Tools build directory next to this
    # checkout (see .vscode/settings.json's cmake.buildDirectory).
    [string]$BuildDir = "",
    [string]$Config = "Debug",

    # Skip the "cmake --build" step and just launch whatever was built last.
    [switch]$NoBuild
)

$ErrorActionPreference = "Stop"

# ============================================================
# Build (or reuse) and launch KeePassXC from build-vscode-x64
# ============================================================
# Companion to build-debug.ps1: that script performs a full clean-room
# Ninja + vcpkg + Qt setup under .\build. This script instead targets the
# multi-config Visual Studio build directory already configured by VS
# Code's CMake Tools (build-vscode-x64), and just builds/launches it.
#
# No PATH or environment overrides are needed here: KeePassXC's CMake
# POST_BUILD step runs windeployqt (as configured in build-vscode-x64's
# CMake cache), which deploys Qt's runtime DLLs next to KeePassXC.exe.
# ============================================================

$ScriptDir =
    if ($PSScriptRoot) { $PSScriptRoot }
    elseif ($PSCommandPath) { Split-Path -Parent $PSCommandPath }
    else { (Get-Location).Path }

$Repo = Split-Path -Parent $ScriptDir

if (-not $BuildDir) { $BuildDir = Join-Path $Repo "build-vscode-x64" }

if (-not (Test-Path $BuildDir -PathType Container)) {
    throw "Build directory was not found: $BuildDir`nConfigure it first (VS Code CMake Tools, or see .vscode/README.md)."
}

if (-not $NoBuild) {
    Write-Host ""
    Write-Host "Building KeePassXC ($Config)..."
    Write-Host ""

    cmake --build $BuildDir --config $Config --target KeePassXC

    if ($LASTEXITCODE -ne 0) {
        throw "KeePassXC build failed."
    }
}

$ExePath = Join-Path $BuildDir "src\$Config\KeePassXC.exe"

if (-not (Test-Path $ExePath)) {
    throw "KeePassXC.exe was not found: $ExePath`nBuild it first (omit -NoBuild), or check -Config/-BuildDir."
}

Write-Host ""
Write-Host "Launching KeePassXC:"
Write-Host $ExePath

Start-Process -FilePath $ExePath -WorkingDirectory (Split-Path -Parent $ExePath)
