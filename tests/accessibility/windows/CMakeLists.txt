#  Copyright (C) 2026 KeePassXC Team <team@keepassxc.org>
#
#  This program is free software: you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation, either version 2 or (at your option)
#  version 3 of the License.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.

if(NOT WIN32)
    return()
endif()

# These tests launch the real KeePassXC.exe as a separate process and inspect
# it through Microsoft's Windows UI Automation (UIA) COM API, rather than
# querying QAccessible in-process. This exercises the native Windows
# accessibility boundary consumed by assistive technology clients.

set(WINDOWS_UIA_LIBRARIES
    Qt6::Core
    Qt6::Test
    Uiautomationcore.lib
    Ole32.lib
    Oleaut32.lib)

add_unit_test(NAME testwindowsaccessibility
    SOURCES TestWindowsAccessibility.cpp ../../util/TemporaryFile.cpp
    LIBS ${WINDOWS_UIA_LIBRARIES})

target_compile_definitions(testwindowsaccessibility PRIVATE
    KEEPASSXC_EXECUTABLE_PATH="$<TARGET_FILE:${PROGNAME}>")

# Regression suite for the complete UI Automation control tree. In addition
# to checking named controls, this verifies that interactive controls expose
# actionable UIA control patterns and that the control view is structurally
# usable by assistive technology.
add_unit_test(NAME testwindowsaccessibilitytree
    SOURCES TestWindowsAccessibilityTree.cpp
    LIBS ${WINDOWS_UIA_LIBRARIES})

target_compile_definitions(testwindowsaccessibilitytree PRIVATE
    KEEPASSXC_EXECUTABLE_PATH="$<TARGET_FILE:${PROGNAME}>")
