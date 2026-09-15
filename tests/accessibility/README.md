# Accessibility Testing

## Windows Debug Build and UI Automation Diagnostics

These instructions describe how to build the Windows Debug version of KeePassXC and inspect the Windows UI Automation (UIA) tree used by assistive technologies such as JAWS.

### Prerequisites

The Windows debug build requires the KeePassXC checkout, vcpkg, CMake, and the required Visual Studio/Windows SDK tooling. The Visual Studio developer environment must be initialized so that MSVC can find its standard C++ headers and Windows SDK libraries.

### One-paste CMake + CTest workflow

From the KeePassXC repository root in PowerShell, the following **single command** configures CMake with tests enabled, initializes the Visual Studio 2026 x64 environment, builds the Debug configuration, verifies CTest discovery, and runs both Windows accessibility tests.

```powershell
cmd /c """C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 && set" | ForEach-Object { if ($_ -match '^(.*?)=(.*)$') { Set-Item -Path "Env:$($matches[1])" -Value $matches[2] } }; if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }; cmake -S . -B build -DWITH_TESTS=ON -DWITH_GUI_TESTS=ON; if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }; cmake --build build --config Debug --parallel; if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }; ctest --test-dir build -C Debug -N; if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }; ctest --test-dir build -C Debug -R '^testwindowsaccessibility(tree)?$' --output-on-failure
```

The workflow performs these steps automatically:

1. Initializes the Visual Studio 2026 x64 developer environment.
2. Configures CMake with `WITH_TESTS=ON` and `WITH_GUI_TESTS=ON`.
3. Builds the Debug configuration.
4. Lists the registered CTest tests with `-N` without executing them.
5. Runs the two Windows accessibility tests:

```text
Test #47: testwindowsaccessibility
Test #48: testwindowsaccessibilitytree
```

The command stops when the CMake configuration or build fails. CTest test output is displayed when a selected accessibility test fails.

The expected Debug executable is:

```text
build\src\KeePassXC.exe
```

If the linker reports that `KeePassXC.exe` cannot be written, make sure an existing KeePassXC process is not holding the executable open. If the linker reports a corrupt `.pdb` file, the affected PDB can be removed and that target rebuilt.

If CTest reports `Total Tests: 0`, check that the CMake configuration completed successfully with `WITH_TESTS=ON` and `WITH_GUI_TESTS=ON`.

### Manual CTest commands

After the one-paste workflow has completed, individual tests can also be run directly when needed.

Run the Windows accessibility regression test:

```powershell
ctest --test-dir build -C Debug -R '^testwindowsaccessibility$' --output-on-failure
```

Run the Windows accessibility tree regression test:

```powershell
ctest --test-dir build -C Debug -R '^testwindowsaccessibilitytree$' --output-on-failure
```

List all registered tests without running them:

```powershell
ctest --test-dir build -C Debug -N
```

### Dump the Windows UIA tree

With the Debug KeePassXC executable running:

```powershell
uv run tests\accessibility\windows\dump_uia_tree.py
```

The script connects to a running KeePassXC window and writes the result to:

```text
uia-tree-dump.txt
```

This is a diagnostic/discovery tool. It is not a replacement for the C++ Windows UIA regression tests or manual JAWS testing.

To inspect the generated dump:

```powershell
Get-Content .\uia-tree-dump.txt
```

To find controls that currently have no accessible name:

```powershell
Select-String -Path .\uia-tree-dump.txt -Pattern "\(no accessible name\)"
```

An unnamed UIA element is not automatically an accessibility defect. Containers and decorative elements may legitimately have no accessible name. Interactive controls should be investigated against their intended accessible name and role.

### Manual JAWS 2026 validation

Automated UIA tests verify the Windows accessibility interface, but they do not establish complete JAWS compatibility. After automated tests pass, manually validate important KeePassXC workflows with JAWS 2026, including:

- Main window and menus
- Create/open/unlock database
- Database tree and entry list
- Entry creation and editing
- Search
- Settings and important dialogs
- Lock/unlock
- Notifications and error messages
- Keyboard navigation and activation
- Speech output
- Braille output where applicable

Use `JAWS-2026-compatibility.md` in this directory for the manual test matrix.

### Recommended investigation loop

1. Run the one-paste CMake + CTest workflow above.
2. Launch the exact executable from `build\src\KeePassXC.exe`.
3. Run `dump_uia_tree.py` to inspect the live Windows UIA tree.
4. Identify actual accessibility defects rather than treating every unnamed container as a defect.
5. Fix the underlying Qt/KeePassXC accessibility implementation.
6. Add or update a C++ Windows UIA regression test.
7. Rebuild and rerun the diagnostic.
8. Run the Windows accessibility CTest tests.
9. Perform the relevant manual JAWS 2026 regression test.
10. Push the changes and verify the Windows accessibility CI workflow.
