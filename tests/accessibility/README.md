# Accessibility Testing

## Windows Debug Build and UI Automation Diagnostics

These instructions describe how to build the Windows Debug version of KeePassXC and inspect the Windows UI Automation (UIA) tree used by assistive technologies such as JAWS.

### Prerequisites

The Windows debug build script expects the KeePassXC checkout, vcpkg, and the required Visual Studio/Windows SDK tooling to be available in the layout documented by `build-debug.ps1`.

### 1. Run the Windows Debug build

From the KeePassXC repository root in PowerShell:

```powershell
powershell -ExecutionPolicy Bypass -File .\build-debug.ps1
```

This uses PowerShell's execution-policy bypass **for this invocation only**. It does not permanently change the system or user execution policy.

The build script configures a Debug build in the `build` directory and launches the resulting `KeePassXC.exe` when the build succeeds. The script currently uses `build`, not `build-debug`, as its CMake build directory.

To request a clean CMake configuration:

```powershell
powershell -ExecutionPolicy Bypass -File .\build-debug.ps1 -Clean
```

### 2. Find or launch the exact Debug executable

The expected executable is under the `build` directory. To locate it:

```powershell
Get-ChildItem -Path . -Filter KeePassXC.exe -Recurse -ErrorAction SilentlyContinue |
    Select-Object FullName, LastWriteTime, Length
```

The normal Debug build produced by `build-debug.ps1` is expected at:

```text
build\src\KeePassXC.exe
```

If KeePassXC is not already running, launch that executable explicitly:

```powershell
Start-Process .\build\src\KeePassXC.exe
```

Using the explicit path is useful when verifying accessibility fixes because the UIA diagnostic connects to an already-running KeePassXC process.

### 3. Dump the Windows UIA tree

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

### 4. Run the Windows accessibility tests with CTest

The Windows accessibility regression tests are registered with CTest during the CMake configuration. From the KeePassXC repository root, run:

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

If the test build was configured separately, use that build directory instead of `build`.

A successful run should end with a CTest summary showing the selected test(s) as passed. If a test fails, `--output-on-failure` displays the test's diagnostic output so the failure can be investigated.

### 5. Manual JAWS 2026 validation

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

1. Build the current Debug executable.
2. Launch the exact executable from `build\src\KeePassXC.exe`.
3. Run `dump_uia_tree.py` to inspect the live Windows UIA tree.
4. Identify actual accessibility defects rather than treating every unnamed container as a defect.
5. Fix the underlying Qt/KeePassXC accessibility implementation.
6. Add or update a C++ Windows UIA regression test.
7. Rebuild and rerun the diagnostic.
8. Run the Windows accessibility CTest tests.
9. Perform the relevant manual JAWS 2026 regression test.
10. Push the changes and verify the Windows accessibility CI workflow.
