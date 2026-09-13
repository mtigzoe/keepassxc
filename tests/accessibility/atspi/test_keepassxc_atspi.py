#!/usr/bin/env python3
"""
tests/accessibility/atspi/test_keepassxc_atspi.py

Linux AT-SPI accessibility regression test for KeePassXC.

Unlike tests/accessibility/dump_tree.py (a diagnostic tree-dump helper),
this is the actual automated regression suite for KeePassXC's Linux
accessibility tree: it fails when required application-level accessible
objects go missing, get renamed, or lose their accessible role.

This uses the same GObject-introspection libatspi bindings as
dump_tree.py (gi.repository.Atspi) -- the accessibility stack real
assistive technology such as Orca uses -- rather than talking to
org.a11y.atspi.* over D-Bus directly. See the AT-SPI documentation on
the Python bindings and on the application root/GetChildren traversal
model this test relies on:
  https://gnome.pages.gitlab.gnome.org/at-spi2-core/devel-docs/atspi-python-stack.html
  https://gnome.pages.gitlab.gnome.org/at-spi2-core/devel-docs/doc-org.a11y.atspi.Accessible.html

Run standalone (KeePassXC and the AT-SPI bus must already be running):
    QT_ACCESSIBILITY=1 QT_LINUX_ACCESSIBILITY_ALWAYS_ON=1 ./keepassxc &
    pytest tests/accessibility/atspi/test_keepassxc_atspi.py -v

The CI workflow (.github/workflows/accessibility-atspi.yml) owns the
environment this test needs: Xvfb, the AT-SPI bus and registry, and a
running KeePassXC process. This file owns the assertions.
"""

import time
from collections import defaultdict

import pytest

try:
    import gi

    gi.require_version("Atspi", "2.0")
    from gi.repository import Atspi
except (ImportError, ValueError) as exc:
    pytest.skip(f"GObject AT-SPI binding is not available: {exc}", allow_module_level=True)

APP_NAME = "keepassxc"
CONNECT_TIMEOUT_S = 20
CONNECT_RETRY_INTERVAL_S = 0.5
TREE_WALK_TIMEOUT_S = 15
MAX_TREE_NODES = 2000

# The Welcome screen's three primary actions (src/gui/WelcomeWidget.ui).
# All three are QPushButtons, so a real regression -- e.g. one losing its
# accessible name, or turning into an unlabelled custom widget -- should
# fail here rather than only being caught by a human running a screen
# reader.
REQUIRED_CONTROLS = {
    "Create Database": "push button",
    "Open Database": "push button",
    "Import File": "push button",
}


def _children(node):
    """Return an accessible's children.

    Accessible objects in this GObject-introspection binding are not
    Python-iterable (unlike, say, a list) -- children must be fetched
    positionally through get_child_count()/get_child_at_index().
    """
    try:
        count = node.get_child_count()
    except Exception:
        return []

    children = []
    for i in range(count):
        try:
            child = node.get_child_at_index(i)
        except Exception:
            continue
        if child is not None:
            children.append(child)
    return children


def _applications():
    """Return the applications currently exposed by the AT-SPI desktop."""
    desktop = Atspi.get_desktop(0)
    if desktop is None:
        return []
    return _children(desktop)


@pytest.fixture(scope="module")
def atspi_session():
    """Initialize the AT-SPI client connection for the whole test module."""
    Atspi.init()
    try:
        yield
    finally:
        try:
            Atspi.exit()
        except Exception:
            pass


@pytest.fixture(scope="module")
def keepassxc_app(atspi_session):
    """Locate KeePassXC as an application on the AT-SPI desktop.

    Polls rather than doing a single check: the AT-SPI registry can take
    a moment to pick up a freshly launched application after it appears
    on the session bus.
    """
    deadline = time.time() + CONNECT_TIMEOUT_S
    last_seen = []

    while time.time() < deadline:
        apps = _applications()
        last_seen = [app.get_name() or "(unnamed)" for app in apps]
        for app in apps:
            if (app.get_name() or "").lower() == APP_NAME.lower():
                return app
        time.sleep(CONNECT_RETRY_INTERVAL_S)

    pytest.fail(
        f"'{APP_NAME}' never appeared on the AT-SPI desktop within {CONNECT_TIMEOUT_S}s. "
        f"Applications seen: {last_seen!r}. Check that KeePassXC is running, that "
        "QT_ACCESSIBILITY=1 and QT_LINUX_ACCESSIBILITY_ALWAYS_ON=1 are set in its "
        "environment, and that the AT-SPI bus and registry are up before this test runs."
    )


def _walk_tree(root):
    """Breadth-first walk of an accessible subtree.

    Returns (nodes_by_name, visited_count). nodes_by_name maps accessible
    name -> list of accessible objects with that name, since more than
    one object in the tree can legitimately share a name (e.g. a menu
    action and a button that both say "Open Database"); callers that
    care about role should check every node for a name, not just the
    first one found.
    """
    nodes_by_name = defaultdict(list)
    queue = [root]
    visited = 0
    deadline = time.time() + TREE_WALK_TIMEOUT_S

    while queue and visited < MAX_TREE_NODES and time.time() < deadline:
        node = queue.pop(0)
        visited += 1

        try:
            name = node.get_name() or ""
        except Exception:
            name = ""

        if name:
            nodes_by_name[name].append(node)

        queue.extend(_children(node))

    return nodes_by_name, visited


@pytest.fixture(scope="module")
def accessible_tree(keepassxc_app):
    """Traverse KeePassXC's accessible tree once for the whole test module."""
    nodes_by_name, visited = _walk_tree(keepassxc_app)
    assert visited > 0, "AT-SPI tree walk visited zero nodes starting from the application root"
    return nodes_by_name


def test_keepassxc_is_exposed_as_an_application(keepassxc_app):
    """KeePassXC must be visible as its own application on the AT-SPI desktop."""
    name = keepassxc_app.get_name()
    assert name, "KeePassXC's application accessible object has no accessible name"
    assert name.lower() == APP_NAME.lower(), f"Expected application name '{APP_NAME}', got {name!r}"

    role = keepassxc_app.get_role_name()
    assert role == "application", f"Expected KeePassXC's AT-SPI role to be 'application', got {role!r}"


def test_accessible_tree_is_reachable(accessible_tree):
    """The application must expose more than just its own root node.

    A tree containing only the root (no children reached at all) means
    Qt's accessibility bridge did not populate -- a much more basic
    failure than any single control going missing, and worth
    distinguishing in the failure message so it isn't mistaken for one.
    """
    assert len(accessible_tree) > 1, (
        "AT-SPI tree walk only found the application root itself "
        f"(names found: {sorted(accessible_tree)}). This points at Qt's "
        "accessibility bridge not populating (QT_ACCESSIBILITY / "
        "QT_LINUX_ACCESSIBILITY_ALWAYS_ON, or the AT-SPI registry not "
        "being ready), not at a specific missing control."
    )


@pytest.mark.parametrize("expected_name", sorted(REQUIRED_CONTROLS))
def test_required_control_is_present(accessible_tree, expected_name):
    """Each control central to the first-run experience must be reachable."""
    assert expected_name in accessible_tree, (
        f"'{expected_name}' was not found anywhere in KeePassXC's AT-SPI tree. "
        f"Names actually found: {sorted(accessible_tree)}"
    )


@pytest.mark.parametrize("expected_name", sorted(REQUIRED_CONTROLS))
def test_required_control_has_expected_role(accessible_tree, expected_name):
    """Required controls must keep an actionable role, not just their name.

    This guards against a control keeping its accessible *name* while
    losing its accessible *role* (e.g. degrading into a plain,
    non-actionable widget) -- a regression a name-only check would miss
    entirely.
    """
    candidates = accessible_tree.get(expected_name, [])
    assert candidates, f"'{expected_name}' was not found in the tree"

    expected_role = REQUIRED_CONTROLS[expected_name]
    roles_seen = [node.get_role_name() for node in candidates]
    assert expected_role in roles_seen, (
        f"None of the {len(candidates)} accessible object(s) named "
        f"'{expected_name}' have role '{expected_role}'. Roles found: {roles_seen}"
    )
