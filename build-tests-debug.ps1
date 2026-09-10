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
# Check Windows SDK
# ============================================================
# Required for the POST_BUILD windeployqt step that runs when building
# testwindowsaccessibility and testwindowsaccessibilitytree (they embed
# $<TARGET_FILE:${PROGNAME}> and thus build the full KeePassXC.exe app).

Write-Host ""
Write-Host "============================================================"
Write-Host "Checking Windows SDK..."
Write-Host "============================================================"

$WindowsSdkRoot = ""
$WindowsSdkVersion = ""

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
    throw "Could not auto-detect an installed Windows 10/11 SDK."
}

Write-Host ""
Write-Host "Windows SDK root:"
Write-Host $WindowsSdkRoot

$DetectedSdkVersion = Get-ChildItem "$WindowsSdkRoot\bin" -Directory -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -match '^\d+\.\d+\.\d+\.\d+$' } |
    Sort-Object { [version]$_.Name } -Descending |
    Select-Object -First 1

if (-not $DetectedSdkVersion) {
    throw "Could not auto-detect an installed Windows SDK version under $WindowsSdkRoot\bin."
}

$WindowsSdkVersion = $DetectedSdkVersion.Name

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
$QtSvgConfig = Join-Path $QtRoot "share\Qt6Svg\Qt6SvgConfig.cmake"
$QtSvgWidgetsConfig = Join-Path $QtRoot "share\Qt6SvgWidgets\Qt6SvgWidgetsConfig.cmake"
$WinDeployQtDebug = Join-Path $QtToolsBin "windeployqt.debug.bat"
$QtTranslationsRoot = Join-Path $QtRoot "translations\Qt6"
$QtTranslationsCatalog = Join-Path $QtTranslationsRoot "catalogs.json"

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

# keepassxc_gui (linked by every accessibility test target, including
# testaccessibility) requires the Qt6 Svg and SvgWidgets components. vcpkg
# ships these via a separate "qtsvg" port, not as part of qtbase.
if (-not (Test-Path $QtSvgConfig) -or -not (Test-Path $QtSvgWidgetsConfig)) {
    throw "Qt6SvgConfig.cmake / Qt6SvgWidgetsConfig.cmake was not found under: $QtRoot\share`nRun your normal build-debug.ps1 first so vcpkg can install the qtsvg port."
}

# testwindowsaccessibility and testwindowsaccessibilitytree embed
# $<TARGET_FILE:${PROGNAME}> (the KeePassXC.exe path), so building either
# one also builds the full KeePassXC.exe app, which runs a POST_BUILD
# windeployqt step on Windows. Plain windeployqt.exe does not correctly
# resolve Qt's debug DLLs out of vcpkg's separate debug\bin tree, so this
# must be the debug wrapper -- the same one build-debug.ps1 uses.
if (-not (Test-Path $WinDeployQtDebug)) {
    throw "Debug windeployqt wrapper was not found: $WinDeployQtDebug`nRun your normal build-debug.ps1 first so vcpkg can install qtbase[windeployqt]."
}

# Qt translations are used by windeployqt during deployment. The POST_BUILD
# step for testwindowsaccessibility and testwindowsaccessibilitytree invokes
# windeployqt on the full KeePassXC.exe, which requires the translations catalog.
if (-not (Test-Path $QtTranslationsCatalog)) {
    throw "Qt translations catalog was not found: $QtTranslationsCatalog`nRun your normal build-debug.ps1 first so vcpkg can install qttranslations."
}

Write-Host ""
Write-Host "Qt:"
Write-Host $QtConfig

Write-Host ""
Write-Host "Qt debug binaries:"
Write-Host $QtDebugBin

Write-Host ""
Write-Host "Debug windeployqt wrapper:"
Write-Host $WinDeployQtDebug

Write-Host ""
Write-Host "Qt translations catalog:"
Write-Host $QtTranslationsCatalog

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
    -DWINDEPLOYQT_EXE="$WinDeployQtDebug" `
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

    if ($Target -in @("testwindowsaccessibility", "testwindowsaccessibilitytree")) {
        Write-Host "(This target embeds the KeePassXC.exe path, so it also"
        Write-Host " builds the full GUI app first -- expect this step to"
        Write-Host " take noticeably longer than testaccessibility did.)"
    }

    cmake --build $BuildDir --target $Target --parallel

    if ($LASTEXITCODE -ne 0) {
        throw "Build failed for target: $Target"
    }
}

# ============================================================
# Optionally run the requested test(s) via CTest
# ============================================================

$AnyTestRequested = $RunAccessibilityTest -or $RunWindowsAccessibilityTest -or $RunWindowsAccessibilityTreeTest

if ($AnyTestRequested) {
    # Match the environment used by the Windows accessibility CI job.
    $env:QT_QPA_PLATFORM = "windows"
    $env:QT_ACCESSIBILITY = "1"

    # Build the CTest regex filter from requested tests.
    # Each -Run* switch maps to the exact CTest test name.
    $TestFilters = @()
    if ($RunAccessibilityTest) {
        $TestFilters += '^testaccessibility$'
    }
    if ($RunWindowsAccessibilityTest) {
        $TestFilters += '^testwindowsaccessibility$'
    }
    if ($RunWindowsAccessibilityTreeTest) {
        $TestFilters += '^testwindowsaccessibilitytree$'
    }

    # Join with '|' for CTest's -R regex (OR semantics).
    $FilterRegex = $TestFilters -join '|'

    $CtestArgs = @(
        "--test-dir", $BuildDir,
        "-C", "Debug",
        "-R", $FilterRegex,
        "--output-on-failure"
    )

    if ($VerboseTest) {
        $CtestArgs += "--verbose"
    }

    Write-Host ""
    Write-Host "============================================================"
    Write-Host "Running accessibility tests via CTest..."
    Write-Host "============================================================"
    Write-Host ""
    Write-Host "ctest $CtestArgs"

    ctest @CtestArgs

    if ($LASTEXITCODE -ne 0) {
        throw "CTest failed with exit code $LASTEXITCODE."
    }

    Write-Host ""
    Write-Host "All requested accessibility tests passed."
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

if (-not $AnyTestRequested) {
    Write-Host ""
    Write-Host "To build and run a test with detailed QTest output, e.g.:"
    Write-Host ""
    Write-Host ".\build-tests-debug.ps1 -RunAccessibilityTest -VerboseTest"
    Write-Host ".\build-tests-debug.ps1 -RunWindowsAccessibilityTest -VerboseTest"
    Write-Host ".\build-tests-debug.ps1 -RunWindowsAccessibilityTreeTest -VerboseTest"
    Write-Host ""
    Write-Host "To run all accessibility tests:"
    Write-Host ""
    Write-Host ".\build-tests-debug.ps1 -RunAccessibilityTest -RunWindowsAccessibilityTest -RunWindowsAccessibilityTreeTest -VerboseTest"
}
