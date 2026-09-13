from __future__ import annotations

import time
from collections import defaultdict, deque

import pytest
from gi.repository import Atspi


APP_NAME = "keepassxc"
CONNECT_TIMEOUT_S = 20
CONNECT_RETRY_INTERVAL_S = 0.5
TREE_WALK_TIMEOUT_S = 15
MAX_TREE_NODES = 2000

REQUIRED_CONTROLS = {
    "Create Database": "push button",
    "Open Database": "push button",
    "Import File": "push button",
}


def _children(node):
    """Return the accessible node's children using the GI Atspi API."""
    children = []
    try:
        count = node.get_child_count()
    except Exception:
        return children
    for index in range(count):
        try:
            child = node.get_child_at_index(index)
        except Exception:
            continue
        if child is not None:
            children.append(child)
    return children


def _applications():
    desktop = Atspi.get_desktop(0)
    if desktop is None:
        return []
    return _children(desktop)


@pytest.fixture(scope="module")
def atspi_session():
    Atspi.init()
    try:
        yield
    finally:
        Atspi.exit()


@pytest.fixture(scope="module")
def keepassxc_app(atspi_session):
    deadline = time.monotonic() + CONNECT_TIMEOUT_S
    while time.monotonic() < deadline:
        for app in _applications():
            try:
                if app.get_name() == APP_NAME:
                    return app
            except Exception:
                continue
        time.sleep(CONNECT_RETRY_INTERVAL_S)
    pytest.fail(
        f"KeePassXC was not exposed through AT-SPI within {CONNECT_TIMEOUT_S} seconds."
    )


def _walk_tree(root):
    """Walk the accessible tree breadth-first with time/node limits."""
    found = defaultdict(list)
    queue = deque([root])
    deadline = time.monotonic() + TREE_WALK_TIMEOUT_S
    visited = 0

    while queue and visited < MAX_TREE_NODES and time.monotonic() < deadline:
        node = queue.popleft()
        visited += 1
        try:
            name = node.get_name() or ""
        except Exception:
            name = ""
        if name:
            found[name].append(node)
        queue.extend(_children(node))

    return found


@pytest.fixture(scope="module")
def accessible_tree(keepassxc_app):
    return _walk_tree(keepassxc_app)


def _find_control(accessible_tree, expected_name, expected_role):
    for node in accessible_tree.get(expected_name, []):
        try:
            if node.get_role_name() == expected_role:
                return node
        except Exception:
            continue
    return None


def _state_names(node):
    try:
        state_set = node.get_state_set()
        return sorted(str(state).lower() for state in state_set.get_states())
    except Exception:
        return []


def _has_state(node, expected_state):
    return expected_state.lower() in _state_names(node)


def _interfaces(node):
    try:
        return list(node.get_interfaces())
    except Exception as exc:
        return [f"error: {exc}"]


def _action_count(node):
    try:
        return node.get_n_actions()
    except Exception:
        return 0


def _describe(node):
    try:
        return (
            f"[name={node.get_name()!r} role={node.get_role_name()!r} "
            f"states={_state_names(node)} interfaces={_interfaces(node)} "
            f"n_actions={_action_count(node)}]"
        )
    except Exception as exc:
        return f"[unable to describe accessible node: {exc}]"


def test_keepassxc_is_exposed_as_an_application(keepassxc_app):
    assert keepassxc_app.get_name() == APP_NAME


def test_accessible_tree_is_reachable(keepassxc_app):
    assert keepassxc_app.get_child_count() > 0


@pytest.mark.parametrize("expected_name", sorted(REQUIRED_CONTROLS))
def test_required_control_is_present(accessible_tree, expected_name):
    expected_role = REQUIRED_CONTROLS[expected_name]
    node = _find_control(accessible_tree, expected_name, expected_role)
    assert node is not None, (
        f"Expected '{expected_role}' named '{expected_name}' was not found in the "
        "KeePassXC accessibility tree."
    )


@pytest.mark.parametrize("expected_name", sorted(REQUIRED_CONTROLS))
def test_required_control_has_expected_role(accessible_tree, expected_name):
    expected_role = REQUIRED_CONTROLS[expected_name]
    node = _find_control(accessible_tree, expected_name, expected_role)
    assert node is not None
    assert node.get_role_name() == expected_role


@pytest.mark.parametrize("expected_name", sorted(REQUIRED_CONTROLS))
def test_required_control_has_accessible_name(accessible_tree, expected_name):
    expected_role = REQUIRED_CONTROLS[expected_name]
    node = _find_control(accessible_tree, expected_name, expected_role)
    assert node is not None
    assert node.get_name() == expected_name


@pytest.mark.parametrize("expected_name", sorted(REQUIRED_CONTROLS))
def test_required_control_exposes_action_interface(accessible_tree, expected_name):
    """Required controls must expose at least one AT-SPI action.

    get_n_actions() > 0 is the primary functional check: it verifies that
    an assistive technology can invoke the control through AT-SPI. The GI
    binding's get_interfaces() API returns short interface names such as
    "Action", not fully-qualified D-Bus interface names, so check that
    representation rather than comparing it with DBUS_INTERFACE_ACTION.
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

    interfaces = _interfaces(node)
    if interfaces and not any("error" in i for i in interfaces):
        assert "Action" in interfaces, (
            f"'{expected_name}' has {n_actions} action(s) but its AT-SPI interface list "
            f"does not include 'Action': {interfaces}. {_describe(node)}"
        )


@pytest.mark.parametrize("expected_name", sorted(REQUIRED_CONTROLS))
def test_required_control_is_enabled(accessible_tree, expected_name):
    """Required controls must be reported as enabled/sensitive."""
    expected_role = REQUIRED_CONTROLS[expected_name]
    node = _find_control(accessible_tree, expected_name, expected_role)
    assert node is not None
    assert _has_state(node, "enabled"), _describe(node)
    assert _has_state(node, "sensitive"), _describe(node)


@pytest.mark.parametrize("expected_name", sorted(REQUIRED_CONTROLS))
def test_required_control_is_focusable(accessible_tree, expected_name):
    """Required controls must be reachable by keyboard/AT focus."""
    expected_role = REQUIRED_CONTROLS[expected_name]
    node = _find_control(accessible_tree, expected_name, expected_role)
    assert node is not None
    assert _has_state(node, "focusable"), _describe(node)


@pytest.mark.parametrize("expected_name", sorted(REQUIRED_CONTROLS))
def test_required_control_parent_link_is_consistent(accessible_tree, expected_name):
    """The parent relation must point back to a node containing the control."""
    expected_role = REQUIRED_CONTROLS[expected_name]
    node = _find_control(accessible_tree, expected_name, expected_role)
    assert node is not None
    parent = node.get_parent()
    assert parent is not None
    assert node.get_index_in_parent() >= 0
    assert parent.get_child_at_index(node.get_index_in_parent()) is not None


@pytest.mark.parametrize("expected_name", sorted(REQUIRED_CONTROLS))
def test_required_control_belongs_to_keepassxc_application(keepassxc_app, accessible_tree, expected_name):
    expected_role = REQUIRED_CONTROLS[expected_name]
    node = _find_control(accessible_tree, expected_name, expected_role)
    assert node is not None
    application = node.get_application()
    assert application is not None
    assert application.get_name() == keepassxc_app.get_name()


def test_application_root_is_not_defunct(keepassxc_app):
    assert not _has_state(keepassxc_app, "defunct"), _describe(keepassxc_app)


def test_application_root_reports_a_toolkit_name(keepassxc_app):
    assert keepassxc_app.get_toolkit_name()


def test_application_root_exposes_accessible_interfaces(keepassxc_app):
    interfaces = _interfaces(keepassxc_app)
    assert interfaces
    assert "Accessible" in interfaces


@pytest.mark.parametrize("expected_name", sorted(REQUIRED_CONTROLS))
def test_required_control_full_accessibility_contract(accessible_tree, expected_name):
    """Run the core accessibility contract in one assertion-oriented check."""
    expected_role = REQUIRED_CONTROLS[expected_name]
    node = _find_control(accessible_tree, expected_name, expected_role)
    assert node is not None, _describe(node)
    assert node.get_name() == expected_name, _describe(node)
    assert node.get_role_name() == expected_role, _describe(node)
    assert _has_state(node, "enabled"), _describe(node)
    assert _has_state(node, "focusable"), _describe(node)
    assert _action_count(node) > 0, _describe(node)
    assert node.get_parent() is not None, _describe(node)
    assert node.get_application() is not None, _describe(node)
