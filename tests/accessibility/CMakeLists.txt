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

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/..)

# These tests query Qt's accessibility bridge directly
# (QAccessible::queryAccessibleInterface()) against a real MainWindow, the
# same in-process interfaces Qt uses to answer AT-SPI2 (Linux), UI
# Automation/MSAA (Windows), and NSAccessibility (macOS) requests. That makes
# them fast and reliable in headless CI -- no running screen reader, D-Bus
# session, or platform AT registry required (QT_QPA_PLATFORM=offscreen is
# enough) -- on every platform KeePassXC supports.
#
# They intentionally do NOT prove that a given platform's native AT stack
# renders this tree correctly end-to-end; that is a separate, platform-specific
# concern covered by .github/workflows/accessibility-atspi.yml on Linux and
# tests/accessibility/windows/dump_uia_tree.py on Windows. Both layers matter:
# this suite guards the Qt-level accessibility tree that both native backends
# are fed from, so regressions are caught immediately without needing a full
# native AT round trip for every change.
add_unit_test(NAME testaccessibility SOURCES TestAccessibility.cpp ../util/TemporaryFile.cpp LIBS ${TEST_LIBRARIES})

if(WIN32)
    # CTest does not inherit the interactive PowerShell environment used to
    # build/run the tests. Keep the Qt runtime and headless platform settings
    # with the test so it can run directly through CTest on Windows.
    get_target_property(QT6_CORE_LOCATION Qt6::Core IMPORTED_LOCATION_DEBUG)
    if(QT6_CORE_LOCATION)
        get_filename_component(QT6_DEBUG_BIN_DIR "${QT6_CORE_LOCATION}" DIRECTORY)
        set_tests_properties(testaccessibility PROPERTIES
            ENVIRONMENT "LANG=en_US.UTF-8;QT_QPA_PLATFORM=offscreen;PATH=${QT6_DEBUG_BIN_DIR};$ENV{PATH}")
    endif()

    # Windows-native UI Automation tests, covering the native accessibility
    # layer this suite's in-process QAccessible tests above do not exercise.
    # See tests/accessibility/windows/CMakeLists.txt.
    add_subdirectory(windows)
endif()
