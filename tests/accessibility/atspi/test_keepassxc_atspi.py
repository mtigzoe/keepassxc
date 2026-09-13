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


def _has_state(node, state_type):
    """Whether an accessible's AT-SPI state set contains the given state."""
    try:
        return bool(node.get_state_set().contains(state_type))
    except Exception:
        return False


def _state_names(node):
    """Human-readable state names for an accessible, for diagnostics only."""
    try:
        return sorted(s.value_nick for s in node.get_state_set().get_states())
    except Exception:
        return ["<error reading state set>"]


def _interfaces(node):
    """The AT-SPI D-Bus interfaces an accessible reports, for diagnostics
    and for the (secondary) Action-interface check below."""
    try:
        return list(node.get_interfaces())
    except Exception:
        return ["<error reading interfaces>"]


def _action_count(node):
    """Number of AT-SPI actions an accessible exposes, or 0 if unsupported/unavailable."""
    try:
        n = node.get_n_actions()
        return n if n is not None else 0
    except Exception:
        return 0


def _describe(node):
    """Compact diagnostic string: name, role, states, interfaces, action count.

    Used in every assertion message below so a failure shows what a
    screen reader would actually perceive (or fail to perceive) about
    the object, not just a bare True/False.
    """
    try:
        name = node.get_name()
    except Exception:
        name = "<error>"
    try:
        role = node.get_role_name()
    except Exception:
        role = "<error>"
    return (
        f"[name={name!r} role={role!r} states={_state_names(node)} "
        f"interfaces={_interfaces(node)} n_actions={_action_count(node)}]"
    )


def _find_control(accessible_tree, name, expected_role):
    """Return the accessible named `name` that also has `expected_role`.

    A required control's name can legitimately be shared by an unrelated
    tree node (e.g. a menu action with the same text as a Welcome-screen
    button); tests that check a role-specific property (state, actions,
    parent link, ...) must anchor on the node that actually has the
    expected role rather than an arbitrary same-named one. Returns None
    if no candidate has that role -- callers should still assert and
    report accessible_tree.get(name, []) for diagnostics in that case.
    """
    for node in accessible_tree.get(name, []):
        try:
            role = node.get_role_name()
        except Exception:
            continue
        if role == expected_role:
            return node
    return None


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


@pytest.mark.parametrize("expected_name", sorted(REQUIRED_CONTROLS))
def test_required_control_has_accessible_name(accessible_tree, expected_name):
    """The accessible name must be exactly the expected text.

    This is a different check from "is present in the tree": that check
    finds the node *by* this name, so it can't catch the name itself
    being wrong. This confirms Qt's mnemonic marker ('&') and any
    accidental leading/trailing whitespace never leak into the name a
    screen reader would actually announce.
    """
    expected_role = REQUIRED_CONTROLS[expected_name]
    node = _find_control(accessible_tree, expected_name, expected_role)
    assert node is not None, (
        f"No '{expected_role}' named '{expected_name}' found to check its accessible name "
        "(see test_required_control_has_expected_role for the role-level failure)."
    )

    name = node.get_name()
    assert name == expected_name, (
        f"Accessible name was {name!r}, expected exactly {expected_name!r} -- a mnemonic "
        f"marker or stray whitespace may be leaking into the AT-SPI name. {_describe(node)}"
    )


@pytest.mark.parametrize("expected_name", sorted(REQUIRED_CONTROLS))
def test_required_control_exposes_action_interface(accessible_tree, expected_name):
    """Required controls must expose at least one AT-SPI action.

    get_n_actions() > 0 is what actually lets an AT invoke the control
    (Orca's "click" via the Action interface), as opposed to merely
    announcing it -- a control can keep its name and role while losing
    this and still look fine in a superficial check.
    """
    expected_role = REQUIRED_CONTROLS[expected_name]
    node = _find_control(accessible_tree, expected_name, expected_role)
    assert node is not None, (
        f"No '{expected_role}' named '{expected_name}' found to check for an action interface."
    )

    n_actions = _action_count(node)
    assert n_actions > 0, (
        f"'{expected_name}' exposes no AT-SPI actions (get_n_actions() == {n_actions}); "
        f"an assistive technology could announce it but not activate it. {_describe(node)}"
    )
    # DBUS_INTERFACE_ACTION ("org.a11y.atspi.Action") is the interfaces()-list
    # was verified against the actual installed bindings; get_n_actions() > 0
    # above is the primary, unambiguous signal, and this membership check is
    # kept secondary since it depends on the exact interface-name strings
    # get_interfaces() returns, which weren't reachable to confirm end-to-end
    # in this sandbox (see the accompanying report).
    interfaces = _interfaces(node)
    if interfaces and not any("error" in i for i in interfaces):
        assert Atspi.DBUS_INTERFACE_ACTION in interfaces, (
            f"'{expected_name}' has {n_actions} action(s) but its AT-SPI interface list "
            f"does not include {Atspi.DBUS_INTERFACE_ACTION!r}: {interfaces}. {_describe(node)}"
        )


@pytest.mark.parametrize("expected_name", sorted(REQUIRED_CONTROLS))
def test_required_control_is_enabled(accessible_tree, expected_name):
    """Required controls must be reported as enabled/sensitive.

    A control that is present, named, and has the right role but is
    reported SENSITIVE=False or ENABLED=False would be announced by a
    screen reader as greyed out / unusable -- effectively invisible to
    someone who can't see that it's merely styled to look disabled.
    """
    expected_role = REQUIRED_CONTROLS[expected_name]
    node = _find_control(accessible_tree, expected_name, expected_role)
    assert node is not None, f"No '{expected_role}' named '{expected_name}' found to check state."

    assert _has_state(node, Atspi.StateType.SENSITIVE), (
        f"'{expected_name}' is not SENSITIVE. {_describe(node)}"
    )
    assert _has_state(node, Atspi.StateType.ENABLED), (
        f"'{expected_name}' is not ENABLED. {_describe(node)}"
    )


@pytest.mark.parametrize("expected_name", sorted(REQUIRED_CONTROLS))
def test_required_control_is_focusable(accessible_tree, expected_name):
    """Required controls must be reported as focusable.

    Keyboard/screen-reader users navigate by focus, not by mouse
    position; a button that is visible and enabled but not FOCUSABLE is
    unreachable to them even though a sighted mouse user would never
    notice anything wrong.
    """
    expected_role = REQUIRED_CONTROLS[expected_name]
    node = _find_control(accessible_tree, expected_name, expected_role)
    assert node is not None, f"No '{expected_role}' named '{expected_name}' found to check state."

    assert _has_state(node, Atspi.StateType.FOCUSABLE), (
        f"'{expected_name}' is not FOCUSABLE. {_describe(node)}"
    )


@pytest.mark.parametrize("expected_name", sorted(REQUIRED_CONTROLS))
def test_required_control_parent_link_is_consistent(accessible_tree, expected_name):
    """A required control's AT-SPI parent link must round-trip correctly.

    Real assistive technology walks the tree via parent/child links (and
    reports position, e.g. "button 2 of 3"), so a control that is
    reachable in a top-down walk but whose own get_parent()/
    get_index_in_parent() do not agree with that parent's children would
    still confuse an AT doing its own top-down-then-verify navigation,
    even though this suite's own top-down walk already found it.
    """
    expected_role = REQUIRED_CONTROLS[expected_name]
    node = _find_control(accessible_tree, expected_name, expected_role)
    assert node is not None, f"No '{expected_role}' named '{expected_name}' found to check its parent link."

    try:
        parent = node.get_parent()
    except Exception as exc:
        parent = None
        parent_error = str(exc)
    else:
        parent_error = None
    assert parent is not None, (
        f"'{expected_name}' has no AT-SPI parent (get_parent() returned None"
        + (f", raised {parent_error}" if parent_error else "")
        + f"). {_describe(node)}"
    )

    try:
        index = node.get_index_in_parent()
    except Exception:
        index = -1
    assert index is not None and index >= 0, (
        f"'{expected_name}' reports an invalid get_index_in_parent() ({index}). {_describe(node)}"
    )

    sibling_at_index = None
    try:
        sibling_at_index = parent.get_child_at_index(index)
    except Exception:
        sibling_at_index = None
    assert sibling_at_index is not None, (
        f"'{expected_name}''s reported parent (name={parent.get_name()!r}, "
        f"role={parent.get_role_name()!r}) has no child at index {index}."
    )
    assert sibling_at_index.get_name() == expected_name and sibling_at_index.get_role_name() == expected_role, (
        f"'{expected_name}''s parent's child at index {index} does not match: got "
        f"name={sibling_at_index.get_name()!r} role={sibling_at_index.get_role_name()!r}. "
        "The parent/child relationship is inconsistent, which would confuse an AT navigating "
        "by position (e.g. Orca announcing 'item N of M')."
    )


@pytest.mark.parametrize("expected_name", sorted(REQUIRED_CONTROLS))
def test_required_control_belongs_to_keepassxc_application(accessible_tree, keepassxc_app, expected_name):
    """A required control's owning application must resolve back to KeePassXC.

    This is the other half of the parent/child story: not just "does
    this node have *a* parent" but "does walking up from it land back on
    the same application a screen reader already announced when it
    switched focus to KeePassXC".
    """
    expected_role = REQUIRED_CONTROLS[expected_name]
    node = _find_control(accessible_tree, expected_name, expected_role)
    assert node is not None, f"No '{expected_role}' named '{expected_name}' found to check its owning application."

    try:
        owning_app = node.get_application()
    except Exception as exc:
        pytest.fail(f"'{expected_name}'.get_application() raised {exc!r}. {_describe(node)}")

    assert owning_app is not None, f"'{expected_name}' reports no owning application. {_describe(node)}"
    owning_name = owning_app.get_name() or ""
    assert owning_name.lower() == APP_NAME.lower(), (
        f"'{expected_name}' reports owning application {owning_name!r}, expected {APP_NAME!r}."
    )


def test_application_root_is_not_defunct(keepassxc_app):
    """The application root itself must not be reported as defunct.

    DEFUNCT means the underlying object is gone; every other test in
    this module assumes the application root is still a live, queryable
    object, so this failing is the first thing to check before any
    control-level failure above is taken at face value.
    """
    assert not _has_state(keepassxc_app, Atspi.StateType.DEFUNCT), (
        f"KeePassXC's application accessible object is DEFUNCT. {_describe(keepassxc_app)}"
    )


def test_application_root_reports_a_toolkit_name(keepassxc_app):
    """Real ATs use the reported toolkit name/version for toolkit-specific
    quirks handling (Orca does this for Qt vs. GTK apps); an application
    root that reports no toolkit name at all degrades that handling.
    """
    toolkit_name = keepassxc_app.get_toolkit_name()
    assert toolkit_name, (
        f"KeePassXC's application object reports no AT-SPI toolkit name. {_describe(keepassxc_app)}"
    )


def test_application_root_exposes_accessible_interfaces(keepassxc_app):
    """The application root should expose at least one AT-SPI interface.

    A bare empty interface list on the root -- distinct from
    test_accessible_tree_is_reachable, which checks that children exist
    at all -- would mean even basic Accessible-level introspection isn't
    being offered for the application object itself.
    """
    interfaces = _interfaces(keepassxc_app)
    assert interfaces and not all("error" in i for i in interfaces), (
        f"KeePassXC's application object exposes no usable AT-SPI interfaces. "
        f"{_describe(keepassxc_app)}"
    )


@pytest.mark.parametrize("expected_name", sorted(REQUIRED_CONTROLS))
def test_required_control_full_accessibility_contract(accessible_tree, expected_name):
    """Regression guard: catches a control keeping its *name* while silently
    losing everything that makes it actually usable via AT-SPI.

    The tests above isolate role / action / state / parent-link failures
    individually so a break is easy to diagnose. This test re-checks all
    of them together against the exact same node and reports every
    failing property in one message, so a future find/replace-style
    Qt change (e.g. one that swaps a QPushButton for a bare QWidget with
    the same text) is caught as a single, unambiguous "this control lost
    its accessibility contract" failure rather than only surfacing as a
    handful of unrelated-looking test failures elsewhere in this file.
    """
    expected_role = REQUIRED_CONTROLS[expected_name]
    node = _find_control(accessible_tree, expected_name, expected_role)
    assert node is not None, (
        f"'{expected_name}' has no accessible object with role '{expected_role}' at all -- "
        f"names found for '{expected_name}': "
        f"{[c.get_role_name() for c in accessible_tree.get(expected_name, [])]}"
    )

    failures = []
    if node.get_name() != expected_name:
        failures.append(f"name is {node.get_name()!r}, expected {expected_name!r}")
    if not _has_state(node, Atspi.StateType.SENSITIVE):
        failures.append("missing SENSITIVE state")
    if not _has_state(node, Atspi.StateType.ENABLED):
        failures.append("missing ENABLED state")
    if not _has_state(node, Atspi.StateType.FOCUSABLE):
        failures.append("missing FOCUSABLE state")
    if _action_count(node) <= 0:
        failures.append(f"has {_action_count(node)} AT-SPI actions, expected at least 1")

    assert not failures, (
        f"'{expected_name}' kept its name and role but lost part of its accessibility "
        f"contract: {'; '.join(failures)}. {_describe(node)}"
    )
