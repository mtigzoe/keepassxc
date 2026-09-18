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
