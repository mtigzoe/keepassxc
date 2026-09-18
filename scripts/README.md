# Development Scripts

## Accessibility test suite

The `build-tests-debug.ps1` script can build and run the accessibility regression tests locally.

### Run all accessibility tests

From the repository root in PowerShell:

```powershell
.\scripts\build-tests-debug.ps1 -RunAccessibilityTest -RunWindowsAccessibilityTest -RunWindowsAccessibilityTreeTest -VerboseTest
```

This runs:

- `testaccessibility` — general Qt accessibility regression tests.
- `testwindowsaccessibility` — Windows accessibility/UIA tests.
- `testwindowsaccessibilitytree` — Windows UI Automation accessibility-tree tests.

`-VerboseTest` enables verbose test output.

### Run individual suites

General accessibility tests:

```powershell
.\scripts\build-tests-debug.ps1 -RunAccessibilityTest
```

Windows accessibility tests:

```powershell
.\scripts\build-tests-debug.ps1 -RunWindowsAccessibilityTest
```

Windows accessibility-tree tests:

```powershell
.\scripts\build-tests-debug.ps1 -RunWindowsAccessibilityTreeTest
```

Add `-VerboseTest` to any command when detailed test output is needed.

## Requirements

The script is intended for the Windows development environment used to build KeePassXC with the project's configured Qt, vcpkg, and Visual Studio toolchain.

Run the commands from the repository root so the relative `scripts/` path resolves correctly.
