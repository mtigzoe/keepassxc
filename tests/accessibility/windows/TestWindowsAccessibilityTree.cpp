/*
 *  Copyright (C) 2026 KeePassXC Team <team@keepassxc.org>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 or (at your option)
 *  version 3 of the License.
 */

#include "util/TemporaryFile.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSettings>
#include <QStringList>
#include <QTest>

#include <UIAutomation.h>
#include <oleauto.h>
#include <wrl/client.h>

#include <functional>

#ifndef KEEPASSXC_EXECUTABLE_PATH
#error "KEEPASSXC_EXECUTABLE_PATH must be defined by CMake"
#endif

namespace
{
constexpr int StartTimeoutMs = 15000;
constexpr int WindowTimeoutMs = 30000;
constexpr int PollIntervalMs = 200;

struct WindowLookup
{
    DWORD processId = 0;
    HWND window = nullptr;
};

BOOL CALLBACK findProcessWindow(HWND hwnd, LPARAM data)
{
    auto* lookup = reinterpret_cast<WindowLookup*>(data);
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == lookup->processId && IsWindowVisible(hwnd) && GetWindow(hwnd, GW_OWNER) == nullptr) {
        lookup->window = hwnd;
        return FALSE;
    }
    return TRUE;
}

QString elementName(IUIAutomationElement* element)
{
    if (!element) {
        return {};
    }

    BSTR value = nullptr;
    if (FAILED(element->get_CurrentName(&value)) || !value) {
        return {};
    }

    const QString result = QString::fromWCharArray(value);
    SysFreeString(value);
    return result;
}

CONTROLTYPEID controlType(IUIAutomationElement* element)
{
    CONTROLTYPEID value = 0;
    if (element) {
        element->get_CurrentControlType(&value);
    }
    return value;
}

bool hasPattern(IUIAutomationElement* element, PATTERNID patternId)
{
    if (!element) {
        return false;
    }

    Microsoft::WRL::ComPtr<IUnknown> pattern;
    return SUCCEEDED(element->GetCurrentPattern(patternId, &pattern)) && pattern;
}

bool isInteractiveControl(CONTROLTYPEID type)
{
    switch (type) {
    case UIA_ButtonControlTypeId:
    case UIA_CheckBoxControlTypeId:
    case UIA_ComboBoxControlTypeId:
    case UIA_EditControlTypeId:
    case UIA_HyperlinkControlTypeId:
    case UIA_ListControlTypeId:
    case UIA_ListItemControlTypeId:
    case UIA_MenuItemControlTypeId:
    case UIA_RadioButtonControlTypeId:
    case UIA_SliderControlTypeId:
    case UIA_SpinnerControlTypeId:
    case UIA_TabItemControlTypeId:
    case UIA_TreeControlTypeId:
    case UIA_TreeItemControlTypeId:
        return true;
    default:
        return false;
    }
}

class WindowsUiAutomationTreeTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void testMainWindowProperties();
    void testControlViewIsNonEmpty();
    void testInteractiveControlsHaveNames();
    void testCoreControlPatterns();

private:
    Microsoft::WRL::ComPtr<IUIAutomationElement> findByName(const QString& name) const;
    Microsoft::WRL::ComPtr<IUIAutomationElement> findByTypeAndName(const QString& name,
                                                                    CONTROLTYPEID type) const;
    HWND waitForWindow(DWORD processId) const;
    bool enumerateControlView(IUIAutomationElement* root,
                              int* total,
                              int* unnamedInteractive,
                              QStringList* unnamedElements) const;

    QProcess m_process;
    QString m_tempConfigPath;
    Microsoft::WRL::ComPtr<IUIAutomation> m_automation;
    Microsoft::WRL::ComPtr<IUIAutomationElement> m_mainWindow;
};

void WindowsUiAutomationTreeTest::initTestCase()
{
    const HRESULT initResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    QVERIFY2(SUCCEEDED(initResult), "CoInitializeEx failed");

    HRESULT hr = CoCreateInstance(CLSID_CUIAutomation,
                                  nullptr,
                                  CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&m_automation));
    QVERIFY2(SUCCEEDED(hr) && m_automation, "Could not create the Windows UI Automation client");

    // TemporaryFile (tests/util/TemporaryFile.h) reserves a unique path via
    // a short-lived, function-local QTemporaryFile and returns a plain
    // QFile-backed path -- unlike a QTemporaryFile kept open for the
    // duration of the test (as this file previously did with an `m_config`
    // member), it does not hold the file open internally afterward. A
    // QTemporaryFile's close() does not release that internal handle -- the
    // file "will exist and be kept open internally by QTemporaryFile" for as
    // long as the QTemporaryFile object itself is alive (Qt docs) -- so
    // QSettings could never get exclusive write access to it and
    // settings.sync() reliably failed with AccessError on Windows. This is
    // the same isolated-config approach already used successfully by
    // ../TestWindowsAccessibility.cpp and (in-process, via
    // Config::createConfigFromFile()) by ../TestAccessibility.cpp.
    m_tempConfigPath = TemporaryFile::createTempConfigFile();
    QVERIFY2(!m_tempConfigPath.isEmpty(), "Could not create the temporary KeePassXC configuration");

    QSettings settings(m_tempConfigPath, QSettings::IniFormat);
    settings.setValue("UpdateCheckMessageShown", true);
    settings.setValue("SingleInstance", false);
    settings.setValue("GUI/MinimizeOnStartup", false);
    settings.setValue("GUI/ShowTrayIcon", false);
    settings.sync();
    QVERIFY2(settings.status() == QSettings::NoError, "Could not initialize the temporary KeePassXC configuration");

    const QString executable = QStringLiteral(KEEPASSXC_EXECUTABLE_PATH);
    QVERIFY2(QFileInfo::exists(executable), qPrintable(QString("KeePassXC.exe not found: %1").arg(executable)));

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert("QT_ACCESSIBILITY", "1");
    environment.insert("QT_QPA_PLATFORM", "windows");
    m_process.setProcessEnvironment(environment);
    m_process.setWorkingDirectory(QFileInfo(executable).absolutePath());
    m_process.setProgram(executable);
    m_process.setArguments({QStringLiteral("--config"), m_tempConfigPath});
    m_process.start();
    QVERIFY2(m_process.waitForStarted(StartTimeoutMs), "KeePassXC.exe did not start");

    const HWND hwnd = waitForWindow(static_cast<DWORD>(m_process.processId()));
    QVERIFY2(hwnd != nullptr, "KeePassXC did not create a visible top-level window");

    hr = m_automation->ElementFromHandle(hwnd, &m_mainWindow);
    QVERIFY2(SUCCEEDED(hr) && m_mainWindow, "UI Automation could not wrap the KeePassXC main window");
}

void WindowsUiAutomationTreeTest::cleanupTestCase()
{
    m_mainWindow.Reset();
    m_automation.Reset();

    if (m_process.state() != QProcess::NotRunning) {
        m_process.terminate();
        if (!m_process.waitForFinished(10000)) {
            m_process.kill();
            m_process.waitForFinished(10000);
        }
    }

    CoUninitialize();
}

HWND WindowsUiAutomationTreeTest::waitForWindow(DWORD processId) const
{
    QElapsedTimer timer;
    timer.start();

    while (timer.elapsed() < WindowTimeoutMs) {
        WindowLookup lookup{processId, nullptr};
        EnumWindows(findProcessWindow, reinterpret_cast<LPARAM>(&lookup));
        if (lookup.window) {
            return lookup.window;
        }

        QTest::qWait(PollIntervalMs);
        if (m_process.state() == QProcess::NotRunning) {
            return nullptr;
        }
    }

    return nullptr;
}

Microsoft::WRL::ComPtr<IUIAutomationElement>
WindowsUiAutomationTreeTest::findByName(const QString& name) const
{
    VARIANT value;
    VariantInit(&value);
    value.vt = VT_BSTR;
    value.bstrVal = SysAllocString(reinterpret_cast<const wchar_t*>(name.utf16()));
    if (!value.bstrVal) {
        return nullptr;
    }

    Microsoft::WRL::ComPtr<IUIAutomationCondition> condition;
    const HRESULT conditionResult = m_automation->CreatePropertyCondition(UIA_NamePropertyId, value, &condition);
    VariantClear(&value);
    if (FAILED(conditionResult) || !condition) {
        return nullptr;
    }

    Microsoft::WRL::ComPtr<IUIAutomationElement> result;
    const HRESULT findResult = m_mainWindow->FindFirst(TreeScope_Descendants, condition.Get(), &result);
    return SUCCEEDED(findResult) ? result : nullptr;
}

Microsoft::WRL::ComPtr<IUIAutomationElement>
WindowsUiAutomationTreeTest::findByTypeAndName(const QString& name, CONTROLTYPEID type) const
{
    auto result = findByName(name);
    if (!result || controlType(result.Get()) != type) {
        return nullptr;
    }
    return result;
}

bool WindowsUiAutomationTreeTest::enumerateControlView(IUIAutomationElement* root,
                                                        int* total,
                                                        int* unnamedInteractive,
                                                        QStringList* unnamedElements) const
{
    Microsoft::WRL::ComPtr<IUIAutomationTreeWalker> walker;
    if (FAILED(m_automation->get_ControlViewWalker(&walker)) || !walker) {
        return false;
    }

    std::function<bool(IUIAutomationElement*)> walk = [&](IUIAutomationElement* element) {
        if (!element) {
            return true;
        }

        ++(*total);
        const CONTROLTYPEID type = controlType(element);
        const QString name = elementName(element);
        if (isInteractiveControl(type) && name.trimmed().isEmpty()) {
            ++(*unnamedInteractive);
            if (unnamedElements->size() < 20) {
                unnamedElements->append(QStringLiteral("control type %1").arg(type));
            }
        }

        Microsoft::WRL::ComPtr<IUIAutomationElement> child;
        if (FAILED(walker->GetFirstChildElement(element, &child))) {
            return false;
        }

        while (child) {
            if (!walk(child.Get())) {
                return false;
            }
            Microsoft::WRL::ComPtr<IUIAutomationElement> sibling;
            if (FAILED(walker->GetNextSiblingElement(child.Get(), &sibling))) {
                return false;
            }
            child = sibling;
        }

        return true;
    };

    return walk(root);
}

void WindowsUiAutomationTreeTest::testMainWindowProperties()
{
    QCOMPARE(elementName(m_mainWindow.Get()), QStringLiteral("KeePassXC"));
    QCOMPARE(controlType(m_mainWindow.Get()), static_cast<CONTROLTYPEID>(UIA_WindowControlTypeId));

    BOOL enabled = FALSE;
    QVERIFY(SUCCEEDED(m_mainWindow->get_CurrentIsEnabled(&enabled)));
    QVERIFY(enabled);

    BOOL offscreen = TRUE;
    QVERIFY(SUCCEEDED(m_mainWindow->get_CurrentIsOffscreen(&offscreen)));
    QVERIFY(!offscreen);
}

void WindowsUiAutomationTreeTest::testControlViewIsNonEmpty()
{
    int total = 0;
    int unnamedInteractive = 0;
    QStringList unnamedElements;
    QVERIFY2(enumerateControlView(m_mainWindow.Get(), &total, &unnamedInteractive, &unnamedElements),
             "Could not enumerate the Windows UI Automation control view");
    QVERIFY2(total >= 5, qPrintable(QString("Unexpectedly small UI Automation control tree: %1 elements").arg(total)));
}

void WindowsUiAutomationTreeTest::testInteractiveControlsHaveNames()
{
    int total = 0;
    int unnamedInteractive = 0;
    QStringList unnamedElements;
    QVERIFY(enumerateControlView(m_mainWindow.Get(), &total, &unnamedInteractive, &unnamedElements));

    QVERIFY2(unnamedInteractive == 0,
             qPrintable(QString("%1 interactive UI Automation elements have no accessible name: %2")
                            .arg(unnamedInteractive)
                            .arg(unnamedElements.join(QStringLiteral(", ")))));
}

void WindowsUiAutomationTreeTest::testCoreControlPatterns()
{
    struct ExpectedControl
    {
        const char* name;
        CONTROLTYPEID type;
        PATTERNID pattern;
    };

    const ExpectedControl controls[] = {
        {"Create Database", UIA_ButtonControlTypeId, UIA_InvokePatternId},
        {"Open Database", UIA_ButtonControlTypeId, UIA_InvokePatternId},
        {"Import File", UIA_ButtonControlTypeId, UIA_InvokePatternId},
    };

    for (const auto& expected : controls) {
        auto element = findByTypeAndName(QString::fromLatin1(expected.name), expected.type);
        QVERIFY2(element,
                 qPrintable(QString("Could not find UI Automation control \"%1\"")
                                .arg(QString::fromLatin1(expected.name))));
        QVERIFY2(hasPattern(element.Get(), expected.pattern),
                 qPrintable(QString("UI Automation element \"%1\" does not expose the expected control pattern")
                                .arg(QString::fromLatin1(expected.name))));
    }
}
} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    app.setApplicationName("testwindowsaccessibilitytree");
    WindowsUiAutomationTreeTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "TestWindowsAccessibilityTree.moc"
