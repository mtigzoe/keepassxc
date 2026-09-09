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

#include "TestWindowsAccessibility.h"

#include "util/TemporaryFile.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QSettings>
#include <QTest>

#include <oleauto.h>

#ifndef KEEPASSXC_EXECUTABLE_PATH
#error "KEEPASSXC_EXECUTABLE_PATH must be defined by CMake -- see tests/accessibility/windows/CMakeLists.txt"
#endif

namespace
{
    // Generous, CI-friendly timeouts. GitHub-hosted Windows runners can be
    // slow to cold-start a freshly built Qt binary (antivirus scanning,
    // shared runner load), so these favor reliability over a fast failure.
    constexpr int ProcessStartTimeoutMs = 15000;
    constexpr int ProcessStopTimeoutMs = 10000;
    constexpr int WindowTimeoutMs = 30000;
    constexpr int ElementTimeoutMs = 15000;
    constexpr int PollIntervalMs = 200;

    struct FindWindowContext
    {
        DWORD processId = 0;
        HWND result = nullptr;
    };

    BOOL CALLBACK enumWindowsForProcess(HWND hwnd, LPARAM lParam)
    {
        auto* context = reinterpret_cast<FindWindowContext*>(lParam);

        DWORD windowProcessId = 0;
        GetWindowThreadProcessId(hwnd, &windowProcessId);
        if (windowProcessId != context->processId) {
            return TRUE; // keep enumerating
        }

        // Only consider genuine top-level, visible windows -- not owned
        // popups/tooltips, and not windows that have already been hidden
        // (e.g. a splash screen that already closed itself).
        if (!IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER) != nullptr) {
            return TRUE;
        }

        context->result = hwnd;
        return FALSE; // stop -- found it
    }
} // namespace

// Looks up `name` in KeePassXC's UI Automation tree (scoped to descendants
// of the main window) and fails the enclosing test slot if it cannot be
// found within ElementTimeoutMs. Declares a local `element`, so each use
// within one scope needs a distinct name. Only usable directly inside a
// void-returning test slot, since QVERIFY2's early "return;" does not
// compile in a function with a non-void return type.
// name is passed through as a runtime const char* (see call sites below,
// which loop over an array), not a compile-time literal, so this must use
// QString::fromLatin1() rather than QStringLiteral() -- the same choice
// ../TestWindowsAccessibilityTree.cpp makes for the identical situation.
#define VERIFY_UIA_ELEMENT(element, name)                                                                            \
    auto element = findDescendantByName(QString::fromLatin1(name), ElementTimeoutMs);                                \
    QVERIFY2(element,                                                                                                \
             qPrintable(QString("UI Automation did not find a \"%1\" element in KeePassXC's accessible tree "       \
                                 "within %2 ms")                                                                     \
                            .arg(name)                                                                               \
                            .arg(ElementTimeoutMs)))

int main(int argc, char* argv[])
{
    // A plain QCoreApplication is enough: unlike ../TestAccessibility.cpp,
    // this suite never constructs a QWidget or touches QAccessible
    // in-process -- KeePassXC itself runs as a separate process below, and
    // this process is purely a UI Automation client of it. QCoreApplication
    // still gives QProcess and QTest::qWait() the event loop they need.
    QCoreApplication app(argc, argv);
    app.setApplicationName("testwindowsaccessibility");
    QTEST_SET_MAIN_SOURCE_PATH
    TestWindowsAccessibility tc;
    return QTest::qExec(&tc, argc, argv);
}

void TestWindowsAccessibility::initTestCase()
{
    // This suite never registers UIA event handlers and never receives
    // inbound COM calls -- every call below is a synchronous, outgoing
    // request answered by KeePassXC's own accessibility provider thread.
    // A multithreaded apartment is therefore sufficient and avoids the
    // Windows message-pump requirement a single-threaded apartment would
    // otherwise impose on this console test executable.
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    QVERIFY2(SUCCEEDED(hr),
             qPrintable(QString("CoInitializeEx failed: 0x%1").arg(static_cast<uint>(hr), 8, 16, QLatin1Char('0'))));

    hr = CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&m_automation));
    QVERIFY2(SUCCEEDED(hr) && m_automation,
             "Failed to create the IUIAutomation COM object -- is UI Automation available on this runner?");

    QVERIFY2(launchKeePassXC(), "Failed to launch KeePassXC.exe for UI Automation testing");

    // === Find the KeePassXC main window through UI Automation. ===
    m_mainWindowHandle = waitForTopLevelWindow(static_cast<DWORD>(m_process.processId()), WindowTimeoutMs);
    QVERIFY2(m_mainWindowHandle != nullptr,
             qPrintable(processDiagnostics("No top-level window appeared for the KeePassXC.exe process")));

    hr = m_automation->ElementFromHandle(m_mainWindowHandle, &m_mainWindowElement);
    QVERIFY2(SUCCEEDED(hr) && m_mainWindowElement,
             "ElementFromHandle failed -- KeePassXC's main window exists natively, but UI Automation could not wrap "
             "it in an IUIAutomationElement");
}

void TestWindowsAccessibility::cleanupTestCase()
{
    // Release every UIA COM reference before CoUninitialize() and before
    // the process they point into is torn down, so nothing is ever
    // invoked against a stale remote object.
    m_mainWindowElement.Reset();
    m_automation.Reset();

    if (m_process.state() != QProcess::NotRunning) {
        m_process.terminate();
        if (!m_process.waitForFinished(ProcessStopTimeoutMs)) {
            m_process.kill();
            m_process.waitForFinished(ProcessStopTimeoutMs);
        }
    }

    CoUninitialize();
}

bool TestWindowsAccessibility::launchKeePassXC()
{
    // launchKeePassXC() returns bool rather than using QVERIFY2 internally
    // (QVERIFY2 contains a bare "return;" and only compiles in a
    // void-returning function) -- initTestCase() turns the bool result
    // into a proper QVERIFY2() failure with a diagnostic message.

    // An isolated, freshly created config file guarantees a deterministic
    // first-run state (welcome screen, no previously open database)
    // regardless of what is on the CI runner or a developer's machine --
    // the same guarantee ../TestAccessibility.cpp gets in-process via
    // Config::createConfigFromFile(). --config is KeePassXC's documented
    // CLI option for this (see src/main.cpp).
    m_tempConfigPath = TemporaryFile::createTempConfigFile();
    if (m_tempConfigPath.isEmpty()) {
        qWarning("Failed to create a temporary config file path");
        return false;
    }

    // Pre-seed a few settings directly, rather than relying only on the
    // CMake feature flags this particular KeePassXC.exe happened to be
    // built with, so the suite stays self-contained:
    //  - UpdateCheckMessageShown suppresses the modal "Check for updates on
    //    startup?" prompt. That prompt only exists when KPXC_FEATURE_UPDATES
    //    is enabled (the accessibility-windows.yml workflow disables it),
    //    but leaving it unset would mean the first window UI Automation
    //    finds is that dialog rather than the main window on any build
    //    that does have it enabled.
    //  - SingleInstance: Application::isAlreadyRunning() (src/gui/Application.cpp)
    //    is unconditionally false in Debug builds, which is what this
    //    workflow builds, but Release builds are not exempt. Disabling it
    //    here means this suite never silently hands off to, and exits in
    //    favor of, some other KeePassXC instance already running on the
    //    machine.
    //  - GUI_MinimizeOnStartup / GUI_ShowTrayIcon are set explicitly (their
    //    defaults already match) so the window is guaranteed to come up
    //    normal-sized and visible rather than minimized or tray-only --
    //    UI Automation can still see a minimized window, but a visible one
    //    removes a variable when diagnosing a CI failure.
    {
        QSettings seedSettings(m_tempConfigPath, QSettings::IniFormat);
        seedSettings.setValue("UpdateCheckMessageShown", true);
        seedSettings.setValue("SingleInstance", false);
        seedSettings.setValue("GUI/MinimizeOnStartup", false);
        seedSettings.setValue("GUI/ShowTrayIcon", false);
        seedSettings.sync();
        if (seedSettings.status() != QSettings::NoError) {
            qWarning("Failed to seed the temporary config file");
            return false;
        }
    }

    const QString executablePath = QStringLiteral(KEEPASSXC_EXECUTABLE_PATH);
    if (!QFileInfo::exists(executablePath)) {
        qWarning("KeePassXC executable not found at \"%s\" -- build it before running this test",
                 qPrintable(executablePath));
        return false;
    }

    // Force the accessibility bridge on unconditionally, rather than
    // relying on it activating lazily the first time a native AT client
    // queries -- this removes a possible race between process startup and
    // our first UI Automation call. Everything else is inherited from this
    // test process's own environment.
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert("QT_ACCESSIBILITY", "1");
    environment.insert("QT_QPA_PLATFORM", "windows");

    m_process.setProcessEnvironment(environment);
    m_process.setWorkingDirectory(QFileInfo(executablePath).absolutePath());
    m_process.setProgram(executablePath);
    m_process.setArguments({QStringLiteral("--config"), m_tempConfigPath});
    m_process.start();

    return m_process.waitForStarted(ProcessStartTimeoutMs);
}

HWND TestWindowsAccessibility::waitForTopLevelWindow(DWORD processId, int timeoutMs) const
{
    QElapsedTimer timer;
    timer.start();

    while (timer.elapsed() < timeoutMs) {
        FindWindowContext context;
        context.processId = processId;
        EnumWindows(enumWindowsForProcess, reinterpret_cast<LPARAM>(&context));
        if (context.result) {
            return context.result;
        }

        // QTest::qWait() sleeps and pumps this process's event loop, which
        // QProcess needs in order to notice the child exiting early (a
        // crash, or a single-instance hand-off that exits without ever
        // creating a window).
        QTest::qWait(PollIntervalMs);

        if (m_process.state() == QProcess::NotRunning) {
            break;
        }
    }

    return nullptr;
}

QString TestWindowsAccessibility::processDiagnostics(const QString& context)
{
    return QString("%1 (process state: %2, exit code: %3, stderr: %4)")
        .arg(context)
        .arg(m_process.state() == QProcess::Running ? "running" : "not running")
        .arg(m_process.exitCode())
        .arg(QString::fromLocal8Bit(m_process.readAllStandardError()).trimmed());
}

Microsoft::WRL::ComPtr<IUIAutomationElement> TestWindowsAccessibility::findDescendantByName(const QString& name,
                                                                                             int timeoutMs) const
{
    VARIANT nameVariant;
    VariantInit(&nameVariant);
    nameVariant.vt = VT_BSTR;
    nameVariant.bstrVal = SysAllocString(reinterpret_cast<const wchar_t*>(name.utf16()));
    if (!nameVariant.bstrVal) {
        return nullptr;
    }

    Microsoft::WRL::ComPtr<IUIAutomationCondition> nameCondition;
    HRESULT hr = m_automation->CreatePropertyCondition(UIA_NamePropertyId, nameVariant, &nameCondition);
    VariantClear(&nameVariant);
    if (FAILED(hr) || !nameCondition) {
        return nullptr;
    }

    QElapsedTimer timer;
    timer.start();
    do {
        Microsoft::WRL::ComPtr<IUIAutomationElement> found;
        hr = m_mainWindowElement->FindFirst(TreeScope_Descendants, nameCondition.Get(), &found);
        if (SUCCEEDED(hr) && found) {
            return found;
        }
        QTest::qWait(PollIntervalMs);
    } while (timer.elapsed() < timeoutMs);

    return nullptr;
}

QString TestWindowsAccessibility::elementName(const Microsoft::WRL::ComPtr<IUIAutomationElement>& element) const
{
    if (!element) {
        return {};
    }

    BSTR name = nullptr;
    if (FAILED(element->get_CurrentName(&name)) || !name) {
        return {};
    }

    QString result = QString::fromWCharArray(name);
    SysFreeString(name);
    return result;
}

CONTROLTYPEID
TestWindowsAccessibility::elementControlType(const Microsoft::WRL::ComPtr<IUIAutomationElement>& element) const
{
    CONTROLTYPEID controlType = 0;
    if (element) {
        element->get_CurrentControlType(&controlType);
    }
    return controlType;
}

bool TestWindowsAccessibility::elementIsEnabled(const Microsoft::WRL::ComPtr<IUIAutomationElement>& element) const
{
    BOOL enabled = FALSE;
    if (element) {
        element->get_CurrentIsEnabled(&enabled);
    }
    return enabled != FALSE;
}

bool TestWindowsAccessibility::isDescendantOfMainWindow(
    const Microsoft::WRL::ComPtr<IUIAutomationElement>& element) const
{
    if (!element || !m_mainWindowElement) {
        return false;
    }

    Microsoft::WRL::ComPtr<IUIAutomationTreeWalker> walker;
    if (FAILED(m_automation->get_RawViewWalker(&walker)) || !walker) {
        return false;
    }

    Microsoft::WRL::ComPtr<IUIAutomationElement> current = element;
    // Bound the walk in case of an unexpected cycle in the UIA tree -- the
    // same defensive pattern isAccessibleDescendantOf() in
    // ../TestAccessibility.cpp uses for the equivalent QAccessible walk.
    for (int i = 0; i < 64; ++i) {
        Microsoft::WRL::ComPtr<IUIAutomationElement> parent;
        if (FAILED(walker->GetParentElement(current.Get(), &parent)) || !parent) {
            return false;
        }

        BOOL same = FALSE;
        m_automation->CompareElements(parent.Get(), m_mainWindowElement.Get(), &same);
        if (same) {
            return true;
        }

        current = parent;
    }

    return false;
}

void TestWindowsAccessibility::testMainWindowIdentity()
{
    // === Verify its UIA Name is "KeePassXC". ===
    QCOMPARE(elementName(m_mainWindowElement), QString("KeePassXC"));

    // === Verify its ControlType is Window. ===
    QCOMPARE(elementControlType(m_mainWindowElement), static_cast<CONTROLTYPEID>(UIA_WindowControlTypeId));
}

void TestWindowsAccessibility::testWelcomeScreenButtonsAccessible()
{
    static const char* const buttonNames[] = {"Create Database", "Open Database", "Import File"};

    for (const char* buttonName : buttonNames) {
        // === Find "Create Database" / "Open Database" / "Import File". ===
        VERIFY_UIA_ELEMENT(element, buttonName);

        // === Verify it is exposed as a Button. ===
        QCOMPARE(elementControlType(element), static_cast<CONTROLTYPEID>(UIA_ButtonControlTypeId));

        // === Verify its UIA Name matches exactly. ===
        // (findDescendantByName() already located this element BY that
        // exact name, so this closes the loop and confirms UI Automation
        // reports the same name back rather than, say, a substring match.)
        QCOMPARE(elementName(element), QString(buttonName));

        // === Verify enabled/disabled state where appropriate. ===
        // All three welcome-screen buttons are expected to be enabled with
        // no database open -- see the equivalent Qt-level assertion in
        // testWelcomeScreenControlsAccessible() in ../TestAccessibility.cpp.
        QVERIFY2(elementIsEnabled(element),
                 qPrintable(QString("\"%1\" should be enabled per UI Automation").arg(buttonName)));
    }
}

void TestWindowsAccessibility::testWelcomeScreenButtonsTreeRelationships()
{
    // === Verify basic UIA tree relationships. ===
    static const char* const buttonNames[] = {"Create Database", "Open Database", "Import File"};

    for (const char* buttonName : buttonNames) {
        VERIFY_UIA_ELEMENT(element, buttonName);
        QVERIFY2(isDescendantOfMainWindow(element),
                 qPrintable(QString("\"%1\" should be a UI Automation descendant of the KeePassXC main window")
                                .arg(buttonName)));
    }
}
