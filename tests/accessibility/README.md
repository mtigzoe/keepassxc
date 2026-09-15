# Accessibility Testing

## Windows Debug Build and UI Automation Diagnostics

These instructions describe how to build the Windows Debug version of KeePassXC and inspect the Windows UI Automation (UIA) tree used by assistive technologies such as JAWS.

### Prerequisites

The Windows debug build requires the KeePassXC checkout, vcpkg, CMake, and the required Visual Studio/Windows SDK tooling. The Visual Studio developer environment must be initialized so that MSVC can find its standard C++ headers and Windows SDK libraries.

If you are using a normal PowerShell session, initialize the Visual Studio 2026 x64 build environment before configuring or building:

```powershell
cmd /c """C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 && set" | ForEach-Object { if ($_ -match '^(.*?)=(.*)$') { Set-Item -Path "Env:$($matches[1])" -Value $matches[2] } }
```

This only initializes the environment for the current PowerShell session. A Visual Studio Developer PowerShell can be used instead.

### 1. Configure CMake with tests enabled

From the KeePassXC repository root in PowerShell:

```powershell
cmake -S . -B build -DWITH_TESTS=ON -DWITH_GUI_TESTS=ON
```

`WITH_TESTS=ON` enables the test suite and `WITH_GUI_TESTS=ON` enables GUI tests, including the Windows accessibility tests. This configuration must be done before CTest can discover the tests.

You can verify the configuration with:

```powershell
cmake -S . -B build -LAH | Select-String "WITH_TESTS|WITH_GUI_TESTS"
```

The expected result is:

```text
WITH_GUI_TESTS:BOOL=ON
WITH_TESTS:BOOL=ON
```

### 2. Build the Debug configuration

After configuring CMake:

```powershell
cmake --build build --config Debug --parallel
```

If the build was previously configured without tests, re-run the CMake configuration command above before building.

The expected executable is:

```text
build\src\KeePassXC.exe
```

If KeePassXC is not already running, launch it explicitly:

```powershell
Start-Process .\build\src\KeePassXC.exe
```

Using the explicit path is useful when verifying accessibility fixes because the UIA diagnostic connects to an already-running KeePassXC process.

If the linker reports that `KeePassXC.exe` cannot be written, make sure an existing KeePassXC process is not holding the executable open. If the linker reports a corrupt `.pdb` file, the affected PDB can be removed and that target rebuilt.

### 3. One-paste build and CTest command

After CMake has already been configured with tests enabled, the following PowerShell command can be pasted once. It initializes the Visual Studio 2026 x64 environment, builds the Debug configuration, lists the registered tests, and then runs both Windows accessibility tests. It stops if the build or CTest discovery fails.

```powershell
cmd /c """C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=x64 && set" | ForEach-Object { if ($_ -match '^(.*?)=(.*)$') { Set-Item -Path "Env:$($matches[1])" -Value $matches[2] } }; if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }; cmake --build build --config Debug --parallel; if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }; ctest --test-dir build -C Debug -N; if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }; ctest --test-dir build -C Debug -R '^testwindowsaccessibility(tree)?$' --output-on-failure
```

The final CTest command runs:

```text
Test #47: testwindowsaccessibility
Test #48: testwindowsaccessibilitytree
```

This is the preferred repeatable workflow for the Windows accessibility regression tests. CTest discovery with `-N` does not execute tests; the final CTest command executes the two selected tests.

### 4. Run CTest and inspect registered tests

Once CMake has been configured with tests enabled, CTest can show the tests registered in the build tree:

```powershell
ctest --test-dir build -C Debug -N
```

This is a discovery/listing operation; it does not execute the tests. A full test-enabled configuration currently registers the KeePassXC test suite, including the Windows accessibility tests. The Windows accessibility tests appear as:

```text
Test #47: testwindowsaccessibility
Test #48: testwindowsaccessibilitytree
```

If CTest reports `Total Tests: 0`, the build directory was configured without tests. Re-run:

```powershell
cmake -S . -B build -DWITH_TESTS=ON -DWITH_GUI_TESTS=ON
```

and rebuild.

### 5. Run the Windows accessibility tests with CTest

Run the Windows accessibility regression test:

```powershell
ctest --test-dir build -C Debug -R "testwindowsaccessibility" --output-on-failure
```

This tells CTest to:

- use the `build` CMake build directory;
- select the Debug configuration;
- run tests whose name matches `testwindowsaccessibility`;
- print test output when a test fails.

For an exact-name match, use:

```powershell
ctest --test-dir build -C Debug -R '^testwindowsaccessibility$' --output-on-failure
```

The Windows accessibility tree regression test can be run separately with:

```powershell
ctest --test-dir build -C Debug -R '^testwindowsaccessibilitytree$' --output-on-failure
```

To run both Windows accessibility tests together:

```powershell
ctest --test-dir build -C Debug -R '^testwindowsaccessibility(tree)?$' --output-on-failure
```

If the test build was configured separately, use that build directory instead of `build`.

A successful run should end with a CTest summary showing the selected test(s) as passed. If a test fails, `--output-on-failure` displays the test's diagnostic output so the failure can be investigated.

### 6. Dump the Windows UIA tree

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

### 7. Manual JAWS 2026 validation

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

1. Initialize the Visual Studio x64 developer environment.
2. Configure CMake with `WITH_TESTS=ON` and `WITH_GUI_TESTS=ON`.
3. Build the Debug configuration with `cmake --build build --config Debug --parallel`.
4. Use `ctest --test-dir build -C Debug -N` to verify that tests are registered.
5. Launch the exact executable from `build\src\KeePassXC.exe`.
6. Run `dump_uia_tree.py` to inspect the live Windows UIA tree.
7. Identify actual accessibility defects rather than treating every unnamed container as a defect.
8. Fix the underlying Qt/KeePassXC accessibility implementation.
9. Add or update a C++ Windows UIA regression test.
10. Rebuild and rerun the diagnostic.
11. Run the Windows accessibility CTest tests.
12. Perform the relevant manual JAWS 2026 regression test.
13. Push the changes and verify the Windows accessibility CI workflow.
