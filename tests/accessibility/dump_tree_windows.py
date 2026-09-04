"""
Target path in the repo: tests/accessibility/dump_tree_windows.py
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
         python dump_tree_windows.py
"""

import sys
import time

try:
    from pywinauto import Application
except ImportError:
    print("ERROR: pywinauto not importable. Run: pip install pywinauto", file=sys.stderr)
    sys.exit(2)

# Adjust if your build's window title doesn't match -- it may include
# a filename or version string. title_re is a regex, so
# ".*KeePassXC.*" matches most variants.
WINDOW_TITLE_RE = ".*KeePassXC.*"
CONNECT_TIMEOUT_S = 15
CONNECT_RETRY_INTERVAL_S = 0.5

# Some of these convenience methods vary slightly by pywinauto version.
# Each is wrapped individually in dump_node() below, so a missing one
# is silently skipped rather than crashing the whole walk.
STATE_METHODS = (
    ("is_visible", "visible"),
    ("is_enabled", "enabled"),
    ("is_keyboard_focusable", "focusable"),
    ("has_keyboard_focus", "focused"),
)


def connect_to_app():
    """Poll for the window rather than a single immediate lookup --
    build-debug.ps1 launching the app doesn't mean the window has
    finished rendering yet."""
    deadline = time.time() + CONNECT_TIMEOUT_S
    last_err = None
    while time.time() < deadline:
        try:
            app = Application(backend="uia").connect(title_re=WINDOW_TITLE_RE)
            return app.top_window()
        except Exception as e:
            last_err = e
            time.sleep(CONNECT_RETRY_INTERVAL_S)
    raise SystemExit(
        f"FAILED: no window matching '{WINDOW_TITLE_RE}' appeared within "
        f"{CONNECT_TIMEOUT_S}s ({last_err}).\n"
        "Make sure KeePassXC is already running (via build-debug.ps1) "
        "before starting this script, and check the actual window "
        "title if it doesn't match the default regex."
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

    flags = []
    for attr, label in STATE_METHODS:
        try:
            if getattr(node, attr)():
                flags.append(label)
        except Exception:
            pass
    flag_str = ", ".join(flags) if flags else "(no notable state)"

    line = f"{'  ' * depth}[{role}] {name}  -- {flag_str}"
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
    print(f"Connected to window matching '{WINDOW_TITLE_RE}'. Walking tree...\n")

    with open("uia-tree-dump.txt", "w", encoding="utf-8") as out:
        out.write(f"UIA accessible tree for window matching '{WINDOW_TITLE_RE}'\n")
        out.write("=" * 60 + "\n")
        dump_node(window, out)

    print("\nTree dump written to uia-tree-dump.txt")


# --- Template for a real focus-traversal assertion, once the tree ---
# --- dump above confirms the widget names you expect. Not run by  ---
# --- default -- copy into its own test once you've validated it.  ---
def _example_focus_check(window):
    from pywinauto.keyboard import send_keys

    first_field = window.child_window(title="Database Name", control_type="Edit")
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
