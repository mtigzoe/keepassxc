"""
Target path in the repo: tests/accessibility/windows/watch_progressbar_label.py

Dynamic companion to dump_uia_tree.py. That script takes one snapshot;
this one polls MainWindow's progress/status label (the clipboard-clear
countdown and sync/reload progress messages) repeatedly and logs every
time its UIA state actually changes, with a timestamp.

Why this exists: a single dump_uia_tree.py run can only prove what the
label's UIA properties are *at that instant*. It cannot show whether they
update live as the underlying message changes -- and per
MainWindow::updateProgressBar(), this label goes through several messages
a second during the clipboard-clear countdown. This script watches it
change in real time instead.

What this DOES prove: that the label's UIA-exposed Name is actually
changing live, over time, as KeePassXC updates it -- which is necessary
for JAWS to have anything to announce.

What this does NOT prove: that JAWS (or any AT client) actually receives
a notification for each change. This label raises QAccessible::ValueChanged
-> UiaRaiseNotificationEvent() (see the comment in
MainWindow::updateProgressBar() for why it's ValueChanged and not
NameChanged), which is a genuine push event, not just a property that
happens to be readable -- confirming it's actually *delivered* still needs
either a live JAWS session running alongside this script, or a real UIA
event subscription (AddAutomationEventHandler for
UIA_NotificationEventId), which is more test infrastructure than this repo
has today.

Previous version of this script used pywinauto's child_window(auto_id=...,
control_type=...) to find the label, and it never found it -- reporting
"not present" throughout, including once with a database open and an
entry selected, where a dump_uia_tree.py snapshot taken at the same moment
proved the element was really there
("[Text] Clearing the clipboard in 4 seconds... (QLabel, id=progressBarLabel)").
child_window() goes through pywinauto's separate find_elements() matching
path, which was never actually exercised or verified against this app --
unlike the plain recursive node.children() walk dump_uia_tree.py uses,
which has produced every correct dump in this project so far. This version
uses that same proven walk instead of child_window(), matching primarily
on automation_id (now printed by dump_uia_tree.py too, so it's directly
checkable) with a class-name/parent-based fallback in case that
assumption is ever wrong again -- logged clearly either way, rather than
failing silently.

Requirements:
    pip install pywinauto

Usage:
    1. Launch KeePassXC via build-debug.ps1, as usual.
    2. Start this script in a separate PowerShell:
         uv run tests\\accessibility\\windows\\watch_progressbar_label.py [duration_seconds]
    3. While it's running, trigger a real message -- e.g. select an entry
       and use "Copy Password" (starts the clipboard-clear countdown), or
       trigger a database sync/reload.
    4. Optionally have JAWS running at the same time and listen for
       whether it announces what this script logs.
"""

import sys
import time
from datetime import datetime

try:
    from dump_uia_tree import connect_to_app
except ImportError:
    print(
        "ERROR: could not import connect_to_app from dump_uia_tree.py -- "
        "run this from tests/accessibility/windows/, or make sure both "
        "files are in the same directory.",
        file=sys.stderr,
    )
    sys.exit(2)

TARGET_AUTOMATION_ID = "progressBarLabel"
ENTRY_COUNT_AUTOMATION_ID = "statusBarLabel"  # to explicitly exclude, not just skip by luck
TARGET_CLASS_NAME = "QLabel"
DEFAULT_DURATION_S = 30.0
POLL_INTERVAL_S = 0.15


def _walk(node):
    yield node
    try:
        children = node.children()
    except Exception:
        children = []
    for child in children:
        yield from _walk(child)


def find_progress_label(window):
    """Same recursive node.children() walk dump_uia_tree.py uses -- not
    child_window(), which is what silently failed to find this element
    before. Returns (element_or_None, method_str) so callers can tell
    whether the primary automation_id match worked or the fallback did."""
    nodes = list(_walk(window))

    for node in nodes:
        try:
            if node.element_info.automation_id == TARGET_AUTOMATION_ID:
                return node, "automation_id"
        except Exception:
            continue

    # Fallback, in case the automation_id assumption is ever wrong again:
    # a QLabel that is a direct child of the StatusBar and isn't the
    # entry-count label. Logged explicitly if this path is the one that
    # actually matches, since it means the primary assumption above needs
    # re-checking against a fresh dump_uia_tree.py run.
    for node in nodes:
        try:
            if node.element_info.class_name != TARGET_CLASS_NAME:
                continue
            if node.element_info.automation_id == ENTRY_COUNT_AUTOMATION_ID:
                continue
            parent_class = node.parent().element_info.class_name
            if parent_class == "QStatusBar":
                return node, "fallback (class_name + parent, NOT automation_id)"
        except Exception:
            continue

    return None, None


def read_label_state(window):
    """Fresh lookup every call -- deliberately not cached -- so this also
    catches the element disappearing entirely (Qt/UIA excludes an
    invisible QLabel from the tree entirely rather than exposing it with
    an empty/invisible state; confirmed empirically against this app's own
    dumps, where an invisible m_progressBarLabel never appeared at all).
    Returns (present, name, method)."""
    try:
        label, method = find_progress_label(window)
        if label is None:
            return False, None, None
        return True, label.element_info.name, method
    except Exception:
        return False, None, None


def main():
    duration = float(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_DURATION_S

    window = connect_to_app()
    print(f"Watching for '{TARGET_AUTOMATION_ID}' on '{window.window_text()}' for {duration:.0f}s.")
    print("Trigger a message now -- e.g. Copy Password to start the clipboard-clear countdown.\n")

    last_present, last_name, last_method = None, None, None
    start = time.monotonic()
    change_count = 0

    while time.monotonic() - start < duration:
        present, name, method = read_label_state(window)
        if present != last_present or name != last_name:
            ts = datetime.now().strftime("%H:%M:%S.%f")[:-3]
            if present:
                tag = "" if method == "automation_id" else f"  [via {method}]"
                print(f"[{ts}] present, name={name!r}{tag}")
            else:
                print(f"[{ts}] not present (hidden or not yet created)")
            last_present, last_name, last_method = present, name, method
            change_count += 1
        time.sleep(POLL_INTERVAL_S)

    print(f"\nDone. Logged {change_count} state change(s) over {duration:.0f}s.")
    if change_count == 0:
        print(
            "No changes seen at all -- either nothing was triggered during the "
            "window (no database open / no entry selected makes Copy Password a "
            "no-op), or something is still wrong with element lookup. Cross-check "
            "with a fresh dump_uia_tree.py run taken at the same moment -- it now "
            "prints each element's automation_id directly, so if it shows the "
            "label under a different id than 'progressBarLabel', that's the next "
            "thing to fix here."
        )
    return 0


if __name__ == "__main__":
    sys.exit(main())
