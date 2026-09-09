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

if(NOT WIN32)
    return()
endif()

# Unlike ../TestAccessibility.cpp, which queries QAccessible directly
# in-process, this suite launches the real KeePassXC.exe as a separate
# process and inspects it exactly as a screen reader or any other UI
# Automation client would: through the Windows UI Automation (UIA) COM API.
# It proves the whole chain -- Qt accessibility implementation -> Windows
# accessibility layer -> native client -- rather than re-testing Qt's
# QAccessible interfaces, which the cross-platform suite already covers.
#
# It only needs the already-built KeePassXC executable and the Windows SDK's
# UI Automation/COM libraries, both of which are always present alongside
# the MSVC toolchain, so it adds no new third-party or vcpkg dependency.
#
# "util/TemporaryFile.h" resolves via the include directory tests/CMakeLists.txt
# already adds for the whole tests/ tree, so no include_directories() is needed here.
add_unit_test(NAME testwindowsaccessibility
    SOURCES TestWindowsAccessibility.cpp ../../util/TemporaryFile.cpp
    LIBS Qt6::Core Qt6::Test Uiautomationcore.lib Ole32.lib Oleaut32.lib)

# testwindowsaccessibility does not link against ${PROGNAME} -- it starts it
# as an external process -- so this only hands it the executable's path.
# Building it still requires the ${PROGNAME} target to be part of the same
# build (as it already is for testcli/keepassxc-cli, above in ../../CMakeLists.txt).
target_compile_definitions(testwindowsaccessibility PRIVATE
    KEEPASSXC_EXECUTABLE_PATH="$<TARGET_FILE:${PROGNAME}>")
