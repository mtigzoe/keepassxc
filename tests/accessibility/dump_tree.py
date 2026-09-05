#!/usr/bin/env python3
"""
Target path in the repo: tests/accessibility/dump_tree.py

Diagnostic accessibility-tree dump for CI. This uses the modern GObject
introspection binding for libatspi directly instead of legacy dogtail/pyatspi.
That avoids an extra discovery layer in a headless CI session while exercising
the same AT-SPI bus used by screen readers such as Orca.

What this does:
  1. Confirms the app appears on the AT-SPI desktop.
  2. Walks the full accessible tree and prints role, name, and state flags.
  3. Writes the same output to atspi-tree-dump.txt for CI artifacts.

This intentionally does NOT assert specific widget names yet. Once the tree
is stable in CI, the names/roles that matter can be turned into assertions.
"""

import sys
import time

try:
    import gi
    gi.require_version("Atspi", "2.0")
    from gi.repository import Atspi
except (ImportError, ValueError) as exc:
    print(f"ERROR: GObject AT-SPI binding is not available: {exc}", file=sys.stderr)
    sys.exit(2)

APP_NAME = "keepassxc"
CONNECT_TIMEOUT_S = 15
CONNECT_RETRY_INTERVAL_S = 0.5


def get_applications():
    """Return the applications currently exposed by the AT-SPI desktop."""
    desktop = Atspi.get_desktop(0)
    if desktop is None:
        return []
    return list(desktop)


def connect_to_app():
    """Poll for KeePassXC to appear on the AT-SPI desktop."""
    deadline = time.time() + CONNECT_TIMEOUT_S
    last_names = []

    while time.time() < deadline:
        applications = get_applications()
        last_names = [app.get_name() or "(unnamed)" for app in applications]
        for app in applications:
            name = app.get_name() or ""
            if name.lower() == APP_NAME.lower():
                return app
        time.sleep(CONNECT_RETRY_INTERVAL_S)

    raise SystemExit(
        f"FAILED: '{APP_NAME}' never appeared on the AT-SPI desktop within "
        f"{CONNECT_TIMEOUT_S}s. Applications seen: {last_names!r}\n"
        "Check AT_SPI_BUS_ADDRESS, the AT-SPI registry, and the Qt "
        "accessibility environment before assuming the widget code is at fault."
    )


def state_flags(node):
    """Return the state flags most relevant to screen-reader use."""
    flags = []
    state_set = node.get_state_set()
    for state, label in (
        (Atspi.StateType.SHOWING, "showing"),
        (Atspi.StateType.SENSITIVE, "sensitive"),
        (Atspi.StateType.FOCUSABLE, "focusable"),
        (Atspi.StateType.FOCUSED, "focused"),
    ):
        try:
            if state_set.contains(state):
                flags.append(label)
        except Exception:
            pass
    return ", ".join(flags) if flags else "(no notable state)"


def dump_node(node, out, depth=0):
    name = node.get_name() or "(no accessible name)"
    role = node.get_role_name() or "(no role)"
    line = f"{'  ' * depth}[{role}] {name}  -- {state_flags(node)}"
    print(line)
    out.write(line + "\n")

    try:
        children = list(node)
    except Exception as exc:
        print(f"{'  ' * (depth + 1)}[children unavailable: {exc}]")
        out.write(f"{'  ' * (depth + 1)}[children unavailable: {exc}]\n")
        return

    for child in children:
        dump_node(child, out, depth + 1)


def main():
    try:
        Atspi.init()
        app = connect_to_app()
        print(f"Connected to '{APP_NAME}' on the AT-SPI bus. Walking tree...\n")

        with open("atspi-tree-dump.txt", "w", encoding="utf-8") as out:
            out.write(f"AT-SPI accessible tree for '{APP_NAME}'\n")
            out.write("=" * 60 + "\n")
            dump_node(app, out)

        print("\nTree dump written to atspi-tree-dump.txt")
    finally:
        try:
            Atspi.exit()
        except Exception:
            pass


if __name__ == "__main__":
    main()
