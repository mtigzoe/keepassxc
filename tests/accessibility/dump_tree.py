#!/usr/bin/env python3
"""
Target path in the repo: tests/accessibility/dump_tree.py

Diagnostic (not yet assertions) script for exercising KeePassXC's
AT-SPI accessible tree the same way Orca / a screen reader would.

What this does:
  1. Confirms the app actually registered on the AT-SPI bus at all --
     if QT_LINUX_ACCESSIBILITY_ALWAYS_ON stops being honored on some
     future Qt6 build, this fails loudly here instead of silently
     testing nothing.
  2. Walks the full accessible tree and prints, for every node: role,
     name, and the state flags most relevant to screen-reader use
     (showing / sensitive / focusable / focused).
  3. Writes the same output to atspi-tree-dump.txt so it can be
     uploaded as a CI artifact and read without digging through logs.

This intentionally does NOT assert anything about *specific* widget
names yet -- run it once, read atspi-tree-dump.txt, then turn the
names/roles you actually care about into real assertions using the
template at the bottom.

CAVEAT: focus-based assertions (is X the focused node right now) ride
the same Xvfb+fluxbox display stack as `testgui --gui`, which has a
documented history (see run-tests.sh) of not reliably delivering focus
events in a throwaway CI session. Treat a focus-assertion failure here
as "investigate", not "confirmed regression", until this job has
proven stable over several runs.
"""

import sys
import time

try:
    from dogtail.tree import root, SearchError
    from dogtail.config import config
except ImportError:
    print("ERROR: dogtail not importable. Is python3-dogtail installed?", file=sys.stderr)
    sys.exit(2)

# Keep dogtail quiet/fast -- we don't need its own logging or the
# visual highlight-on-action feature for a CI run.
config.logDebugToFile = False
config.blinkOnActions = False

# AT-SPI application names are usually the process name, but this can
# vary -- if connect_to_app() times out, check atspi-tree-dump.txt's
# absence and try "KeePassXC" (capitalized) or inspect `busctl --user
# tree org.a11y.Bus` from a manual run to see what actually registered.
APP_NAME = "keepassxc"
CONNECT_TIMEOUT_S = 15
CONNECT_RETRY_INTERVAL_S = 0.5


def connect_to_app():
    """Poll for the app to appear on the AT-SPI bus rather than doing a
    single immediate lookup -- registration can lag a moment after the
    process starts."""
    deadline = time.time() + CONNECT_TIMEOUT_S
    last_err = None
    while time.time() < deadline:
        try:
            return root.application(APP_NAME)
        except SearchError as e:
            last_err = e
            time.sleep(CONNECT_RETRY_INTERVAL_S)
    raise SystemExit(
        f"FAILED: '{APP_NAME}' never appeared on the AT-SPI bus within "
        f"{CONNECT_TIMEOUT_S}s ({last_err}).\n"
        "This means either the app didn't launch, or it isn't "
        "registering with AT-SPI at all -- check that "
        "QT_LINUX_ACCESSIBILITY_ALWAYS_ON=1 is actually taking effect "
        "on this Qt6 build before assuming the widget code is at fault."
    )


def dump_node(node, out, depth=0):
    name = node.name or "(no accessible name)"
    role = node.roleName or "(no role)"

    flags = []
    for attr in ("showing", "sensitive", "focusable", "focused"):
        try:
            if getattr(node, attr):
                flags.append(attr)
        except Exception:
            pass
    flag_str = ", ".join(flags) if flags else "(no notable state)"

    line = f"{'  ' * depth}[{role}] {name}  -- {flag_str}"
    print(line)
    out.write(line + "\n")

    for child in node.children:
        dump_node(child, out, depth + 1)


def main():
    app = connect_to_app()
    print(f"Connected to '{APP_NAME}' on the AT-SPI bus. Walking tree...\n")

    with open("atspi-tree-dump.txt", "w") as out:
        out.write(f"AT-SPI accessible tree for '{APP_NAME}'\n")
        out.write("=" * 60 + "\n")
        dump_node(app, out)

    print("\nTree dump written to atspi-tree-dump.txt")


# --- Template for a real focus-traversal assertion, once the tree ---
# --- dump above confirms the widget names you expect. Not run by  ---
# --- default -- copy into its own test once you've validated it.  ---
def _example_focus_check(app):
    from dogtail.rawinput import pressKey

    first_field = app.findChild(lambda n: n.name == "Database Name")
    first_field.grabFocus()
    pressKey("Tab")

    focused = app.findChild(lambda n: n.focused, retry=False)
    expected_next = "Database Description"
    assert focused and focused.name == expected_next, (
        f"expected focus on '{expected_next}' after Tab, "
        f"got '{focused.name if focused else None}'"
    )


if __name__ == "__main__":
    main()
