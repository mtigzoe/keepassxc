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
control_type=...) to find the label, and it never found it. Root cause
turned out to be the lookup method (child_window() was never exercised
against this app), not the application -- confirmed once this script was
switched to the same recursive node.children() walk dump_uia_tree.py uses,
which found the label reliably via a class-name/parent fallback. But that
fallback run also showed the primary automation_id == "progressBarLabel"
match never fired, meaning the real live AutomationId for this element is
still unconfirmed -- a source-level reading of Qt's
automationIdForAccessible() predicts "progressBarLabel", but prediction
isn't the same as measurement.

STATUS-BAR CHILDREN DIAGNOSTIC (added to answer that directly): every time
a state change is detected, this also re-enumerates the QStatusBar's
direct UIA children and prints each one's raw control type, class name,
Name, AutomationId, and visibility/enabled state -- specifically bypassing
pywinauto's automation_id convenience property, which silently converts a
COMError into None (see uia_element_info.py: "except COMError: return
None"). That makes "the property is genuinely empty" indistinguishable
from "the call itself failed", which is exactly the ambiguity we need
resolved. This calls the raw COM property (element_info.element.
CurrentAutomationId) directly and reports a COMError as
"id_status=COMError: ...", never silently as an empty id. This does NOT
assume which child is which -- it prints all of them (normally
statusBarLabel, m_progressBarLabel, and the QProgressBar) so the actual
identifying properties can be read directly off the running process.

Requirements:
    pip install pywinauto

Usage:
    1. Launch KeePassXC via build-debug.ps1, as usual.
    2. Unlock a database and select an entry first -- Copy Password is a
       no-op with nothing selected.
    3. Trigger Copy Password, THEN immediately start this script (the
       countdown is only ~10s, and dump_uia_tree.py-style startup/connect
       overhead can eat a meaningful chunk of that if you start first):
         uv run tests\\accessibility\\windows\\watch_progressbar_label.py [duration_seconds]
    4. Watch for "-- StatusBar children --" blocks in the output; the
       middle child (control_type=Text, name changing every second) is
       the countdown label. Its id=... / id_status=... fields are the
       answer to what its real AutomationId is.
"""

import sys
import time
from datetime import datetime

from comtypes import COMError

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
STATUSBAR_CLASS_NAME = "QStatusBar"
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
            if parent_class == STATUSBAR_CLASS_NAME:
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


def find_statusbar(window):
    for node in _walk(window):
        try:
            if node.element_info.class_name == STATUSBAR_CLASS_NAME:
                return node
        except Exception:
            continue
    return None


def read_raw_automation_id(node):
    """Bypasses pywinauto's automation_id property on purpose. That
    property is `try: return self._element.CurrentAutomationId except
    COMError: return None` (uia_element_info.py) -- it cannot tell us
    whether an empty/None result means "property genuinely not set" or
    "the COM call itself raised". We need that distinction, so this calls
    the same underlying COM property directly and reports which case
    happened. Returns (raw_id_or_None, status_str)."""
    try:
        raw_id = node.element_info.element.CurrentAutomationId
        if raw_id == "":
            status = "ok (empty string)"
        elif raw_id is None:
            status = "ok (None -- call succeeded but returned None, not '')"
        else:
            status = "ok"
        return raw_id, status
    except COMError as exc:
        return None, f"COMError: {exc!r}"
    except Exception as exc:
        return None, f"unexpected error ({type(exc).__name__}): {exc!r}"


def snapshot_statusbar_children(statusbar_node):
    """Direct children only -- not a recursive walk -- of the QStatusBar
    element. Returns a list of raw diagnostic dicts, one per child, making
    no assumption about which one is the countdown label. Used both to
    print the diagnostic block and to build a change-detection signature."""
    try:
        children = list(statusbar_node.children())
    except Exception as exc:
        return [{"error": f"could not enumerate StatusBar children: {exc!r}"}]

    snapshots = []
    for child in children:
        info = {}
        for field, getter in (
            ("control_type", lambda c=child: c.element_info.control_type),
            ("class_name", lambda c=child: c.element_info.class_name),
            ("name", lambda c=child: c.element_info.name),
        ):
            try:
                info[field] = getter()
            except Exception as exc:
                info[field] = f"(unavailable: {exc!r})"

        raw_id, id_status = read_raw_automation_id(child)
        info["automation_id"] = raw_id
        info["automation_id_status"] = id_status

        for field, getter in (
            ("visible", lambda c=child: c.is_visible()),
            ("enabled", lambda c=child: c.is_enabled()),
        ):
            try:
                info[field] = getter()
            except Exception as exc:
                info[field] = f"(unavailable: {exc!r})"

        snapshots.append(info)
    return snapshots


def format_statusbar_snapshot(children_info):
    lines = ["  -- StatusBar children --"]
    for i, info in enumerate(children_info):
        if "error" in info:
            lines.append(f"    ! {info['error']}")
            continue
        lines.append(
            f"    [{i}] control_type={info['control_type']!r} class_name={info['class_name']!r}\n"
            f"        name={info['name']!r}\n"
            f"        automation_id={info['automation_id']!r}  (id_status: {info['automation_id_status']})\n"
            f"        visible={info['visible']}  enabled={info['enabled']}"
        )
    return "\n".join(lines)


def statusbar_signature(children_info):
    """What triggers a re-print. Keyed on (control_type, name, automation_id)
    per child -- name because that's what changes every countdown tick,
    automation_id in case it's ever inconsistent between polls (which
    would itself be a finding worth seeing)."""
    return tuple((c.get("control_type"), c.get("name"), c.get("automation_id")) for c in children_info)


def main():
    duration = float(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_DURATION_S

    window = connect_to_app()
    print(f"Watching for '{TARGET_AUTOMATION_ID}' on '{window.window_text()}' for {duration:.0f}s.")
    print("Trigger Copy Password now if you haven't already -- an entry must be selected.\n")

    last_present, last_name, last_method = None, None, None
    last_statusbar_sig = None
    start = time.monotonic()
    change_count = 0

    while time.monotonic() - start < duration:
        present, name, method = read_label_state(window)
        statusbar_node = find_statusbar(window)
        statusbar_children = snapshot_statusbar_children(statusbar_node) if statusbar_node else []
        statusbar_sig = statusbar_signature(statusbar_children)

        label_changed = present != last_present or name != last_name
        statusbar_changed = statusbar_sig != last_statusbar_sig

        if label_changed or statusbar_changed:
            ts = datetime.now().strftime("%H:%M:%S.%f")[:-3]
            if present:
                tag = "" if method == "automation_id" else f"  [via {method}]"
                print(f"[{ts}] present, name={name!r}{tag}")
            else:
                print(f"[{ts}] not present (hidden or not yet created)")
            if statusbar_node is None:
                print("  ! QStatusBar not found in tree")
            else:
                print(format_statusbar_snapshot(statusbar_children))
            last_present, last_name, last_method = present, name, method
            last_statusbar_sig = statusbar_sig
            change_count += 1
        time.sleep(POLL_INTERVAL_S)

    print(f"\nDone. Logged {change_count} state change(s) over {duration:.0f}s.")
    if change_count == 0:
        print(
            "No changes seen at all -- either nothing was triggered during the "
            "window (no database open / no entry selected makes Copy Password a "
            "no-op), or something is still wrong with element lookup."
        )
    return 0


if __name__ == "__main__":
    sys.exit(main())
