# Accessibility tests

This directory contains KeePassXC's accessibility regression tests. The tests are split into layers so that accessibility regressions can be detected at the Qt level and, on Windows, at the native Windows UI Automation boundary.

## Test layers

### Qt / QAccessible

`TestAccessibility.cpp` exercises Qt's in-process accessibility interfaces through `QAccessible::queryAccessibleInterface()` against a real `MainWindow`.

This layer checks things such as:

- accessible roles and names
- visibility and enabled/disabled state
- keyboard focusability and Tab navigation
- toolbar controls and their relationship to the accessibility tree
- search controls
- entry and group views
- the Edit Entry dialog

These tests are intentionally independent of a running screen reader. They verify the accessibility tree exposed by Qt before it reaches a platform accessibility API.

### Windows UI Automation

The `windows/` tests launch the real `KeePassXC.exe` as a separate process and inspect it through Microsoft's Windows UI Automation (UIA) COM API.

There are two Windows suites:

- `testwindowsaccessibility` checks application identity, accessible controls, and basic UIA relationships.
- `testwindowsaccessibilitytree` checks the broader UIA control tree, including names, control types, and actionable control patterns.

This layer is important because a Qt accessibility test passing does not by itself prove that Windows assistive technology receives the expected native UIA tree.

## Running the tests on Windows

The recommended contributor workflow is `build-tests-debug.ps1` from the KeePassXC repository root. It creates a separate `build-tests` Debug build and builds all three accessibility test targets.

Build only:

```powershell
.\build-tests-debug.ps1
```

Run the Qt accessibility suite:

```powershell
.\build-tests-debug.ps1 -RunAccessibilityTest
```

Run the basic Windows UIA suite:

```powershell
.\build-tests-debug.ps1 -RunWindowsAccessibilityTest
```

Run the Windows UIA tree suite:

```powershell
.\build-tests-debug.ps1 -RunWindowsAccessibilityTreeTest
```

### Run all accessibility suites

To clean the test build, build the application and all three accessibility test targets, and run all three suites with verbose QTest output:

```powershell
.\build-tests-debug.ps1 -Clean -RunAccessibilityTest -RunWindowsAccessibilityTest -RunWindowsAccessibilityTreeTest -VerboseTest
```

This is the recommended single command for validating a Windows accessibility change before pushing it.

Run a test with verbose QTest output by adding `-VerboseTest`.

If the test build has stale CMake or vcpkg paths, use `-Clean` to recreate the `build-tests` directory:

```powershell
.\build-tests-debug.ps1 -Clean -RunAccessibilityTest
```

## CI

The Windows workflow is `.github/workflows/accessibility-windows.yml`. It builds KeePassXC on a native Windows runner and runs all three accessibility suites.

The workflow deliberately does not contain a developer-specific `QT_PLUGIN_PATH`. Qt is installed by the workflow, so machine-specific local vcpkg paths must not be embedded in the test configuration.

## Platform scope

`QAccessible` tests are useful for guarding the common Qt accessibility tree, but they are not an end-to-end replacement for native platform testing.

- **Windows:** native UIA coverage is provided by `windows/`.
- **Linux:** native AT-SPI end-to-end coverage can be added separately when the CI architecture and test environment are established.
- **macOS:** the Qt-level tests provide common accessibility-tree regression coverage; native NSAccessibility coverage can be added separately if needed.

The Python tree-dump helpers in this directory are diagnostic tools for inspecting accessibility trees manually. They are not substitutes for the automated regression suites.

## Troubleshooting

### Windows UIA tests cannot find KeePassXC.exe

The Windows test targets embed the path to the built `KeePassXC.exe` target. The recommended PowerShell build script therefore builds the application automatically when either Windows UIA test is requested.

### Qt platform plugin problems

Make sure the required Qt installation and vcpkg dependencies are available. Avoid adding an absolute `QT_PLUGIN_PATH` pointing at a personal checkout; CI and other contributors will have different installation paths.

### Test passes locally but accessibility is wrong in a screen reader

The Qt tests and Windows UIA tests validate the accessibility tree at different boundaries. A real screen-reader regression can still require manual testing with supported assistive technology, including keyboard navigation and screen-reader interaction.

## Adding a regression test

When fixing an accessibility bug:

1. Add the smallest automated regression test that reproduces the problem.
2. Prefer the Qt/QAccessible layer when the behavior is platform-independent.
3. Add a Windows UIA test when the regression concerns the native Windows accessibility boundary.
4. Keep assertions semantic: verify names, roles, states, relationships, and keyboard behavior rather than brittle widget ordering when possible.
5. Run all three suites locally before pushing a Windows accessibility change.
