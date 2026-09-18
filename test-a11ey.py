import xa11y

def main():
    app = xa11y.App.by_name("KeePassXC")
    print(f"Connected to: {app.name} (pid={app.pid})")

    print("\n--- Buttons ---")
    for el in app.locator("button").elements():
        print(f"  {el.name!r}")

    print("\n--- Text fields ---")
    for el in app.locator("text_field").elements():
        print(f"  {el.name!r}")

    print("\n--- All named elements ---")
    for el in app.locator("*").elements():
        if el.name:
            print(f"  {el.role}: {el.name!r}")

if __name__ == "__main__":
    main()