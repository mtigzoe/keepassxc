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


def main():
    if len(sys.argv) > 1:
        app = xa11y.App.by_pid(int(sys.argv[1]))
    else:
        app = xa11y.App.by_name("KeePassXC")
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


if __name__ == "__main__":
    main()
