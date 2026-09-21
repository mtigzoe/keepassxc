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

    repo_root = __file__
    for _ in range(3):
        repo_root = __import__("os").path.dirname(repo_root)

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
        __import__("os").path.normcase(
            __import__("os").path.abspath(
                __import__("os").path.join(repo_root, "build", "src", "KeePassXC.exe")
            )
        ),
        __import__("os").path.normcase(
            __import__("os").path.abspath(
                __import__("os").path.join(
                    repo_root, "build-vscode-86", "src", "KeePassXC.exe"
                )
            )
        ),
    }

    for process in processes:
        executable = process.get("ExecutablePath")
        if executable and __import__("os").path.normcase(
            __import__("os").path.abspath(executable)
        ) in expected_paths:
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

    print("\n--- All named elements ---")
    all_elements = app.locator("*").elements()
    for el in all_elements:
        if el.name:
            print(f"  {el.role}: {el.name!r}")

    # --- New: find every button with no accessible name, and show its ---
    # --- full ancestor chain + raw platform data (uia_control_type,     ---
    # --- uia_class_name, etc. on Windows) so we can identify exactly    ---
    # --- which widget it is instead of guessing from source.           ---
    print("\n--- Unnamed buttons: detail ---")
    unnamed = [el for el in app.locator("button").elements() if not el.name]
    if not unnamed:
        print("  (none)")
    for el in unnamed:
        print("\n  Unnamed button found:")
        describe(el, "    ")
        print("    --- ancestor chain (self -> root) ---")
        for i, anc in enumerate(ancestor_chain(el)):
            print(f"    [{i}] role={anc.role!r} name={anc.name!r} raw={anc.raw!r}")

    # --- New: find any name that's shared between a button and a       ---
    # --- static_text (or any two different roles) -- this is the       ---
    # --- duplicate-announcement pattern from the warning banner.       ---
    print("\n--- Names duplicated across different roles ---")
    by_name = {}
    for el in all_elements:
        if el.name:
            by_name.setdefault(el.name, set()).add(str(el.role))
    dupes = {name: roles for name, roles in by_name.items() if len(roles) > 1}
    if not dupes:
        print("  (none)")
    for name, roles in dupes.items():
        print(f"  {name!r} exposed as roles: {sorted(roles)}")

    return 0


if __name__ == "__main__":
    sys.exit(main())