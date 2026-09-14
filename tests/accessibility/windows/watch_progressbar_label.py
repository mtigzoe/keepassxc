"""
Target path in the repo: tests/accessibility/windows/watch_progressbar_label.py

Dynamic companion to dump_uia_tree.py. That script takes one snapshot;
this one polls MainWindow's progress/status label (UIA AutomationId
"progressBarLabel" -- the clipboard-clear countdown and sync/reload
progress messages) repeatedly and logs every time its UIA state actually
changes, with a timestamp.

Why this exists: a single dump_uia_tree.py run can only prove what the
label's UIA properties are *at that instant*. It cannot show whether they
update live as the underlying message changes -- and per
MainWindow::updateProgressBar(), this label goes through several messages
a second during the clipboard-clear countdown. This script watches it
change in real time instead.

What this DOES prove: that the label's UIA-exposed Name/visibility is
actually changing live, over time, as KeePassXC updates it -- which is
necessary for JAWS to have anything to announce.

What this does NOT prove: that JAWS (or any AT client) actually receives
a notification for each change. UIA property changes can update silently
if the app never raises the corresponding event (this label specifically
raises QAccessible::ValueChanged -> UiaRaiseNotificationEvent(), not a
NameChanged property-changed event -- see the comment in
MainWindow::updateProgressBar() for why). Confirming real delivery needs
either a live JAWS session running alongside this script (watch for the
two to line up), or a genuine UIA event subscription
(AddAutomationEventHandler for UIA_NotificationEventId), which is more
test infrastructure than this repo has today.

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

AUTOMATION_ID = "progressBarLabel"
CONTROL_TYPE = "Text"
DEFAULT_DURATION_S = 30.0
POLL_INTERVAL_S = 0.15


def read_label_state(window):
    """Fresh lookup every call -- deliberately not cached -- so this
    also catches the element disappearing entirely (Qt/UIA excludes an
    invisible QLabel from the tree entirely rather than exposing it with
    an empty/invisible state; confirmed empirically against this app's
    own dumps, where an invisible m_progressBarLabel never appeared at
    all). Returns (present, name) -- present=False covers both "hidden"
    and "not found yet"."""
    try:
        label = window.child_window(auto_id=AUTOMATION_ID, control_type=CONTROL_TYPE)
        name = label.element_info.name
        return True, name
    except Exception:
        return False, None


def main():
    duration = float(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_DURATION_S

    window = connect_to_app()
    print(f"Watching '{AUTOMATION_ID}' on '{window.window_text()}' for {duration:.0f}s.")
    print("Trigger a message now -- e.g. Copy Password to start the clipboard-clear countdown.\n")

    last_present, last_name = None, None
    start = time.monotonic()
    change_count = 0

    while time.monotonic() - start < duration:
        present, name = read_label_state(window)
        if present != last_present or name != last_name:
            ts = datetime.now().strftime("%H:%M:%S.%f")[:-3]
            if present:
                print(f"[{ts}] present, name={name!r}")
            else:
                print(f"[{ts}] not present (hidden or not yet created)")
            last_present, last_name = present, name
            change_count += 1
        time.sleep(POLL_INTERVAL_S)

    print(f"\nDone. Logged {change_count} state change(s) over {duration:.0f}s.")
    if change_count == 0:
        print(
            "No changes seen at all -- either nothing was triggered during the "
            "window, or the label isn't updating live. Re-run and trigger a "
            "message immediately, or check with dump_uia_tree.py right after "
            "triggering one as a sanity check."
        )
    return 0


if __name__ == "__main__":
    sys.exit(main())
