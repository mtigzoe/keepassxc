# KeePassXC JAWS 2026 Compatibility Matrix

This is the manual regression checklist for validating KeePassXC with JAWS
2026 on Windows. It complements, and does not replace, the automated Windows
UI Automation suite in `tests/accessibility/windows/` (`testaccessibility`,
`testwindowsaccessibility`, `testwindowsaccessibilitytree`). Where a row below
is already covered by an automated test, it's noted — run through it anyway,
since JAWS surfaces things (verbosity, Braille output, virtual cursor
behavior) that UIA alone cannot verify.

## How to use this

1. Build the Windows Debug configuration and launch the resulting
   `KeePassXC.exe` with JAWS 2026 running.
2. Work through each workflow below in order.
3. For every numbered check, record `PASS` / `FAIL` / `PARTIAL` and a note
   (what JAWS actually said, or what it failed to announce).
4. Any `FAIL` or `PARTIAL` becomes a UIA investigation: find the underlying
   `Name`/`ControlType`/`state` problem, fix KeePassXC, and add a regression
   test to `testwindowsaccessibility` (or `testwindowsaccessibilitytree`) so
   CI catches it going forward. Only file it as a JAWS-specific note if it
   turns out not to be representable through UIA at all (see the bottom of
   this document).

---

## 1. Main window

**Automated coverage:** `testMainWindowIdentity` (name, ControlType).

| # | Check | Result | Notes |
|---|-------|--------|-------|
| 1 | Launch KeePassXC; JAWS announces the window as "KeePassXC" | | |
| 2 | Tab order on first launch is logical (toolbar → welcome screen → status bar) | | |
| 3 | JAWS layer/virtual cursor reads the window without dead zones | | |

## 2. Database creation

**Automated coverage:** `testWelcomeScreenButtonsAccessible` /
`...TreeRelationships` cover the "Create Database", "Open Database", "Import
File" buttons on the welcome screen only — not yet the wizard itself.

| # | Check | Result | Notes |
|---|-------|--------|-------|
| 1 | "Create Database" button is announced by name and role (Button) | | |
| 2 | Activating it opens the New Database wizard; JAWS announces the new dialog | | |
| 3 | Database name / description fields are announced with their labels | | |
| 4 | Password + confirm-password fields on the key-setup page are announced as password fields | | |
| 5 | "Next" / "Back" / "Create" / "Done" buttons are announced correctly at each page | | |
| 6 | Focus lands somewhere sensible after the wizard closes (main window / database view) | | |

## 3. Database unlock

**Automated coverage:** none yet — proposed next increment.

Real control names to check against (from `DatabaseOpenWidget.ui`): dialog
accessible name **"Unlock KeePassXC Database"**, password field accessible
name **"Password field"**, key-file controls **"Key file to unlock the
database"** / **"Browse for key file"**, hardware-key controls **"Hardware
key slot selection"** / **"Refresh Hardware Keys"**, quick-unlock button
**"Unlock Database"**.

| # | Check | Result | Notes |
|---|-------|--------|-------|
| 1 | Opening a `.kdbx` file announces the "Unlock KeePassXC Database" dialog | | |
| 2 | The password field is announced as "Password field" and as a password/protected edit | | |
| 3 | Tab reaches the key-file and hardware-key controls in a sensible order | | |
| 4 | "Unlock Database" / quick-unlock button is announced correctly | | |
| 5 | A wrong password produces an announced error (see §11) rather than silent failure | | |
| 6 | On success, focus moves into the unlocked database (group tree or entry list) | | |

## 4. Database tree

**Automated coverage:** none yet. Real accessible names: group tree is
**"Groups"**, entry list is **"Entries"** (`DatabaseWidget.cpp`).

| # | Check | Result | Notes |
|---|-------|--------|-------|
| 1 | The group tree is announced as "Groups" and as a Tree | | |
| 2 | Arrowing through groups announces each group's name and nesting level | | |
| 3 | Expand/collapse state (expanded/collapsed) is announced on groups with children | | |
| 4 | Selecting a group updates and announces the entry list contents | | |

## 5. Entry list

**Automated coverage:** none yet. Real accessible name: **"Entries"**.

| # | Check | Result | Notes |
|---|-------|--------|-------|
| 1 | The entry list is announced as "Entries" and as a List/Table | | |
| 2 | Arrowing through entries announces title (and configured columns, if JAWS reads column headers) | | |
| 3 | Selecting an entry updates the preview/detail pane and JAWS announces the change | | |
| 4 | Sorting by a column header is announced (ascending/descending) | | |

## 6. Entry editor

**Automated coverage:** none yet.

| # | Check | Result | Notes |
|---|-------|--------|-------|
| 1 | Opening an entry announces the edit dialog and its title | | |
| 2 | Title, username, password, URL, notes fields are each announced with their label | | |
| 3 | The password field's reveal/hide toggle announces its state | | |
| 4 | The password generator button/dialog is announced and usable from JAWS | | |
| 5 | Tags input (custom widget) announces added/removed tags | | |
| 6 | Attachments list is announced with file names | | |
| 7 | "OK" / "Cancel" / "Apply" are announced correctly | | |

## 7. Search

**Automated coverage:** none yet. Real accessible names (`SearchWidget.cpp`):
search field is **"Search"**, clear button is **"Clear Search"**.

| # | Check | Result | Notes |
|---|-------|--------|-------|
| 1 | The search field is announced as "Search" (not just "edit") | | |
| 2 | Typing announces result-count feedback if/when KeePassXC surfaces it | | |
| 3 | "Clear Search" button is announced and restores the untruncated entry list | | |
| 4 | Pressing Enter moves focus into results in a sensible order | | |

## 8. Settings

**Automated coverage:** none yet.

| # | Check | Result | Notes |
|---|-------|--------|-------|
| 1 | Application Settings dialog announces its category list (General / Security / Access / Browser Integration / etc.) | | |
| 2 | Selecting a category announces the panel change | | |
| 3 | Checkboxes, combo boxes, and spin boxes in each panel announce label + current value/state | | |
| 4 | Database Settings dialog (per-database) announces its own category list separately from Application Settings | | |
| 5 | Encryption settings (rounds/memory) announce their current values and any validation warnings | | |

## 9. Menus

**Automated coverage:** none yet.

| # | Check | Result | Notes |
|---|-------|--------|-------|
| 1 | Menu bar items are announced by name and as Menu Items | | |
| 2 | Submenus announce that they have a submenu before expanding | | |
| 3 | Checkable/toggle menu items announce their checked state | | |
| 4 | Keyboard shortcuts are announced where shown | | |
| 5 | Disabled menu items (e.g. Lock Database with nothing open) announce as disabled | | |

## 10. Dialogs (general)

**Automated coverage:** none yet. This row is for dialogs not already broken
out above (e.g. Merge Database, Master Key change, Break YubiKey association,
CSV import wizard).

| # | Check | Result | Notes |
|---|-------|--------|-------|
| 1 | Opening any modal dialog moves JAWS focus into it immediately | | |
| 2 | The dialog's title is announced | | |
| 3 | Tab order stays inside the dialog (no escape into the main window) until closed | | |
| 4 | Closing the dialog returns focus to where it was invoked from | | |

## 11. Notifications

**Automated coverage:** none yet. Covers the tray-icon/toast style
notifications KeePassXC raises (clipboard-clear countdown, auto-save,
database-changed-externally, etc.).

| # | Check | Result | Notes |
|---|-------|--------|-------|
| 1 | Transient notifications are announced without requiring focus to move | | |
| 2 | Time-sensitive ones (e.g. clipboard clearing in N seconds) are announced with enough lead time to act | | |
| 3 | Notifications don't interrupt/garble whatever JAWS was already reading | | |

## 12. Error messages

**Automated coverage:** none yet.

| # | Check | Result | Notes |
|---|-------|--------|-------|
| 1 | A wrong master password on unlock is announced as an error, not silent failure | | |
| 2 | "Database Version Mismatch" and similar message boxes are announced with their full text | | |
| 3 | Inline validation errors (e.g. "Number of rounds too high/low" in Encryption settings) are announced at the point of entry, not just on OK | | |
| 4 | Message box buttons (OK/Yes/No/Cancel) are announced correctly | | |

## 13. Lock/unlock

**Automated coverage:** none yet. Menu action is **"Lock Database"**
(`actionLockDatabase` in `MainWindow.cpp`).

| # | Check | Result | Notes |
|---|-------|--------|-------|
| 1 | "Lock Database" toolbar button and menu item are announced correctly | | |
| 2 | It's announced as disabled when there's nothing unlocked to lock | | |
| 3 | Locking announces the transition back to the unlock screen | | |
| 4 | Quick-unlock (re-entering just enough to unlock a locked-in-place database) is announced distinctly from a full unlock | | |

## 14. Reports

**Automated coverage:** none yet. Covers `ReportsDialog`'s pages: Statistics,
Health Check, Passkeys, Browser Statistics, Have I Been Pwned.

| # | Check | Result | Notes |
|---|-------|--------|-------|
| 1 | The Reports dialog announces its page list (Statistics / Health Check / Passkeys / Browser Statistics / HIBP) | | |
| 2 | Switching pages announces the change and reads the new page's heading | | |
| 3 | Health Check's findings list announces each flagged entry with its issue (weak/reused/old password, etc.) | | |
| 4 | Any table/list in these pages is navigable with JAWS's table navigation commands, not just arrow keys | | |

---

## JAWS-specific findings (not representable via UIA)

Use this section for anything that survives investigation as genuinely
JAWS-specific rather than a KeePassXC/UIA bug — verbosity settings, JAWS
navigation-quick-key behavior, Braille display line wrapping/abbreviation
choices, or JAWS's own interpretation of a UIA event. These stay in this
document rather than becoming an automated test.

| Screen | Finding | JAWS setting/version | Notes |
|--------|---------|----------------------|-------|
| | | | |
