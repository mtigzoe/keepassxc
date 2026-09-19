"""
Target path in the repo: tests/accessibility/windows/verify_remaining_alert_fixes.py

Live UIA event-capture companion to verify_alert_uia_notifications.py, for
the other four Alert-plus-Announcement fixes made in the same session:
TagsEdit::announceTagsState(), PasswordWidget::updateRepeatStatus(),
YubiKeyEditWidget::hardwareKeyResponse(), and MessageBox::messageBox()'s
"Weak password" warning (DatabaseSettingsWidgetDatabaseKey::saveSettings()).

verify_alert_uia_notifications.py live-verifies MessageWidget's fix. This script
exercises the other four source files' code paths (TagsEdit + PasswordWidget
+ YubiKeyEditWidget + MessageBox) and reports, per scenario, whether a real
UIA_NotificationEventId fired -- the same standard of evidence used for
MessageWidget, not just a static tree read.

What this does NOT prove: JAWS/NVDA behavior (see
verify_alert_uia_notifications.py's docstring -- same caveat applies here).

Requirements (already present in this repo's .venv): pywinauto, comtypes, pywin32.

Usage (from the repo's activated .venv):
    1. Have exactly one KeePassXC.exe running, at the Welcome screen (no
       database open yet).
    2. uv run tests\\accessibility\\windows\\verify_remaining_alert_fixes.py
       This copies tests/data/NewDatabase.kdbx (password "a") to a temp
       file, unlocks it, and drives the scenarios below. No real data is
       touched; all changes are made to the disposable temp copy and never
       saved to the original.
"""

import shutil
import sys
import tempfile
import time
import ctypes
from ctypes import wintypes
from pathlib import Path

import comtypes.client as cc
import comtypes.gen.UIAutomationClient as UIA
import pythoncom
from pywinauto import Desktop
from pywinauto.keyboard import send_keys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from dump_uia_tree import connect_to_app  # noqa: E402
from verify_alert_uia_notifications import (  # noqa: E402
    CLSID_CUIAutomation8,
    NotificationHandler,
    PropertyChangedHandler,
    describe_property_id,
    find_open_file_dialog,
    force_foreground,
    pump_messages_for,
    set_dialog_path_and_confirm,
)
from watch_progressbar_label import _walk

DEFAULT_TEST_DB = Path(__file__).resolve().parents[3] / "tests" / "data" / "NewDatabase.kdbx"
TEST_DB_PASSWORD = "a"


def poll_until(predicate, timeout_s, interval_s=0.25):
    deadline = time.time() + timeout_s
    result = predicate()
    while result is None and time.time() < deadline:
        time.sleep(interval_s)
        result = predicate()
    return result


def find_by(window, class_name=None, automation_id_suffix=None, name=None, control_type=None):
    for node in _walk(window):
        try:
            if class_name is not None and node.element_info.class_name != class_name:
                continue
            if automation_id_suffix is not None and not (node.element_info.automation_id or "").endswith(
                automation_id_suffix
            ):
                continue
            if name is not None and node.element_info.name != name:
                continue
            if control_type is not None and node.element_info.control_type != control_type:
                continue
            return node
        except Exception:
            continue
    return None


def find_window_by_exact_title(title):
    """QMessageBox/native dialogs are owned windows -- not enumerated by
    pywinauto's Desktop(backend="uia").windows() at all, confirmed directly
    (the 'Unsaved Changes' prompt was completely absent from that listing
    while still fully alive and clickable via its raw hwnd). Raw EnumWindows
    sees it; wrap the hwnd via Desktop(...).window(handle=...) once found."""
    user32 = ctypes.windll.user32
    results = []

    def _callback(hwnd, _lparam):
        length = user32.GetWindowTextLengthW(hwnd)
        buf = ctypes.create_unicode_buffer(length + 1)
        user32.GetWindowTextW(hwnd, buf, length + 1)
        if buf.value == title and user32.IsWindowVisible(hwnd):
            results.append(hwnd)
        return True

    enum_proc = ctypes.WINFUNCTYPE(ctypes.c_bool, wintypes.HWND, wintypes.LPARAM)
    user32.EnumWindows(enum_proc(_callback), 0)
    if not results:
        return None
    return Desktop(backend="uia").window(handle=results[0])


def dismiss_unsaved_changes_prompt():
    """Clicks Discard on the 'Unsaved Changes' confirmation if it appears
    within a couple seconds; a no-op if it never shows up."""
    dlg = poll_until(lambda: find_window_by_exact_title("Unsaved Changes"), timeout_s=3.0)
    if dlg is None:
        return
    for btn in dlg.descendants(control_type="Button"):
        if btn.window_text() == "Discard":
            btn.click_input()
            return


def capture_one_event(element, trigger_fn, wait_s=2.0):
    """Subscribes fresh handlers to `element`, calls `trigger_fn()`, pumps
    messages for `wait_s`, then unsubscribes. Returns the list of events."""
    iuia = cc.CreateObject(CLSID_CUIAutomation8, interface=UIA.IUIAutomation6)
    raw_element = element.element_info.element
    events = []
    notif_handler = NotificationHandler(events)
    prop_handler = PropertyChangedHandler(events)
    iuia.AddNotificationEventHandler(raw_element, UIA.TreeScope_Element | UIA.TreeScope_Descendants, None, notif_handler)
    iuia.AddPropertyChangedEventHandler(
        raw_element,
        UIA.TreeScope_Element | UIA.TreeScope_Descendants,
        None,
        prop_handler,
        [UIA.UIA_NamePropertyId, UIA.UIA_ValueValuePropertyId],
    )
    trigger_fn()
    pump_messages_for(wait_s)
    iuia.RemoveAllEventHandlers()
    return events


def report(scenario_name, events):
    print(f"\n=== {scenario_name} ===")
    if not events:
        print("  NO UIA events captured.")
        return
    for evt in events:
        if evt["type"] == "Notification":
            print(f"  [Notification] {evt['display_string']!r} kind={evt['notification_kind']} processing={evt['notification_processing']}")
        else:
            print(f"  [PropertyChanged] {describe_property_id(evt['property_id'])} -> {evt['new_value']!r}")


def main():
    window = connect_to_app()
    force_foreground(window.handle)

    with tempfile.TemporaryDirectory() as tmp_dir:
        tmp_db = Path(tmp_dir) / "verify-remaining-fixes.kdbx"
        shutil.copyfile(DEFAULT_TEST_DB, tmp_db)

        open_button = find_by(window, name="Open Database", control_type="Button")
        if open_button is None:
            sys.exit("Could not find an Open Database button.")
        open_button.click_input()

        dialog = poll_until(find_open_file_dialog, timeout_s=5.0)
        if dialog is None:
            sys.exit("Timed out waiting for the native Open dialog.")
        force_foreground(dialog.handle)
        set_dialog_path_and_confirm(dialog, tmp_db)

        password_edit = poll_until(
            lambda: find_by(window, class_name="QLineEdit", automation_id_suffix="passwordEdit"), timeout_s=10.0
        )
        if password_edit is None:
            sys.exit("Timed out waiting for the unlock password field.")
        force_foreground(window.handle)
        password_edit.set_edit_text(TEST_DB_PASSWORD)
        send_keys("{ENTER}")

        entry_view = poll_until(lambda: find_by(window, name="Entries"), timeout_s=10.0)
        if entry_view is None:
            sys.exit("Timed out waiting for the database to unlock (Entries view not found).")
        print("Database unlocked.")

        # --- Scenario 1: TagsEdit::announceTagsState() ---
        new_entry_button = find_by(window, name="New Entry", control_type="Button")
        if new_entry_button is None:
            print("Could not find New Entry button; skipping TagsEdit scenario.")
        else:
            force_foreground(window.handle)
            new_entry_button.click_input()
            tags_list = poll_until(lambda: find_by(window, automation_id_suffix="tagsList"), timeout_s=5.0)
            if tags_list is None:
                print("Could not find tagsList in the entry editor; skipping TagsEdit scenario.")
            else:
                force_foreground(window.handle)
                tags_list.set_focus()

                def trigger_tag_add():
                    send_keys("verify-tag-one{ENTER}")

                events = capture_one_event(tags_list, trigger_tag_add)
                report("TagsEdit::announceTagsState (add tag)", events)

            # Cancel the new-entry dialog so nothing is actually saved. This
            # pops an "Unsaved Changes" prompt (since a tag was added) --
            # discard it rather than leaving it stuck blocking the rest of
            # the script.
            cancel_button = find_by(window, name="Cancel", control_type="Button")
            if cancel_button is not None:
                force_foreground(window.handle)
                cancel_button.click_input()
                dismiss_unsaved_changes_prompt()

        # --- Scenarios 2 & 3: PasswordWidget mismatch + YubiKeyEditWidget ---
        # --- Scenario 4: MessageBox "Weak password" warning ---
        db_settings_button = find_by(window, name="Show Database Settings", control_type="CheckBox")
        if db_settings_button is None:
            sys.exit("Could not find the Show Database Settings toolbar button.")
        force_foreground(window.handle)
        db_settings_button.click_input()

        security_category = poll_until(lambda: find_by(window, name="Security"), timeout_s=5.0)
        if security_category is None:
            sys.exit("Timed out waiting for the Security settings category.")
        force_foreground(window.handle)
        security_category.click_input()

        credentials_tab = poll_until(lambda: find_by(window, name="Database Credentials"), timeout_s=5.0)
        if credentials_tab is None:
            sys.exit("Timed out waiting for the Database Credentials tab.")
        force_foreground(window.handle)
        credentials_tab.click_input()

        # The test database already has a password key, so this page starts
        # in "view" mode (Change Password/Remove Password buttons) rather
        # than showing the enter/repeat fields directly -- confirmed
        # directly by inspecting the live tree before adding this click.
        change_password_button = poll_until(lambda: find_by(window, name="Change Password"), timeout_s=5.0)
        if change_password_button is None:
            sys.exit("Could not find the Change Password button.")
        force_foreground(window.handle)
        change_password_button.click_input()

        enter_password = poll_until(
            lambda: find_by(window, class_name="QLineEdit", automation_id_suffix="enterPasswordEdit.passwordEdit"),
            timeout_s=5.0,
        )
        repeat_password = find_by(window, class_name="QLineEdit", automation_id_suffix="repeatPasswordEdit.passwordEdit")
        if enter_password is None or repeat_password is None:
            sys.exit("Could not find the enter/repeat password fields on the Database Credentials tab.")

        force_foreground(window.handle)
        enter_password.set_edit_text("weak1")

        def trigger_mismatch():
            repeat_password.set_edit_text("different")

        events = capture_one_event(repeat_password, trigger_mismatch)
        report("PasswordWidget::updateRepeatStatus (mismatch)", events)

        def trigger_match():
            repeat_password.set_edit_text("weak1")

        events = capture_one_event(repeat_password, trigger_match)
        report("PasswordWidget::updateRepeatStatus (match)", events)

        additional_toggle = find_by(window, automation_id_suffix="additionalKeyOptionsToggle")
        if additional_toggle is None:
            print("Could not find 'Add additional protection...' toggle; skipping YubiKey scenario.")
        else:
            force_foreground(window.handle)
            additional_toggle.click_input()
            refresh_button = poll_until(lambda: find_by(window, automation_id_suffix="refreshHardwareKeys"), timeout_s=5.0)
            combo = poll_until(lambda: find_by(window, automation_id_suffix="comboChallengeResponse"), timeout_s=5.0)
            if refresh_button is None or combo is None:
                print("Could not find YubiKey refresh button/combo; skipping YubiKey scenario.")
            else:
                force_foreground(window.handle)

                def trigger_refresh():
                    refresh_button.click_input()

                # Hardware-key detection is asynchronous; give it longer.
                events = capture_one_event(combo, trigger_refresh, wait_s=4.0)
                report("YubiKeyEditWidget::hardwareKeyResponse (no hardware keys)", events)

        # --- Scenario 4: trigger the weak-password MessageBox ---
        force_foreground(window.handle)
        enter_password.set_edit_text("weak1")
        repeat_password.set_edit_text("weak1")

        save_button = find_by(window, name="OK", control_type="Button") or find_by(
            window, name="Save", control_type="Button"
        )
        if save_button is None:
            print("Could not find the settings dialog's OK/Save button; skipping MessageBox scenario.")
        else:
            msgbox_events = []
            iuia = cc.CreateObject(CLSID_CUIAutomation8, interface=UIA.IUIAutomation6)
            notif_handler = NotificationHandler(msgbox_events)
            desktop_root = iuia.GetRootElement()
            iuia.AddNotificationEventHandler(desktop_root, UIA.TreeScope_Subtree, None, notif_handler)

            force_foreground(window.handle)
            save_button.click_input()
            pump_messages_for(2.0)
            iuia.RemoveAllEventHandlers()
            report("MessageBox::messageBox (Weak password)", msgbox_events)

            # Dismiss the warning dialog (Cancel keeps the weak password
            # unset) so the process can exit cleanly.
            send_keys("{ESC}")

        # Discard everything on the settings dialog itself too, so nothing
        # from this run is left applied to the (already-disposable) temp db.
        settings_cancel = find_by(window, automation_id_suffix="databaseSettingsDialog.buttonBox.QPushButton", name="Cancel")
        if settings_cancel is not None:
            force_foreground(window.handle)
            settings_cancel.click_input()

    print("\nDone. No changes were saved to any real database.")


if __name__ == "__main__":
    main()
