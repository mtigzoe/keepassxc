# Accessibility Testing

This accessibility test suite uses several layers of tools and technologies to test KeePassXC from the application source through the platform accessibility APIs and, ultimately, assistive technology.

## Accessibility testing stack

1. **CMake** — configuring and building the accessibility test targets.
2. **CTest** — discovering and running the tests in a repeatable way.
3. **Qt QAccessible** — testing what Qt itself exposes to assistive technology.
4. **Python** — inspecting and validating the Windows UI Automation tree and automating diagnostics.
5. **Windows UI Automation (UIA)** — checking what Windows actually exposes to screen readers.
6. **JAWS/NVDA** — ultimately verifying the real assistive-technology experience.
7. **GitHub Actions** — running the accessibility tests automatically on Windows and Linux/AT-SPI.

These layers complement one another. A control can exist in KeePassXC and be represented by Qt without being exposed correctly through Windows UI Automation, so accessibility testing needs to cover more than one layer.

## Why `xa11y-inspect.py` is needed

The C++ accessibility tests are **regression tests**: they assert known expected behavior and fail when that behavior changes. They are not designed to answer the exploratory question, **“What exactly is this control in the live accessibility tree?”**

`tests/accessibility/xa11y-inspect.py` fills that diagnostic gap. It connects to a running KeePassXC instance through the `xa11y` accessibility API and lets us inspect what the application is actually exposing at runtime.

It can:

- list buttons and text fields visible to the accessibility API;
- list all named elements and their exposed roles;
- find **unnamed interactive buttons** that need investigation;
- print raw platform accessibility data, including Windows UIA properties when available;
- print an element's **ancestor chain**, helping identify which Qt/KeePassXC widget produced an unexpected accessibility element;
- detect the same accessible name exposed under different roles, which can reveal duplicate-announcement or duplicate-exposure patterns;
- inspect the **live application**, rather than relying only on source code or assumptions about Qt's accessibility exposure.

This is especially useful during an accessibility bug hunt. If JAWS appears to announce a control incorrectly, the diagnostic script helps determine whether the issue is an incorrect accessible name, role, duplicate element, unexpected container, or another UIA exposure problem before changing the KeePassXC source.

### Diagnostic tool vs. regression test

`xa11y-inspect.py` should **not** replace the C++ Windows UIA tests. They serve different purposes:

| Tool | Purpose |
| --- | --- |
| `xa11y-inspect.py` | Exploratory inspection and diagnosis of the live accessibility tree |
| `dump_uia_tree.py` | Broad diagnostic dump of the Windows UIA tree |
| `testwindowsaccessibility` | Automated Windows UIA regression assertions |
| `testwindowsaccessibilitytree` | Automated Windows UIA tree/interaction regression assertions |
| `testaccessibility` | In-process Qt accessibility regression assertions |
| JAWS/NVDA | Final assistive-technology behavior and speech/braille validation |

A typical workflow is:

1. Reproduce or observe the accessibility problem.
2. Use `xa11y-inspect.py` to identify the actual exposed element and its role, name, raw platform data, and ancestors.
3. Use that information to locate the corresponding Qt/KeePassXC widget.
4. Fix the underlying accessibility implementation.
5. Add a deterministic C++ regression test where the behavior can be asserted.
6. Re-run the diagnostic tool to confirm the live accessibility tree is correct.
7. Run the relevant CTest tests.
8. Perform the relevant manual JAWS/NVDA validation.

### Run `xa11y-inspect.py`

With KeePassXC running, execute from the repository root:

```powershell
uv run tests\accessibility\xa11y-inspect.py
```

By default the script connects to KeePassXC by application name. To target a specific process ID:

```powershell
uv run tests\accessibility\xa11y-inspect.py <PID>
```

The output includes buttons, text fields, named elements, unnamed buttons with detailed raw accessibility data, ancestor chains, and names exposed across multiple roles.

Use this script when you need to **discover and diagnose** the live accessibility tree. Once a defect is understood, encode the expected behavior in a C++ regression test so CI can catch future regressions.

## Windows Debug Build and UI Automation Diagnostics

These instructions describe how to build the Windows Debug version of KeePassXC and inspect the Windows UI Automation (UIA) tree used by assistive technologies such as JAWS.

### Prerequisites

The Windows debug build requires the KeePassXC checkout, vcpkg, CMake, and the required Visual Studio/Windows SDK tooling. The Visual Studio developer environment must be initialized so that MSVC can find its standard C++ headers and Windows SDK libraries.

### One-paste CMake + CTest workflow

From the KeePassXC repository root in PowerShell, the following **single command** configures CMake with tests enabled, initializes the Visual Studio 2026 x64 environment, builds the Debug configuration, verifies CTest discovery, and runs both Windows accessibility tests.

```powershell
cmd /c '\"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat\" -arch=x64 && cmake -S . -B build -DWITH_TESTS=ON -DWITH_GUI_TESTS=ON && cmake --build build --config Debug --parallel && ctest --test-dir build -C Debug -N && ctest --test-dir build -C Debug -R testwindowsaccessibility --output-on-failure'
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

The command stops when the Visual Studio environment initialization, CMake configuration, or build fails. CTest test output is displayed when a selected accessibility test fails.

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

### Run the Qt accessibility test (`testaccessibility`)

The in-process Qt accessibility test is CTest test #46. When building this test on Windows, use the Visual Studio 2026 x64 developer environment and a **serial Debug build** (`--parallel 1`). The serial build avoids intermittent vcpkg `z-applocal` file-locking failures seen during parallel builds.

From the KeePassXC repository root in PowerShell:

```powershell
cmd /c '\"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat\" -arch=x64 && cmake --build build --config Debug --parallel 1 && ctest --test-dir build -C Debug -R testaccessibility --output-on-failure'
```

This command:

1. Initializes the Visual Studio 2026 x64 developer environment.
2. Rebuilds the Debug configuration serially.
3. Runs only the `testaccessibility` CTest target.
4. Prints the QtTest failure output when the test fails.

Use this command when iterating on `tests/accessibility/TestAccessibility.cpp`. Run `git diff --check` before the build to catch whitespace errors.

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
