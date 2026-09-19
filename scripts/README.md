<<<<<<< HEAD
# Development Scripts and CMake/CTest Workflows

The normal Windows development workflow uses CMake Tools in VS Code to configure/build KeePassXC, F5 to launch the application, and CTest to run regression tests. The older PowerShell test runner is not required for the normal workflow.

## VS Code workflow

1. Open the KeePassXC repository in VS Code.
2. Let CMake Tools configure the `build-vscode-x64` build directory.
3. Use **Ctrl+Shift+B** to build KeePassXC.
4. Use **F5** to launch KeePassXC under the debugger.

The build configuration should have tests enabled. KeePassXC defaults to:

- `WITH_TESTS=ON` — builds the unit-test targets and enables CTest.
- `WITH_GUI_TESTS=OFF` — controls the separate GUI-test option.

If an existing build directory was configured without tests, reconfigure it with:

```powershell
cmake -S . -B build-vscode-x64 -G "Visual Studio 18 2026" -A x64 -DWITH_TESTS=ON
```

Then build:

```powershell
cmake --build build-vscode-x64 --config Debug --parallel
```

## CTest

Run all registered tests:

```powershell
ctest --test-dir build-vscode-x64 -C Debug --output-on-failure
```

List registered tests without running them:

```powershell
ctest --test-dir build-vscode-x64 -C Debug -N
```

Run the accessibility regression suites:

```powershell
ctest --test-dir build-vscode-x64 -C Debug -R "testaccessibility|testwindowsaccessibility|testwindowsaccessibilitytree" --output-on-failure
```

Run an individual suite:

```powershell
ctest --test-dir build-vscode-x64 -C Debug -R "^testaccessibility$" --output-on-failure
ctest --test-dir build-vscode-x64 -C Debug -R "^testwindowsaccessibility$" --output-on-failure
ctest --test-dir build-vscode-x64 -C Debug -R "^testwindowsaccessibilitytree$" --output-on-failure
```

If CTest reports **No tests were found**, the build directory was likely configured without `WITH_TESTS=ON), or the test targets have not been configured/built yet. Reconfigure and build, then run `ctest ... -N` again.

## Reconfiguring after CMake changes

When CMake files, Qt `.ui` files, or generated build configuration become stale, reconfigure the existing build directory:

```powershell
cmake -S . -B build-vscode-x64
```

For a clean rebuild, remove the build directory and let CMake Tools configure it again, or use a clean CMake Tools configuration.

## vcpkg dependencies

KeePassXC uses the project's configured vcpkg environment. Do not install a second unrelated vcpkg copy just to repair a build.

If CMake reports a missing vcpkg library such as:

```
vcpkg_installed\\x64-windows\\debug\\lib\\zlibd.lib
```

first locate the vcpkg executable configured for the repository and use that copy to install/repair the dependency. For example, if the repository is alongside a vcpkg checkout:

```powershell
..\\vcpkg\\vcpkg.exe install zlib:x64-windows
```

Then verify the expected library exists before rebuilding:

```powershell
Test-Path ..\\vcpkg_installed\\x64-windows\\debug\\lib\\zlibd.lib
```

## Accessibility test targets

The accessibility suites are:

- `testaccessibility` — general Qt accessibility regression tests.
- `testwindowsaccessibility` — Windows accessibility/UI Automation tests.
- `testwindowsaccessibilitytree` — Windows UI Automation accessibility-tree tests.

For accessibility work, the recommended loop is:

**Ctrl+Shift+B → F5 → manually verify with JAWS/NVDA → CTest accessibility suites.**

Run commands from the repository root so the relative `build-vscode-x64` path resolves correctly.
=======
# Scripts

Helper PowerShell scripts for the Windows development workflow. These are
optional conveniences; KeePassXC's standard build documentation
([INSTALL.md](../INSTALL.md)) remains authoritative.

## build-debug.ps1

Performs a full clean-room Debug build: locates/loads the Visual Studio
Developer environment, verifies vcpkg, installs any missing Qt features
through vcpkg, configures CMake with Ninja into `.\build`, builds, and
launches the resulting `KeePassXC.exe` at the end.

```powershell
.\scripts\build-debug.ps1
```

Use `-Clean` to force a completely fresh CMake configuration. See the
script's own `param()` block for all other overrides (repo/vcpkg/Ruby
locations, Visual Studio/Windows SDK detection, etc.).

## run-vscode-debug.ps1

A lighter companion for the VS Code + CMake Tools workflow described in
[.vscode/README.md](../.vscode/README.md). It targets the existing
`build-vscode-x64` build directory (Visual Studio generator, multi-config)
instead of creating its own build tree, and just builds and launches
`KeePassXC.exe`:

```powershell
.\scripts\run-vscode-debug.ps1
```

Options:

- `-Config <name>` — build configuration to use (default `Debug`).
- `-BuildDir <path>` — use a build directory other than `build-vscode-x64`.
- `-NoBuild` — skip the build step and just launch whatever was built last.

No PATH or environment variables need to be set for the launched process:
KeePassXC's CMake `POST_BUILD` step runs `windeployqt`, which deploys Qt's
runtime DLLs next to `KeePassXC.exe` as part of the build itself. If
`KeePassXC.exe` still reports a missing DLL after building, verify that
`build-vscode-x64`'s CMake cache has `WINDEPLOYQT_EXE` pointing at
`windeployqt.debug.bat` (the Debug-config wrapper), not plain
`windeployqt.exe` — see `.vscode/README.md`'s troubleshooting section.

## Equivalent one-line CMake command

Both scripts wrap the same basic idea. From the repository root, with
`build-vscode-x64` already configured:

```powershell
cmake --build build-vscode-x64 --config Debug --target KeePassXC
.\build-vscode-x64\src\Debug\KeePassXC.exe
```

CMake itself has no built-in "run" command — `cmake --build` only builds.
Launching the produced executable is a separate step, which is what these
scripts automate.
>>>>>>> 2d2c620c (Debugged the Keepassxc launch)
