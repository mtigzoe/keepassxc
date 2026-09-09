# Windows UI Automation accessibility testing

KeePassXC uses Qt's `QAccessible` interfaces to expose accessibility information. On Windows, the application must also expose that information correctly through the native Windows UI Automation (UIA) layer consumed by assistive technologies.

Microsoft recommends using Accessibility Insights for Windows during development to inspect the live UI Automation tree, properties, control patterns, and focus behavior. The existing CI tests complement this manual inspection by exercising the native UIA boundary programmatically.

## Automated coverage

The Windows accessibility workflow runs two complementary suites:

1. `testaccessibility` — validates the Qt accessibility tree in-process.
2. `testwindowsaccessibility` — launches the real `KeePassXC.exe` and queries it through the Windows UI Automation COM API.
3. `testwindowsaccessibilitytree` — validates the UIA control view, accessible names for interactive controls, and required control patterns on core welcome-screen controls.

Run the Windows-specific tests locally from a configured Windows build directory with:

```powershell
ctest --test-dir build-debug -C Debug -R '^testwindowsaccessibility' --output-on-failure
```

## Accessibility Insights for Windows

Install and run Accessibility Insights for Windows on the same Windows machine as the KeePassXC build.

Use **Live Inspect** to inspect KeePassXC while moving keyboard focus through the application. For important controls, verify at least:

- Name
- Control Type
- IsEnabled
- IsOffscreen
- HasKeyboardFocus / focus transitions
- Control Patterns
- Parent/child navigation in the UI Automation tree

Use **Tab Stops** to verify that keyboard-focusable controls appear in a logical order and that visible controls do not disappear from the accessibility tree when they receive focus.

Use **Event**/troubleshooting features when investigating dynamic behavior such as opening dialogs, changing selection, expanding the database tree, or changing a control's state.

## Core Windows scenarios

The following scenarios should be inspected manually after changes to the GUI or accessibility implementation:

- Welcome screen
- Create database dialog
- Open database dialog
- Main database window
- Database/group tree
- Entry list
- Entry preview
- Entry editor
- Password generator
- Application settings
- Database settings
- Confirmation dialogs
- Error and warning dialogs
- Lock/unlock workflow

For each scenario, verify keyboard navigation first and then inspect the corresponding UIA representation.

## Screen-reader verification

UI Automation inspection is necessary but does not replace assistive-technology testing. Microsoft recommends verification with users and assistive technologies where possible.

The Windows manual regression pass should include both NVDA and JAWS and should verify:

- focus is announced when moving between controls;
- control names and roles are correct;
- state changes are announced;
- dialogs receive focus correctly;
- focus returns to the expected control after a dialog closes;
- menus and menu items are navigable from the keyboard;
- tree and list navigation works with arrow keys;
- editable fields expose their labels and values;
- buttons expose an actionable button role;
- password visibility changes are communicated appropriately;
- validation and error messages are announced;
- no keyboard-only workflow requires a mouse.

Passing the automated Qt and UIA tests should therefore be treated as a regression baseline, not as proof that every screen-reader workflow is fully compatible.
