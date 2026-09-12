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
#
#  You should have received a copy of the GNU General Public License
#  along with this program.  If not, see <http://www.gnu.org/licenses/>.

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/..)

# These tests query Qt's accessibility bridge directly
# (QAccessible::queryAccessibleInterface()) against a real MainWindow, the
# same in-process interfaces Qt uses to answer AT-SPI2 (Linux), UI
# Automation/MSAA (Windows), and NSAccessibility (macOS) requests. They make
# the Qt-level checks fast and reliable without requiring a running screen
# reader or native platform accessibility registry.
#
# They intentionally do NOT prove that a given platform's native AT stack
# renders this tree correctly end-to-end. That is a separate,
# platform-specific concern covered by the native UI Automation tests on
# Windows and future native AT-SPI coverage on Linux. Both layers matter:
# this suite guards the Qt-level accessibility tree that native backends are
# fed from, while native tests verify the platform accessibility boundary.
add_unit_test(NAME testaccessibility SOURCES TestAccessibility.cpp ../util/TemporaryFile.cpp LIBS ${TEST_LIBRARIES})

# Set environment for Qt accessibility tests. Keep platform selection in the
# workflow rather than embedding developer-specific Qt installation paths in
# the source tree. This makes the test portable across local checkouts and CI.
set_tests_properties(testaccessibility PROPERTIES
    ENVIRONMENT "LANG=en_US.UTF-8;QT_QPA_PLATFORM=windows;QT_ACCESSIBILITY=1;QT_FATAL_WARNINGS=0;QT_DEBUG_PLUGINS=0;QT_LOGGING_RULES=*.debug=false"
)

# Windows-native UI Automation tests, covering the native accessibility
# layer this suite's in-process QAccessible tests above do not exercise.
# See tests/accessibility/windows/CMakeLists.txt.
if(WIN32)
    add_subdirectory(windows)
endif()
