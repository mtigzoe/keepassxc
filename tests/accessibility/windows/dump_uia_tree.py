#!/usr/bin/env python3
"""
Target path in the repo: tests/accessibility/windows/dump_uia_tree.py

Diagnostic Windows UI Automation (UIA) tree dump for KeePassXC. This uses
Microsoft's UI Automation client API through pywinauto to inspect the native
Windows accessibility tree exposed by the real KeePassXC application.

Use this as a diagnostic/discovery tool when investigating Windows
accessibility or JAWS compatibility issues. It is not a replacement for the
C++ Windows UIA regression tests or manual JAWS 2026 testing.
"""

import sys
import time

try:
    from pywinauto import Desktop
except ImportError as exc:
    print(f"ERROR: pywinauto is not available: {exc}", file=sys.stderr)
    sys.exit(2)

APP_NAME = "KeePassXC"
CONNECT_TIMEOUT_S = 15
CONNECT_RETRY_INTERVAL_S = 0.5


def connect_to_app():
    """Poll for KeePassXC to appear in the Windows UIA desktop."""
    deadline = time.time() + CONNECT_TIMEOUT_S
    last_windows = []

    while time.time() < deadline:
        try:
            windows = Desktop(backend="uia").windows()
            last_windows = [window.window_text() or "(unnamed)" for window in windows]
            for window in windows:
                title = window.window_text() or ""
                if APP_NAME.lower() in title.lower():
                    return window
        except Exception:
            pass
        time.sleep(CONNECT_RETRY_INTERVAL_S)

    raise SystemExit(
        f"FAILED: '{APP_NAME}' never appeared in the Windows UIA desktop within "
        f"{CONNECT_TIMEOUT_S}s. Windows seen: {last_windows!r}\n"
        "Check that KeePassXC is running and that Windows UI Automation is "
        "available before assuming the widget code is at fault."
    )


def dump_node(node, out, depth=0):
    """Print one UIA node and recursively dump its descendants."""
    try:
        name = node.element_info.name or "(no accessible name)"
        control_type = node.element_info.control_type or "(no control type)"
        automation_id = node.element_info.automation_id or ""
        enabled = node.is_enabled()
        visible = node.is_visible()
        focusable = node.is_keyboard_focusable()
    except Exception as exc:
        line = f"{'  ' * depth}[node unavailable: {exc}]"
        print(line)
        out.write(line + "\n")
        return

    details = (
        f"[{control_type}] {name}"
        f"  -- enabled={enabled}, visible={visible}, focusable={focusable}"
    )
    if automation_id:
        details += f", automation_id={automation_id}"

    line = f"{'  ' * depth}{details}"
    print(line)
    out.write(line + "\n")

    try:
        children = node.children()
    except Exception as exc:
        line = f"{'  ' * (depth + 1)}[children unavailable: {exc}]"
        print(line)
        out.write(line + "\n")
        return

    for child in children:
        dump_node(child, out, depth + 1)


def main():
    app = connect_to_app()
    print(f"Connected to '{APP_NAME}' through Windows UI Automation. Walking tree...\n")

    with open("uia-tree-dump.txt", "w", encoding="utf-8") as out:
        out.write(f"Windows UI Automation tree for '{APP_NAME}'\n")
        out.write("=" * 60 + "\n")
        dump_node(app, out)

    print("\nUIA tree dump written to uia-tree-dump.txt")


if __name__ == "__main__":
    main()
