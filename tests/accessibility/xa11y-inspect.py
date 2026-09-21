import os
import sys

import xa11y


def describe(el, indent=""):
    print(f"{indent}role={el.role!r} name={el.name!r} visible={el.visible!r} bounds={el.bounds!r}")
    print(f"{indent}raw={el.raw!r}")


def ancestor_chain(el, max_depth=10):
    chain = []
    node = el
    for _ in range(max_depth):
        if node is None:
            break
        chain.append(node)
        try:
            node = node.parent()
        except Exception:
            break
    return chain


def find_keepassxc_pid():
    """Find KeePassXC by executable path in either supported build directory."""
    if sys.platform != "win32":
        return None

    repo_root = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))

    import json
    import subprocess

    result = subprocess.run(
        [
            "powershell.exe",
            "-NoProfile",
            "-Command",
            "Get-CimInstance Win32_Process -Filter \"Name='KeePassXC.exe'\" "
            "| Select-Object ProcessId,ExecutablePath | ConvertTo-Json -Compress",
        ],
        capture_output=True,
        text=True,
        check=False,
    )
    if result.returncode != 0 or not result.stdout.strip():
        return None

    try:
        processes = json.loads(result.stdout)
    except json.JSONDecodeError:
        return None

    if isinstance(processes, dict):
        processes = [processes]

    expected_paths = {
        os.path.normcase(os.path.abspath(os.path.join(repo_root, "build", "src", "KeePassXC.exe"))),
        os.path.normcase(
            os.path.abspath(os.path.join(repo_root, "build-vscode-86", "src", "KeePassXC.exe"))
        ),
    }

    for process in processes:
        executable = process.get("ExecutablePath")
        if executable and os.path.normcase(os.path.abspath(executable)) in expected_paths:
            return int(process["ProcessId"])

    return None


def main():
    if len(sys.argv) > 1:
        app = xa11y.App.by_pid(int(sys.argv[1]))
    else:
        pid = find_keepassxc_pid()
        try:
            if pid is not None:
                app = xa11y.App.by_pid(pid)
            else:
                app = xa11y.App.by_name("KeePassXC")
        except Exception as exc:
            print("KeePassXC is not running or could not be discovered.")
            print("Launch KeePassXC from build/src or build-vscode-86/src and run this script again.")
            print(f"Discovery error: {exc}")
            return 1

    print(f"Connected to: {app.name} (pid={app.pid})")

    print("\n--- Buttons ---")
    for el in app.locator("button").elements():
        print(f"  {el.name!r}")

    print("\n--- Text fields ---")
    for el in app.locator("text_field").elements():
        print(f"  {el.name!r}")

    print("\n--- Spin buttons: UIA value details ---")
    spin_buttons = app.locator("spin_button").elements()
    if not spin_buttons:
        print("  (none)")
    for el in spin_buttons:
        print(f"\n  {el.name!r}")
        print(f"    role={el.role!r} visible={el.visible!r} bounds={el.bounds!r}")
        print(f"    raw={el.raw!r}")
        for attribute in ("value", "is_enabled", "is_keyboard_focusable", "has_keyboard_focus"):
            try:
                value = getattr(el, attribute)
            except Exception as exc:
                print(f"    {attribute}=<error: {exc}>")
                continue
            try:
                value = value() if callable(value) else value
            except Exception as exc:
                value = f"<error: {exc}>"
            print(f"    {attribute}={value!r}")
        print("    public attributes containing value/focus/pattern:")
        try:
            names = sorted(
                name for name in dir(el)
                if any(token in name.lower() for token in ("value", "focus", "pattern"))
            )
            print(f"      {names!r}")
        except Exception as exc:
            print(f"      <error: {exc}>")

        print("    public attributes containing child/descendant/parent:")
        try:
            names = sorted(
                name
                for name in dir(el)
                if any(token in name.lower() for token in ("child", "descendant", "parent"))
            )
            print(f"      {names!r}")
        except Exception as exc:
            print(f"      <error: {exc}>")

    print("\n--- All named elements ---")
    all_elements = app.locator("*").elements()
    for el in all_elements:
        if el.name:
            print(f"  {el.role}: {el.name!r}")

    # xa11y can expose some Qt layout widgets (QFrame/QSplitter) as
    # buttons on Windows UI Automation. They are not interactive controls,
    # so exclude those known non-interactive Qt classes from the report.
    print("\n--- Unnamed buttons: detail ---")
    button_elements = app.locator("button").elements()
    non_interactive_qt_classes = {"QFrame", "QSplitter"}
    unnamed = [
        el
        for el in button_elements
        if not el.name and el.raw.get("class_name") not in non_interactive_qt_classes
    ]
    if not unnamed:
        print("  (none)")
    for el in unnamed:
        print("\n  Unnamed button found:")
        describe(el, "    ")
        print("    --- ancestor chain (self -> root) ---")
        for i, anc in enumerate(ancestor_chain(el)):
            print(f"    [{i}] role={anc.role!r} name={anc.name!r} raw={anc.raw!r}")

    # Report duplicate names only when a button shares its name with another role.
    print("\n--- Names duplicated across different roles ---")
    by_name = {}
    for el in all_elements:
        if el.name:
            by_name.setdefault(el.name, set()).add(str(el.role))
    dupes = {
        name: roles
        for name, roles in by_name.items()
        if len(roles) > 1 and "button" in roles
    }
    if not dupes:
        print("  (none)")
    for name, roles in dupes.items():
        print(f"  {name!r} exposed as roles: {sorted(roles)}")

    return 0


if __name__ == "__main__":
    sys.exit(main())