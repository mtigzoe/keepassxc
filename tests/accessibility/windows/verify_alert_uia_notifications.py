"""
Target path in the repo: tests/accessibility/windows/verify_alert_uia_notifications.py

Empirical companion to the source-reading finding that `QAccessible::Alert`
(fired by MessageWidget::showMessage(), PasswordWidget's match-state banner,
MessageBox.cpp, TagsEdit.cpp, YubiKeyEditWidget.cpp) never reaches Windows UI
Automation as an event -- confirmed by reading qtbase's
qwindowsuiaaccessibility.cpp / qwindowsuiamainprovider.cpp (6.11 branch)
directly: QAccessible::Alert only plays a system sound
(QWindowsUiaAccessibility::notifyAccessibilityUpdate()'s first switch); it is
not one of the cases in the second switch that dispatches to UIA
(Announcement, Focus, StateChanged, ValueChanged, NameChanged, RoleChanged,
SelectionAdd, Text*). This script proves that empirically, against the real
running KeePassXC.exe, instead of relying on the source reading alone.

What this DOES prove:
  - Whether a UIA_NotificationEventId event (the actual push notification a
    screen reader's UIA integration would react to) fires when
    DatabaseOpenWidget's messageWidget shows its "Wrong password" error
    (which goes through MessageWidget::showMessage() -> QAccessible::Alert).
  - Whether a UIA_AutomationPropertyChangedEventId event for the Name or
    Value property fires for that same messageWidget element at the same
    moment (covering the NameChanged/ValueChanged code paths too, in case a
    future fix repurposes one of those instead of Announcement).
  - The messageWidget's static Name property before and after, independent
    of whether any event fired -- this is the "accessible name is correct"
    fact already confirmed manually; this script re-confirms it
    mechanically and ties it to the same run as the event capture.

What this does NOT prove:
  - That JAWS/NVDA would or wouldn't say anything. JAWS's virtual buffer can
    behave independently of raw UIA events (e.g. it may re-scan focused
    ancestors on its own timer). Zero UIA_NotificationEventId /
    property-changed events observed here means Qt raised nothing for AT
    clients to react to via UIA -- it does not simulate what JAWS itself
    does with focus, sound cues, or its own heuristics. A real JAWS session
    is still required to close that gap.
  - Anything about macOS/Linux -- this is the Windows UIA bridge only.

Requirements (already present in this repo's .venv):
    uv pip install pywinauto comtypes

Usage (from the repo's activated .venv, i.e. after
`.\\.venv\\Scripts\\activate`):
    1. Make sure exactly one KeePassXC.exe (built from build-vscode-x64) is
       running, with no database open (Welcome screen or File menu
       reachable) -- close any already-open database tab first.
    2. uv run tests\\accessibility\\windows\\verify_alert_uia_notifications.py \
           [path-to-test-kdbx]
       If no path is given, this copies tests/data/NewDatabase.kdbx to a
       temp file itself (the real file's correct password is "a", and this
       script deliberately enters an incorrect one to trigger the error
       banner -- it never needs the correct password).
"""

import ctypes
import shutil
import sys
import tempfile
import time
from ctypes import wintypes
from pathlib import Path

import comtypes.client as cc
import comtypes.gen.UIAutomationClient as UIA
import pythoncom
from comtypes import COMObject, GUID
from pywinauto import Desktop
from pywinauto.keyboard import send_keys

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

# CLSID_CUIAutomation8 -- the class that actually implements IUIAutomation6
# (AddNotificationEventHandler needs IUIAutomation4+; the plain
# CUIAutomation class pywinauto's IUIA() wraps only answers QueryInterface
# for the original IUIAutomation on this machine, confirmed by trying it).
CLSID_CUIAutomation8 = GUID("{e22ad333-b25f-460c-83d0-0581107395c9}")

REPO_ROOT = Path(__file__).resolve().parents[3]
DEFAULT_TEST_DB = REPO_ROOT / "tests" / "data" / "NewDatabase.kdbx"
WRONG_PASSWORD = "this-is-deliberately-wrong"


def _all_nodes(window):
    from watch_progressbar_label import _walk  # reuse the same walk helper

    return list(_walk(window))


def find_database_open_message_widget(window):
    """Qt's automation_id is the full dotted parent-objectName chain (see
    automationIdForAccessible() in qtbase), not just the leaf name -- e.g.
    the always-visible warning banner shows up as
    "...centralwidget.globalMessageWidget". This looks for a *different*
    MessageWidget instance: the one inside DatabaseOpenWidget, identified by
    its automation_id containing "databaseOpenWidget" and NOT being
    globalMessageWidget."""
    for node in _all_nodes(window):
        try:
            if node.element_info.class_name != "MessageWidget":
                continue
            auto_id = node.element_info.automation_id or ""
            if "globalMessageWidget" in auto_id:
                continue
            if "databaseOpenWidget" in auto_id or "messageWidget" in auto_id:
                return node
        except Exception:
            continue
    return None


def find_password_edit(window):
    """Finds the QLineEdit inside DatabaseOpenWidget's PasswordWidget by
    automation_id suffix, since Qt's full dotted automation_id chain makes
    pywinauto's normal auto_id="passwordEdit" exact-match lookup fail."""
    for node in _all_nodes(window):
        try:
            if node.element_info.class_name != "QLineEdit":
                continue
            auto_id = node.element_info.automation_id or ""
            if auto_id.endswith("passwordEdit"):
                return node
        except Exception:
            continue
    return None


def find_open_database_button(window):
    for node in _all_nodes(window):
        try:
            if node.element_info.control_type == "Button" and node.element_info.name == "Open Database":
                return node
        except Exception:
            continue
    return None


def poll_until(predicate, timeout_s, interval_s=0.25):
    deadline = time.time() + timeout_s
    result = predicate()
    while result is None and time.time() < deadline:
        time.sleep(interval_s)
        result = predicate()
    return result


def pump_messages_for(duration_s):
    """time.sleep() never delivers queued incoming COM calls -- our
    NotificationHandler/PropertyChangedHandler COMObjects run in an STA
    (per pywinauto's own "Revert to STA COM threading mode" warning at
    startup), and STA COM requires the thread's Windows message queue to be
    pumped for cross-apartment/cross-process calls to be dispatched at all.
    Without this, event callbacks could arrive and simply sit undelivered
    for the entire sleep, making "0 events captured" indistinguishable from
    "Qt genuinely raised nothing" -- exactly the ambiguity this script
    exists to resolve, so it can't be allowed to leak in here too."""
    deadline = time.time() + duration_s
    while time.time() < deadline:
        pythoncom.PumpWaitingMessages()
        time.sleep(0.02)


def force_foreground(hwnd):
    """Plain SetForegroundWindow silently fails here -- confirmed directly
    (GetForegroundWindow() stayed pointed at the calling terminal/VS Code
    afterward) -- because Windows' foreground-lock blocks a background
    process from stealing focus. AttachThreadInput temporarily merges input
    state with the current foreground thread, which is the standard,
    confirmed-working bypass; without this, click_input()/send_keys land on
    whatever window actually has focus instead of the target."""
    user32 = ctypes.windll.user32
    fg_hwnd = user32.GetForegroundWindow()
    fg_thread = user32.GetWindowThreadProcessId(fg_hwnd, None)
    target_thread = user32.GetWindowThreadProcessId(hwnd, None)
    user32.AttachThreadInput(target_thread, fg_thread, True)
    user32.ShowWindow(hwnd, 9)  # SW_RESTORE
    user32.SetForegroundWindow(hwnd)
    user32.BringWindowToTop(hwnd)
    user32.AttachThreadInput(target_thread, fg_thread, False)


def find_open_file_dialog():
    """pywinauto's Desktop(backend="uia").windows() does NOT enumerate this
    dialog at all -- confirmed directly (it was absent from that listing
    while still fully alive, visible, and interactable via its raw hwnd).
    It's an owned dialog rather than an independent top-level window, which
    that enumeration mode apparently excludes. Raw EnumWindows does see it,
    and Desktop(...).window(handle=...) can still wrap it directly by hwnd
    for descendants()/set_edit_text() once found this way."""
    user32 = ctypes.windll.user32
    results = []

    def _callback(hwnd, _lparam):
        length = user32.GetWindowTextLengthW(hwnd)
        buf = ctypes.create_unicode_buffer(length + 1)
        user32.GetWindowTextW(hwnd, buf, length + 1)
        cls = ctypes.create_unicode_buffer(256)
        user32.GetClassNameW(hwnd, cls, 256)
        if cls.value == "#32770" and "open" in buf.value.lower() and user32.IsWindowVisible(hwnd):
            results.append(hwnd)
        return True

    enum_proc = ctypes.WINFUNCTYPE(ctypes.c_bool, wintypes.HWND, wintypes.LPARAM)
    user32.EnumWindows(enum_proc(_callback), 0)
    if not results:
        return None
    return Desktop(backend="uia").window(handle=results[0])


def set_dialog_path_and_confirm(dialog, path):
    """send_keys corrupts/drops characters typing a full path into the
    native Open dialog (observed directly: "verify-alert-uia.kdbx" arrived
    as "vxerify-alert-uia.kdb") -- likely a timing issue with a string this
    long.

    automation_id "1148" is the Explorer-style common dialog's well-known
    filename combo/edit ID -- the first Edit found by descendants() is a
    list-view column ('Name'), not the filename box, confirmed directly.

    pywinauto's set_edit_text() on this control silently no-ops here --
    confirmed directly (window_text() kept reading the "File name:" label
    afterward). Calling IUIAutomationValuePattern.SetValue() on the raw COM
    element directly instead does work (confirmed via the pattern's own
    CurrentValue reading back the path correctly, and Enter successfully
    submitting a valid path once this was in place) -- window_text() on
    this particular hosted combo edit just never reflects the value either
    way, so it can't be used to verify success, only CurrentValue can.
    Setting focus before Enter is required too: SetValue does not move
    keyboard focus, and Enter without focus on the edit doesn't submit."""
    edits = [e for e in dialog.descendants(control_type="Edit") if e.element_info.automation_id == "1148"]
    if not edits:
        raise RuntimeError('Open dialog has no automation_id="1148" filename Edit control')

    value_pattern = edits[0].element_info.element.GetCurrentPattern(UIA.UIA_ValuePatternId)
    value_pattern.QueryInterface(UIA.IUIAutomationValuePattern).SetValue(str(path))

    edits[0].set_focus()
    send_keys("{ENTER}")


class NotificationHandler(COMObject):
    """Implements IUIAutomationNotificationEventHandler. comtypes dispatches
    to a method named "<InterfaceName>_<MethodName>" on a COMObject
    subclass (confirmed against comtypes/_comobject.py's IPersist_GetClassID
    pattern) -- a plain method named HandleNotificationEvent is not enough."""

    _com_interfaces_ = [UIA.IUIAutomationNotificationEventHandler]

    def __init__(self, events):
        super().__init__()
        self.events = events

    def IUIAutomationNotificationEventHandler_HandleNotificationEvent(
        self, sender, NotificationKind, NotificationProcessing, displayString, activityId
    ):
        self.events.append(
            {
                "time": time.time(),
                "type": "Notification",
                "display_string": displayString,
                "notification_kind": NotificationKind,
                "notification_processing": NotificationProcessing,
            }
        )
        return 0


class PropertyChangedHandler(COMObject):
    """Implements IUIAutomationPropertyChangedEventHandler (see
    NotificationHandler above for why the method must be named
    IUIAutomationPropertyChangedEventHandler_HandlePropertyChangedEvent)."""

    _com_interfaces_ = [UIA.IUIAutomationPropertyChangedEventHandler]

    def __init__(self, events):
        super().__init__()
        self.events = events

    def IUIAutomationPropertyChangedEventHandler_HandlePropertyChangedEvent(self, sender, propertyId, newValue):
        self.events.append(
            {
                "time": time.time(),
                "type": "PropertyChanged",
                "property_id": propertyId,
                "new_value": newValue,
            }
        )
        return 0


def describe_property_id(prop_id):
    names = {
        UIA.UIA_NamePropertyId: "UIA_NamePropertyId",
        UIA.UIA_ValueValuePropertyId: "UIA_ValueValuePropertyId",
    }
    return names.get(prop_id, str(prop_id))


def main():
    test_db_arg = sys.argv[1] if len(sys.argv) > 1 else None
    src_db = Path(test_db_arg) if test_db_arg else DEFAULT_TEST_DB
    if not src_db.exists():
        print(f"ERROR: test database not found: {src_db}", file=sys.stderr)
        sys.exit(2)

    print(f"Connecting to running KeePassXC window (using test db: {src_db})...")
    window = connect_to_app()
    force_foreground(window.handle)

    with tempfile.TemporaryDirectory() as tmp_dir:
        tmp_db = Path(tmp_dir) / "verify-alert-uia.kdbx"
        shutil.copyfile(src_db, tmp_db)

        open_button = find_open_database_button(window)
        if open_button is None:
            print('Could not find an "Open Database" button.', file=sys.stderr)
            sys.exit(1)
        open_button.click_input()

        dialog = poll_until(find_open_file_dialog, timeout_s=5.0)
        if dialog is None:
            print("Timed out waiting for the native Open dialog to appear.", file=sys.stderr)
            sys.exit(1)
        force_foreground(dialog.handle)
        set_dialog_path_and_confirm(dialog, tmp_db)

        password_edit = poll_until(lambda: find_password_edit(window), timeout_s=10.0)
        if password_edit is None:
            print("Timed out waiting for DatabaseOpenWidget's password field to appear.", file=sys.stderr)
            sys.exit(1)

        # DatabaseOpenWidget's messageWidget does not exist in the UIA tree
        # at all while hidden -- confirmed directly (dump_uia_tree.py showed
        # no MessageWidget-classed element under databaseOpenWidget before
        # any error had been shown). It can only be found, and therefore
        # only be subscribed to, after showMessage() has made it visible at
        # least once. So: trigger the error once un-instrumented just to
        # make the element exist, subscribe to it, then trigger it again --
        # that second call is the one this script actually measures.
        print("Submitting a first wrong password to make messageWidget exist in the UIA tree...")
        force_foreground(window.handle)
        password_edit.set_edit_text(WRONG_PASSWORD)
        send_keys("{ENTER}")

        message_widget = poll_until(lambda: find_database_open_message_widget(window), timeout_s=5.0)
        if message_widget is None:
            print("Timed out waiting for DatabaseOpenWidget's messageWidget to appear in the UIA tree.", file=sys.stderr)
            sys.exit(1)

        name_before = message_widget.element_info.name
        print(f"messageWidget Name BEFORE second trigger: {name_before!r}")

        # Build our own CUIAutomation8 COM object (not pywinauto's IUIA()
        # shared instance) specifically to get IUIAutomation6 /
        # AddNotificationEventHandler.
        iuia = cc.CreateObject(CLSID_CUIAutomation8, interface=UIA.IUIAutomation6)

        # Reuse pywinauto's own COM element pointer rather than going through
        # ElementFromHandle -- messageWidget is a non-toplevel Qt widget with
        # no HWND of its own (only top-level windows get one), so
        # ElementFromHandle would fail; element_info.element is the raw
        # IUIAutomationElement pywinauto already holds for exactly this node.
        element = message_widget.element_info.element

        events = []
        notif_handler = NotificationHandler(events)
        prop_handler = PropertyChangedHandler(events)

        # Signatures confirmed directly against the generated comtypes
        # typelib (UIA.IUIAutomation5._methods_ / UIA.IUIAutomation._methods_)
        # rather than assumed from MSDN, since paramflags/argtypes can differ
        # by Windows SDK version bundled with comtypes' cached typelib.
        iuia.AddNotificationEventHandler(
            element, UIA.TreeScope_Element | UIA.TreeScope_Descendants, None, notif_handler
        )
        iuia.AddPropertyChangedEventHandler(
            element,
            UIA.TreeScope_Element | UIA.TreeScope_Descendants,
            None,
            prop_handler,
            [UIA.UIA_NamePropertyId, UIA.UIA_ValueValuePropertyId],
        )

        # Password field needs to be re-found: the first wrong-password
        # submission may have re-created/cleared it rather than reusing the
        # same QLineEdit instance.
        password_edit = poll_until(lambda: find_password_edit(window), timeout_s=5.0)
        if password_edit is None:
            print("Timed out re-finding the password field for the second trigger.", file=sys.stderr)
            sys.exit(1)

        print("Event handlers registered. Submitting the wrong password a second time...")
        force_foreground(window.handle)
        password_edit.set_edit_text(WRONG_PASSWORD)
        send_keys("{ENTER}")

        # Give Qt's animation/timer-driven show a moment to run, and pump
        # messages so any queued COM event callbacks actually get delivered
        # (see pump_messages_for()) before reading anything back.
        pump_messages_for(2.0)

        name_after = message_widget.element_info.name
        print(f"messageWidget Name AFTER second trigger:  {name_after!r}")

    print(f"\nCaptured {len(events)} UIA event(s) during the trigger window:")
    if not events:
        print(
            "  (none) -- no UIA_NotificationEventId and no "
            "Name/Value property-changed event fired. This is the expected "
            "result if QAccessible::Alert is the only event raised: per "
            "qwindowsuiaaccessibility.cpp, Alert is not forwarded to UIA at "
            "all, so nothing here should have fired."
        )
    for evt in events:
        if evt["type"] == "Notification":
            print(
                f"  [Notification] display_string={evt['display_string']!r} "
                f"kind={evt['notification_kind']} processing={evt['notification_processing']}"
            )
        else:
            print(f"  [PropertyChanged] {describe_property_id(evt['property_id'])} -> {evt['new_value']!r}")

    iuia.RemoveAllEventHandlers()

    name_changed = name_before != name_after
    print("\nSummary:")
    print(f"  Static Name property changed: {name_changed}")
    print(f"  Live UIA event(s) observed:   {len(events) > 0}")
    if name_changed and not events:
        print(
            "  -> Confirms the finding: the accessible tree's Name is updated "
            "correctly, but nothing was pushed to UIA as a live event. A "
            "screen reader polling on its own schedule might still pick up "
            "the new Name; nothing here proves it will."
        )


if __name__ == "__main__":
    main()
