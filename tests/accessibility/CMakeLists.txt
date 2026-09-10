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
# Automation/MSAA (Windows), and NSAccessibility (macOS) requests. That makes
# them fast and reliable in headless CI -- no running screen reader, D-Bus
# session, or platform AT registry required (QT_QPA_PLATFORM=offscreen is
# enough) -- on every platform KeePassXC supports.
#
# They intentionally do NOT prove that a given platform's native AT stack
# renders this tree correctly end-to-end; that is a separate, platform-specific
# concern covered by .github/workflows/accessibility-atspi.yml on Linux and
# tools/dump_tree_windows.py on Windows. Both layers matter: this suite
# guards the Qt-level accessibility tree that both native backends are fed
# from, so regressions are caught immediately without needing a full native
# AT round trip for every change.
add_unit_test(NAME testaccessibility SOURCES TestAccessibility.cpp ../util/TemporaryFile.cpp LIBS ${TEST_LIBRARIES})

# Set environment for Qt accessibility tests to match CI.
# Use windows platform for Windows CI parity; suppress debug dialogs from
# Qt assertions in debug builds. Point QT_PLUGIN_PATH to the debug Qt plugins
# so the windows platform plugin (qwindowsd.dll) can be found.
set_tests_properties(testaccessibility PROPERTIES
    ENVIRONMENT "LANG=en_US.UTF-8;QT_QPA_PLATFORM=windows;QT_ACCESSIBILITY=1;QT_FATAL_WARNINGS=0;QT_DEBUG_PLUGINS=0;QT_LOGGING_RULES=*.debug=false;QT_PLUGIN_PATH=C:/Users/Miriam/Personal_Coding/keepassxc-repo/vcpkg/installed/x64-windows/debug/Qt6/plugins"
)

# Windows-native UI Automation tests, covering the native accessibility
# layer this suite's in-process QAccessible tests above do not exercise.
# See tests/accessibility/windows/CMakeLists.txt.
if(WIN32)
    add_subdirectory(windows)
endif()
