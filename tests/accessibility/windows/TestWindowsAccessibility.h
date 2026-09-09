/*
 *  Copyright (C) 2026 KeePassXC Team <team@keepassxc.org>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 or (at your option)
 *  version 3 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef KEEPASSX_TESTWINDOWSACCESSIBILITY_H
#define KEEPASSX_TESTWINDOWSACCESSIBILITY_H

#undef NOMINMAX
#define NOMINMAX
// Deliberately NOT defining WIN32_LEAN_AND_MEAN here: it strips <ole2.h>
// (and therefore <objbase.h>/<unknwn.h>) out of <windows.h>, which is
// where the `interface`/MIDL_INTERFACE macros that <UIAutomationCore.h>
// (pulled in below via <UIAutomation.h>) needs to parse its
// `interface IRawElementProviderSimple : public IUnknown { ... }` style
// declarations come from. Without them, `interface` is just a plain
// identifier and the header fails with "C2146: missing ';' before
// identifier" followed by a cascade of "C2371: redefinition" errors --
// exactly the failure this comment is here to prevent regressing.
// ../TestWindowsAccessibilityTree.cpp never sets this macro either
// (it lets Qt's own qt_windows.h include a full <windows.h>), which is
// why that target already builds cleanly.
#include <windows.h>

#include <UIAutomation.h>
#include <wrl/client.h>

#include <QObject>
#include <QProcess>
#include <QString>

/**
 * Windows-native accessibility regression tests for KeePassXC.
 *
 * These tests launch the real KeePassXC.exe under test as a separate
 * process and drive it purely through Microsoft's Windows UI Automation
 * (UIA) client API (IUIAutomation and friends) -- the same API JAWS, NVDA,
 * and Windows' own Narrator use. They deliberately do not touch
 * QAccessible or any KeePassXC/Qt headers in-process; that in-process
 * coverage already exists in ../TestAccessibility.cpp. This suite instead
 * verifies that Qt's accessibility implementation is actually reaching the
 * native Windows accessibility layer, end to end.
 *
 * No mouse coordinates, keyboard input, or JAWS/other AT product install is
 * required or used anywhere in this suite.
 */
class TestWindowsAccessibility : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();

    // Covers: 2) main window UIA Name is "KeePassXC", and
    //         3) main window ControlType is Window.
    void testMainWindowIdentity();

    // Covers: 4/7/9) find "Create Database" / "Open Database" / "Import
    //         File", 5/8/10) each is exposed as a Button, 6) "Create
    //         Database"'s UIA Name matches exactly, and 11) each is
    //         reported enabled with no database open.
    void testWelcomeScreenButtonsAccessible();

    // Covers: 12) basic UIA tree relationships -- each welcome-screen
    //         button is a genuine UIA descendant of the main window.
    void testWelcomeScreenButtonsTreeRelationships();

private:
    bool launchKeePassXC();
    HWND waitForTopLevelWindow(DWORD processId, int timeoutMs) const;
    // Not const: readAllStandardError() drains QProcess's internal stderr
    // buffer as a side effect, so this genuinely mutates m_process.
    QString processDiagnostics(const QString& context);

    // UIA helpers. These centralize retry/timeout handling and property
    // access so each test slot above reads as a short list of assertions,
    // the same shape ../TestAccessibility.cpp uses around
    // QAccessibleInterface.
    Microsoft::WRL::ComPtr<IUIAutomationElement> findDescendantByName(const QString& name, int timeoutMs) const;
    QString elementName(const Microsoft::WRL::ComPtr<IUIAutomationElement>& element) const;
    CONTROLTYPEID elementControlType(const Microsoft::WRL::ComPtr<IUIAutomationElement>& element) const;
    bool elementIsEnabled(const Microsoft::WRL::ComPtr<IUIAutomationElement>& element) const;
    bool isDescendantOfMainWindow(const Microsoft::WRL::ComPtr<IUIAutomationElement>& element) const;

    Microsoft::WRL::ComPtr<IUIAutomation> m_automation;
    Microsoft::WRL::ComPtr<IUIAutomationElement> m_mainWindowElement;
    QProcess m_process;
    HWND m_mainWindowHandle = nullptr;
    QString m_tempConfigPath;
};

#endif // KEEPASSX_TESTWINDOWSACCESSIBILITY_H
