"""
Target path in the repo: tests/accessibility/windows/dump_uia_tree.py
(pairs with tests/accessibility/dump_tree.py, the Linux/AT-SPI version)

Diagnostic (not yet assertions) script for exercising KeePassXC's
accessible tree on Windows via UI Automation (UIA) -- the same bridge
JAWS reads from -- using pywinauto instead of raw COM.

Unlike dump_tree.py, this script does NOT launch the app itself. Keep
using build-debug.ps1 exactly as you do now; this connects to whatever
KeePassXC window is already running, by title, regardless of how it
was launched or whether a debugger is attached.

What this does:
  1. Polls for a KeePassXC window to appear (connect(), retried --
     the window can take a moment to render after launch).
  2. Walks the full UIA tree and prints, for every element: control
     type (role), accessible name, and the state flags most relevant
     to screen-reader use (visible / enabled / keyboard-focusable /
     has keyboard focus).
  3. Writes the same output to uia-tree-dump.txt.

This intentionally does NOT assert anything about *specific* widget
names yet -- run it once, read uia-tree-dump.txt, then turn the names/
roles you actually care about into real assertions using the template
at the bottom.

Requirements:
    pip install pywinauto

Usage:
    1. Launch KeePassXC via build-debug.ps1, as usual.
    2. Once the window is up, in a separate PowerShell:
         python dump_uia_tree.py
"""

import ctypes
from ctypes import wintypes
import sys
import time

try:
    from pywinauto import Desktop
except ImportError:
    print("ERROR: pywinauto not importable. Run: pip install pywinauto", file=sys.stderr)
    sys.exit(2)

WINDOW_TITLE_RE = ".*KeePassXC.*"
EXPECTED_PROCESS_NAME = "keepassxc.exe"
CONNECT_TIMEOUT_S = 15
CONNECT_RETRY_INTERVAL_S = 0.5


def _process_name_for_pid(pid):
    """Return the executable name (e.g. 'keepassxc.exe') owning `pid`,
    or None if it can't be determined. Uses ctypes directly instead of
    adding a psutil dependency -- pywinauto on Windows already implies
    pywin32/ctypes are available."""
    PROCESS_QUERY_LIMITED_INFORMATION = 0x1000
    handle = ctypes.windll.kernel32.OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, False, pid)
    if not handle:
        return None
    try:
        buf = ctypes.create_unicode_buffer(260)
        size = wintypes.DWORD(260)
        ok = ctypes.windll.kernel32.QueryFullProcessImageNameW(handle, 0, buf, ctypes.byref(size))
        if not ok:
            return None
        return buf.value.rsplit("\\", 1)[-1]
    finally:
        ctypes.windll.kernel32.CloseHandle(handle)

STATE_METHODS = (
    ("is_visible", "visible"),
    ("is_enabled", "enabled"),
    ("is_keyboard_focusable", "focusable"),
    ("has_keyboard_focus", "focused"),
)


def connect_to_app():
    """Poll for the window rather than a single immediate lookup --
    build-debug.ps1 launching the app doesn't mean the window has
    finished rendering yet. Requires both a title_re match AND a
    process name match (see EXPECTED_PROCESS_NAME above); raises
    immediately, without retrying, if more than one window matches
    both filters, since guessing wrong is worse than failing loudly."""
    deadline = time.time() + CONNECT_TIMEOUT_S
    last_err = None
    while time.time() < deadline:
        try:
            candidates = Desktop(backend="uia").windows(title_re=WINDOW_TITLE_RE)
        except Exception as e:
            last_err = e
            time.sleep(CONNECT_RETRY_INTERVAL_S)
            continue

        matches = [w for w in candidates if (_process_name_for_pid(w.process_id()) or "").lower() == EXPECTED_PROCESS_NAME]

        if len(matches) == 1:
            return matches[0]
        if len(matches) > 1:
            titles = ", ".join(repr(w.window_text()) for w in matches)
            raise SystemExit(
                f"FAILED: {len(matches)} windows matched both the title regex "
                f"and process '{EXPECTED_PROCESS_NAME}' ({titles}) -- refusing "
                "to guess which one to walk. Close extra KeePassXC windows/"
                "instances and retry."
            )

        last_err = f"{len(candidates)} window(s) matched the title, none from '{EXPECTED_PROCESS_NAME}'"
        time.sleep(CONNECT_RETRY_INTERVAL_S)

    raise SystemExit(
        f"FAILED: no window matching '{WINDOW_TITLE_RE}' AND belonging to "
        f"'{EXPECTED_PROCESS_NAME}' appeared within {CONNECT_TIMEOUT_S}s "
        f"({last_err}).\n"
        "Make sure KeePassXC is already running (via build-debug.ps1) "
        "before starting this script, and check the actual window "
        "title and process name if they don't match the defaults."
    )


def dump_node(node, out, depth=0):
    try:
        name = node.element_info.name or "(no accessible name)"
    except Exception:
        name = "(name unavailable)"
    try:
        role = node.element_info.control_type or "(no control type)"
    except Exception:
        role = "(control type unavailable)"
    try:
        # The underlying Qt widget class (e.g. "QToolButton", "QCheckBox").
        # Qt's accessibility bridge exposes this as the UIA ClassName
        # property. For an unnamed/unclear element, this is often the
        # fastest way to identify exactly which widget it is without
        # round-tripping through a source-code guess -- e.g. distinguishing
        # a real QCheckBox from a checkable QToolButton (Qt reports both as
        # ControlType CheckBox) that some other code merely forgot to name.
        class_name = node.element_info.class_name or "(no class name)"
    except Exception:
        class_name = "(class name unavailable)"

    flags = []
    for attr, label in STATE_METHODS:
        try:
            if getattr(node, attr)():
                flags.append(label)
        except Exception:
            pass
    flag_str = ", ".join(flags) if flags else "(no notable state)"

    line = f"{'  ' * depth}[{role}] {name}  ({class_name})  -- {flag_str}"
    print(line)
    out.write(line + "\n")

    try:
        children = node.children()
    except Exception:
        children = []
    for child in children:
        dump_node(child, out, depth + 1)


def main():
    window = connect_to_app()
    print(f"Connected to '{EXPECTED_PROCESS_NAME}' window matching '{WINDOW_TITLE_RE}'. Walking tree...\n")

    with open("uia-tree-dump.txt", "w", encoding="utf-8") as out:
        out.write(f"UIA accessible tree for window matching '{WINDOW_TITLE_RE}'\n")
        out.write("=" * 60 + "\n")
        dump_node(window, out)

    print("\nTree dump written to uia-tree-dump.txt")


def _example_focus_check(window):
    from pywinauto.keyboard import send_keys

    first_field = window.descendants(title="Database Name", control_type="Edit")[0]
    first_field.set_focus()
    send_keys("{TAB}")

    focused = None
    for child in window.descendants():
        try:
            if child.has_keyboard_focus():
                focused = child
                break
        except Exception:
            continue

    expected_next = "Database Description"
    actual = focused.element_info.name if focused else None
    assert actual == expected_next, (
        f"expected focus on '{expected_next}' after Tab, got '{actual}'"
    )


if __name__ == "__main__":
    main()
