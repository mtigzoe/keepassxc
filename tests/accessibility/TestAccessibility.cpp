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

#include "TestAccessibility.h"
#include "gui/Application.h"

#include <QAccessible>
#include <QAction>
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTest>
#include <QTimer>
#include <QToolBar>
#include <QTreeView>
#include <QtTest/QTestAccessibility>

#include "config-keepassx-tests.h"
#include "core/Config.h"
#include "core/Database.h"
#include "crypto/Crypto.h"
#include "gui/DatabaseTabWidget.h"
#include "gui/DatabaseWidget.h"
#include "gui/FileDialog.h"
#include "gui/MessageBox.h"
#include "gui/MessageWidget.h"
#include "gui/PasswordWidget.h"
#include "gui/entry/EditEntryWidget.h"
#include "gui/entry/EntryView.h"
#include "gui/tag/TagsEdit.h"

// Queries the accessible interface for `widget` and fails the current test
// slot (via QVERIFY2's bare "return;") if the widget or its accessible
// interface is missing or invalid. Declares a new local `iface` variable, so
// each use within the same scope needs a distinct name.
//
// This can only be used directly inside a function returning void (i.e. a
// test slot, or another helper written the same way) -- QVERIFY2's early
// return does not compile in a function with a non-void return type, which
// is why the plain lookups (queryAccessible(), findAccessibleChildByNamePrefix(),
// isAccessibleDescendantOf()) never call QVERIFY/QCOMPARE themselves.
#define VERIFY_ACCESSIBLE(iface, widget, context)                                                                     \
    QVERIFY2((widget) != nullptr, qPrintable(QString("Widget not found for: %1").arg(context)));                      \
    auto* iface = queryAccessible(widget);                                                                            \
    QVERIFY2(iface, qPrintable(QString("No accessible interface exposed for: %1").arg(context)));                     \
    QVERIFY2(iface->isValid(), qPrintable(QString("Accessible interface invalid for: %1").arg(context)))

int main(int argc, char* argv[])
{
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QGuiApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
    Application app(argc, argv);
    app.setApplicationName("KeePassXC");
    app.setApplicationVersion(KEEPASSXC_VERSION);
    app.setQuitOnLastWindowClosed(false);
    app.setAttribute(Qt::AA_Use96Dpi, true);
    app.applyTheme();
    QTEST_DISABLE_KEYPAD_NAVIGATION
    TestAccessibility tc;
    QTEST_SET_MAIN_SOURCE_PATH
    return QTest::qExec(&tc, argc, argv);
}

void TestAccessibility::initTestCase()
{
    QVERIFY(Crypto::init());

    QTestAccessibility::initialize();

    // Create temporary config file
    Config::createConfigFromFile(TemporaryFile::createTempConfigFile(), {});

    QLocale::setDefault(QLocale::c());
    Application::bootstrap();

    m_mainWindow.reset(new MainWindow());
    m_tabWidget = m_mainWindow->findChild<DatabaseTabWidget*>("tabWidget");
    m_mainWindow->show();
    m_mainWindow->resize(1024, 768);
}

// Every test starts with a reset config and no database open. Tests that
// need an open database call openTestDatabase() themselves as their first
// step; this lets testWelcomeScreenControlsAccessible() verify the
// no-database-open state, which a TestGui-style unconditional init() cannot.
void TestAccessibility::init()
{
    config()->resetToDefaults();
    config()->set(Config::AutoSaveAfterEveryChange, false);
    config()->set(Config::AutoSaveOnExit, false);
    config()->set(Config::UpdateCheckMessageShown, true);
    config()->set(Config::Security_QuickUnlock, false);
    config()->set(Config::UseAtomicSaves, false);
    config()->set(Config::GUI_ShowExpiredEntriesOnDatabaseUnlock, false);

    // Make sure the window is activated, or focus-dependent checks may fail
    // (this must happen before every test, not just ones that open a
    // database -- testWelcomeScreenControlsAccessible() checks focus too).
    m_mainWindow->activateWindow();
    QApplication::processEvents();

    m_dbOpen = false;
}

// Every test ends with closing the temp database (if one was opened) without saving.
void TestAccessibility::cleanup()
{
    closeTestDatabase();
}

void TestAccessibility::cleanupTestCase()
{
    m_dbFile.remove();
    QTestAccessibility::cleanup();
}

void TestAccessibility::triggerAction(const QString& name)
{
    auto* action = m_mainWindow->findChild<QAction*>(name);
    QVERIFY2(action, qPrintable(QString("Action doesn't exist: %1").arg(name)));
    QVERIFY2(action->isEnabled(), qPrintable(QString("Action is disabled: %1").arg(name)));
    action->trigger();
    QApplication::processEvents();
}

void TestAccessibility::openTestDatabase()
{
    if (m_dbOpen) {
        return;
    }

    // Copy the test database to a fresh temporary file for this test.
    auto origFilePath = QDir(KEEPASSX_TEST_DATA_DIR).absoluteFilePath("NewDatabase.kdbx");
    QVERIFY(m_dbFile.copyFromFile(origFilePath));
    m_dbFilePath = m_dbFile.fileName();

    fileDialog()->setNextFileName(m_dbFilePath);
    triggerAction("actionDatabaseOpen");
    QApplication::processEvents();

    m_dbWidget = m_tabWidget->currentDatabaseWidget();
    QVERIFY(m_dbWidget);

    auto* databaseOpenWidget = m_dbWidget->findChild<QWidget*>("databaseOpenWidget");
    QVERIFY(databaseOpenWidget);
    // editPassword is a PasswordWidget, not a QLineEdit directly.
    auto* editPassword =
        databaseOpenWidget->findChild<PasswordWidget*>("editPassword")->findChild<QLineEdit*>("passwordEdit");
    QVERIFY(editPassword);
    editPassword->setFocus();
    QTRY_VERIFY(editPassword->hasFocus());

    QTest::keyClicks(editPassword, "a");
    QTest::keyClick(editPassword, Qt::Key_Enter);

    QTRY_VERIFY(!m_dbWidget->isLocked());
    m_db = m_dbWidget->database();
    m_dbOpen = true;

    QApplication::processEvents();
}

void TestAccessibility::closeTestDatabase()
{
    if (!m_dbOpen) {
        return;
    }

    if (m_tabWidget->isVisible()) {
        // DO NOT save the database.
        if (m_db) {
            m_db->markAsClean();
        }
        MessageBox::setNextAnswer(MessageBox::No);
        triggerAction("actionDatabaseClose");
        QApplication::processEvents();
        MessageBox::setNextAnswer(MessageBox::NoButton);
        delete m_dbWidget;
    }

    m_dbOpen = false;
    m_db.reset();
}

QAccessibleInterface* TestAccessibility::queryAccessible(QWidget* widget) const
{
    if (!widget) {
        return nullptr;
    }
    return QAccessible::queryAccessibleInterface(widget);
    // Note: the returned interface is owned by Qt's internal accessibility
    // cache. It must NOT be deleted here.
}

QAccessibleInterface*
TestAccessibility::findAccessibleChildByNamePrefix(QAccessibleInterface* parent, const QString& prefix) const
{
    if (!parent) {
        return nullptr;
    }

    for (int i = 0; i < parent->childCount(); ++i) {
        auto* child = parent->child(i);
        if (!child) {
            continue;
        }

        if (child->text(QAccessible::Name).startsWith(prefix)) {
            return child;
        }

        if (auto* descendant = findAccessibleChildByNamePrefix(child, prefix)) {
            return descendant;
        }
    }

    return nullptr;
}

bool TestAccessibility::isAccessibleDescendantOf(QAccessibleInterface* candidate, QAccessibleInterface* ancestor) const
{
    if (!candidate || !ancestor) {
        return false;
    }

    QObject* ancestorObject = ancestor->object();
    QAccessibleInterface* current = candidate->parent();
    // Bound the walk in case of an unexpected cycle in the accessibility tree.
    for (int i = 0; current && i < 64; ++i) {
        if (current->object() == ancestorObject) {
            return true;
        }
        current = current->parent();
    }
    return false;
}

void TestAccessibility::testMainWindowAccessible()
{
    VERIFY_ACCESSIBLE(iface, m_mainWindow.data(), QStringLiteral("MainWindow"));

    QCOMPARE(iface->role(), QAccessible::Window);
    QVERIFY2(!iface->text(QAccessible::Name).isEmpty(), "MainWindow should expose a non-empty accessible name");
    QVERIFY2(!iface->state().invisible, "MainWindow should be visible to accessibility clients");
}

void TestAccessibility::testWelcomeScreenControlsAccessible()
{
    QVERIFY2(!m_dbOpen, "This test must run before any database is opened (see declaration order in the header)");

    auto* welcomeWidget = m_mainWindow->findChild<QWidget*>("welcomeWidget");
    QVERIFY(welcomeWidget);

    static const struct
    {
        const char* objectName;
        const char* expectedName;
    } controls[] = {
        {"buttonNewDatabase", "Create Database"},
        {"buttonOpenDatabase", "Open Database"},
        {"buttonImport", "Import File"},
    };

    for (const auto& control : controls) {
        auto* button = welcomeWidget->findChild<QPushButton*>(control.objectName);
        VERIFY_ACCESSIBLE(iface, button, QString(control.objectName));

        QCOMPARE(iface->role(), QAccessible::PushButton);
        QCOMPARE(iface->text(QAccessible::Name), QString(control.expectedName));
        QVERIFY2(!iface->state().invisible, qPrintable(QString("%1 should be visible").arg(control.objectName)));
        QVERIFY2(iface->state().focusable,
                 qPrintable(QString("%1 should be keyboard-focusable").arg(control.objectName)));
        QVERIFY2(!iface->state().disabled, qPrintable(QString("%1 should be enabled").arg(control.objectName)));
    }

    // Keyboard reachability: Tab must move focus away from the first button
    // instead of getting stuck on it. This is deliberately loose (it doesn't
    // assert an exact next widget) to stay reliable across minor layout
    // changes while still catching a real focus trap.
    auto* firstButton = welcomeWidget->findChild<QPushButton*>("buttonNewDatabase");
    firstButton->setFocus();
    QTRY_VERIFY(firstButton->hasFocus());
    QTest::keyClick(firstButton, Qt::Key_Tab);
    QApplication::processEvents();
    auto* focusAfterTab = QApplication::focusWidget();
    QVERIFY2(focusAfterTab && focusAfterTab != firstButton,
             "Tab should move keyboard focus away from the first welcome screen button");
}

void TestAccessibility::testToolbarButtonsAccessible()
{
    openTestDatabase();

    auto* toolBar = m_mainWindow->findChild<QToolBar*>("toolBar");
    VERIFY_ACCESSIBLE(toolBarIface, toolBar, QStringLiteral("toolBar"));

    QCOMPARE(toolBarIface->role(), QAccessible::ToolBar);
    QCOMPARE(toolBarIface->text(QAccessible::Name), QString("Database toolbar"));

    static const struct
    {
        const char* actionName;
        const char* expectedName;
    } controls[] = {
        {"actionDatabaseOpen", "Open Database"},
        {"actionDatabaseSave", "Save Database"},
        {"actionEntryNew", "New Entry"},
        {"actionEntryEdit", "Edit Entry"},
    };

    for (const auto& control : controls) {
        auto* action = m_mainWindow->findChild<QAction*>(control.actionName);
        QVERIFY2(action, qPrintable(QString("Missing action: %1").arg(control.actionName)));

        auto* buttonIface =
            findAccessibleChildByNamePrefix(toolBarIface, QString::fromUtf8(control.expectedName));
        QVERIFY2(buttonIface,
                 qPrintable(QString("Toolbar should expose accessible control: %1").arg(control.expectedName)));

        QCOMPARE(buttonIface->text(QAccessible::Name), QString(control.expectedName));
        QVERIFY2(!buttonIface->state().invisible,
                 qPrintable(QString("%1 should be visible").arg(control.actionName)));
        QVERIFY2(isAccessibleDescendantOf(buttonIface, toolBarIface),
                 qPrintable(QString("%1 should be exposed as a descendant of the toolbar in the accessibility tree")
                                .arg(control.actionName)));

        // Toolbar buttons must be reachable via Tab while enabled, not just
        // mouse click or the underlying action's shortcut -- regression
        // coverage in the same spirit as the PasswordWidget toggle/generator
        // focus fix. A disabled control should report its disabled state to
        // accessibility clients; Qt's QAccessibleWidget::state() derives
        // focusable from the widget's focus policy, not from isEnabled().
        // KPToolBar gives toolbar buttons Qt::TabFocus regardless of enabled
        // state, so the disabled button may legitimately remain focusable in
        // the accessibility tree. Real keyboard traversal below verifies that
        // Qt skips the disabled button as an actual Tab destination.
        QCOMPARE(buttonIface->state().disabled, !action->isEnabled());
        if (action->isEnabled()) {
            QVERIFY2(buttonIface->state().focusable,
                     qPrintable(QString("%1 should be keyboard-focusable while enabled").arg(control.actionName)));
        }
    }

    // Real keyboard-reachability regression: Tab from the enabled "Open
    // Database" button must leave that button and must not land on the
    // disabled "Save Database" button.
    auto* openAction = m_mainWindow->findChild<QAction*>("actionDatabaseOpen");
    auto* saveAction = m_mainWindow->findChild<QAction*>("actionDatabaseSave");
    auto* openButton = toolBar->widgetForAction(openAction);
    auto* saveButton = toolBar->widgetForAction(saveAction);
    QVERIFY(openAction);
    QVERIFY(saveAction);
    QVERIFY(openButton);
    QVERIFY(saveButton);
    QVERIFY2(!saveButton->isEnabled(), "actionDatabaseSave should still be disabled (no unsaved changes)");

    openButton->setFocus(Qt::TabFocusReason);
    QTRY_VERIFY(openButton->hasFocus());
    QTest::keyClick(openButton, Qt::Key_Tab);
    QApplication::processEvents();
    auto* focusAfterTab = QApplication::focusWidget();
    QVERIFY2(focusAfterTab && focusAfterTab != openButton,
             "Tab should move keyboard focus away from Open Database");
    QVERIFY2(focusAfterTab != saveButton,
             "Tab should skip the disabled Save Database button, not land on it");

    // Round-trip actionEntryEdit specifically: selecting an entry should
    // flip it from disabled to enabled, and the accessible tree needs to
    // reflect that live, not just whatever state it was constructed with.
    auto* entryView = m_dbWidget->findChild<EntryView*>("entryView");
    QVERIFY(entryView->model()->rowCount() > 0);
    entryView->setCurrentIndex(entryView->model()->index(0, 0));

    auto* editEntryAction = m_mainWindow->findChild<QAction*>("actionEntryEdit");
    auto* editEntryButton = toolBar->widgetForAction(editEntryAction);
    QTRY_VERIFY2(editEntryButton->isEnabled(), "actionEntryEdit should become enabled once an entry is selected");
    VERIFY_ACCESSIBLE(editEntryButtonIface, editEntryButton, QStringLiteral("actionEntryEdit (after selection)"));
    QVERIFY2(!editEntryButtonIface->state().disabled,
             "actionEntryEdit should report enabled once an entry is selected");
    QVERIFY2(editEntryButtonIface->state().focusable,
             "actionEntryEdit should become keyboard-focusable once enabled");
}

void TestAccessibility::testSearchWidgetAccessible()
{
    openTestDatabase();

    auto* toolBar = m_mainWindow->findChild<QToolBar*>("toolBar");
    auto* searchWidget = toolBar->findChild<QWidget*>("SearchWidget");
    QVERIFY(searchWidget);

    auto* searchEdit = searchWidget->findChild<QLineEdit*>("searchEdit");
    VERIFY_ACCESSIBLE(searchEditIface, searchEdit, QStringLiteral("searchEdit"));

    QCOMPARE(searchEditIface->role(), QAccessible::EditableText);
    QCOMPARE(searchEditIface->text(QAccessible::Name), QString("Search"));
    QVERIFY2(searchEditIface->state().focusable, "The search field should be keyboard-focusable");
    QVERIFY2(!searchEditIface->state().passwordEdit, "The search field must not be exposed as a password field");

    // The clear-text button is an internal QLineEdit icon button with no
    // stable object name, so find it the same way a screen reader would: by
    // walking the search field's accessible children and matching the
    // accessible name SearchWidget explicitly assigns to it.
    auto* clearButtonIface = findAccessibleChildByNamePrefix(searchEditIface, QStringLiteral("Clear Search"));
    QVERIFY2(clearButtonIface, "The search field should expose a child control named \"Clear Search\"");
    QCOMPARE(clearButtonIface->role(), QAccessible::PushButton);

    // This is NOT a Tab stop -- Qt's internal QLineEditIconButton sets
    // Qt::NoFocus by design (confirmed in qtbase's qlineedit_p.cpp), same as
    // every other Qt application's line-edit clear button. A keyboard-only
    // user is expected to select-all/backspace the field directly rather
    // than Tab to the icon. This documents that as an intentional, known
    // limitation rather than an oversight -- if this ever starts passing as
    // true, something changed in how the button is built and deserves a
    // second look.
    QVERIFY2(!clearButtonIface->state().focusable,
             "The Clear Search button is expected to be excluded from the Tab order (stock Qt behavior)");
}

void TestAccessibility::testEntryAndGroupViewsAccessible()
{
    openTestDatabase();

    auto* groupView = m_dbWidget->findChild<QTreeView*>("groupView");
    auto* entryView = m_dbWidget->findChild<EntryView*>("entryView");
    QVERIFY(groupView);
    QVERIFY(entryView);

    VERIFY_ACCESSIBLE(groupViewIface, groupView, QStringLiteral("groupView"));
    QCOMPARE(groupViewIface->role(), QAccessible::Tree);
    QCOMPARE(groupViewIface->text(QAccessible::Name), QString("Groups"));

    VERIFY_ACCESSIBLE(entryViewIface, entryView, QStringLiteral("entryView"));
    QCOMPARE(entryViewIface->role(), QAccessible::Tree);
    QCOMPARE(entryViewIface->text(QAccessible::Name), QString("Entries"));

    // Qt's tree accessibility bridge exposes one accessible child per
    // (row, column) cell -- not one per row -- and may also include a
    // header row, so childCount() isn't directly comparable to rowCount().
    // Adding exactly one entry should still grow childCount() by exactly
    // one row's worth of cells (columnCount()); checking the delta rather
    // than an absolute value keeps this robust to header/offset details.
    int initialRowCount = entryView->model()->rowCount();
    int initialChildCount = entryViewIface->childCount();
    int columnCount = entryView->model()->columnCount();

    triggerAction("actionEntryNew");
    QCOMPARE(m_dbWidget->currentMode(), DatabaseWidget::Mode::EditEntryMode);
    auto* editEntryWidget = m_dbWidget->findChild<EditEntryWidget*>("editEntryWidget");
    auto* titleEdit = editEntryWidget->findChild<QLineEdit*>("titleEdit");
    QTest::keyClicks(titleEdit, "Accessibility Test Entry");
    auto* buttonBox = editEntryWidget->findChild<QDialogButtonBox*>("buttonBox");
    auto* okButton = buttonBox->button(QDialogButtonBox::Ok);
    // Activate OK via the keyboard (Tab-then-Space, as a keyboard/screen
    // reader user would) rather than a synthetic mouse click -- this is an
    // accessibility test suite, so its interactions should match how the
    // users it cares about actually operate the UI.
    okButton->setFocus();
    QTRY_VERIFY(okButton->hasFocus());
    QTest::keyClick(okButton, Qt::Key_Space);
    QCOMPARE(m_dbWidget->currentMode(), DatabaseWidget::Mode::ViewMode);

    QTRY_COMPARE(entryView->model()->rowCount(), initialRowCount + 1);
    QCOMPARE(entryViewIface->childCount(), initialChildCount + columnCount);
}

void TestAccessibility::testEditEntryDialogAccessible()
{
    openTestDatabase();

    triggerAction("actionEntryNew");
    QCOMPARE(m_dbWidget->currentMode(), DatabaseWidget::Mode::EditEntryMode);

    auto* editEntryWidget = m_dbWidget->findChild<EditEntryWidget*>("editEntryWidget");
    QVERIFY(editEntryWidget);

    // The title field has an explicit accessible name in EditEntryWidgetMain.ui.
    // Keep this assertion aligned with the UI so a future change does not
    // accidentally remove the field's programmatic label.
    auto* titleEdit = editEntryWidget->findChild<QLineEdit*>("titleEdit");
    VERIFY_ACCESSIBLE(titleEditIface, titleEdit, QStringLiteral("titleEdit"));
    QCOMPARE(titleEditIface->role(), QAccessible::EditableText);
    QCOMPARE(titleEditIface->text(QAccessible::Name), QString("Title field"));

    // New Entry should place keyboard focus directly into the title field so
    // a keyboard or screen reader user can start typing immediately.
    QTRY_VERIFY2(titleEdit->hasFocus(), "The title field should have keyboard focus when the New Entry form opens");

    // The password field must be exposed as a password field to
    // accessibility clients, so screen readers don't announce typed
    // characters or read the stored secret back on request.
    auto* passwordWidget = editEntryWidget->findChild<PasswordWidget*>("passwordEdit");
    QVERIFY(passwordWidget);
    auto* passwordEdit = passwordWidget->findChild<QLineEdit*>("passwordEdit");
    VERIFY_ACCESSIBLE(passwordEditIface, passwordEdit, QStringLiteral("passwordEdit"));
    QCOMPARE(passwordEditIface->role(), QAccessible::EditableText);
    QVERIFY2(passwordEditIface->state().passwordEdit,
             "The password field must expose the password state to accessibility clients");

    // The password toggle-visibility and generator buttons are added as
    // QLineEdit actions rather than ordinary child widgets. Verify they are
    // both named and keyboard-focusable via the same accessible tree a
    // screen reader would walk (regression coverage for the Tab-focus fix
    // applied to PasswordWidget's trailing actions).
    auto* toggleVisibleIface = findAccessibleChildByNamePrefix(passwordEditIface, QStringLiteral("Toggle Password"));
    QVERIFY2(toggleVisibleIface, "The password field should expose a \"Toggle Password\" control");
    QVERIFY2(toggleVisibleIface->state().focusable, "The Toggle Password control should be keyboard-focusable");

    auto* generatorIface = findAccessibleChildByNamePrefix(passwordEditIface, QStringLiteral("Generate Password"));
    QVERIFY2(generatorIface, "The password field should expose a \"Generate Password\" control");
    QVERIFY2(generatorIface->state().focusable, "The Generate Password control should be keyboard-focusable");

    // OK/Cancel controls.
    auto* buttonBox = editEntryWidget->findChild<QDialogButtonBox*>("buttonBox");
    QVERIFY(buttonBox);
    auto* okButton = buttonBox->button(QDialogButtonBox::Ok);
    auto* cancelButton = buttonBox->button(QDialogButtonBox::Cancel);
    VERIFY_ACCESSIBLE(okIface, okButton, QStringLiteral("OK button"));
    VERIFY_ACCESSIBLE(cancelIface, cancelButton, QStringLiteral("Cancel button"));
    QCOMPARE(okIface->role(), QAccessible::PushButton);
    QCOMPARE(cancelIface->role(), QAccessible::PushButton);
    QVERIFY2(!okIface->text(QAccessible::Name).isEmpty(), "The OK button should have an accessible name");
    QVERIFY2(!cancelIface->text(QAccessible::Name).isEmpty(), "The Cancel button should have an accessible name");

    // Leave the dialog without saving so later tests start from a clean
    // state. Cancel is activated via the keyboard (Tab-then-Space), not a
    // synthetic mouse click. Note this form is a plain QWidget, not a
    // QDialog -- Escape is NOT wired to Cancel here (EditWidget never
    // overrides keyPressEvent for it), which is exactly the kind of gap
    // this suite exists to surface rather than assume away.
    MessageBox::setNextAnswer(MessageBox::Discard);
    cancelButton->setFocus();
    QTRY_VERIFY(cancelButton->hasFocus());
    QTest::keyClick(cancelButton, Qt::Key_Space);
    QApplication::processEvents();
    MessageBox::setNextAnswer(MessageBox::NoButton);
    QCOMPARE(m_dbWidget->currentMode(), DatabaseWidget::Mode::ViewMode);
}

void TestAccessibility::testProgressBarLabelAccessibleNameTracksMessages()
{
    // This is MainWindow's status-bar message label for the clipboard-clear
    // countdown (Clipboard::sendCountdownStatus()) and sync/reload progress
    // messages (DatabaseWidget's updateSyncProgress() call sites -- e.g.
    // "Downloading...", "Syncing...", "Reload successful"). Distinct from
    // m_statusBarLabel (the entry-count label, object name "statusBarLabel").
    auto* progressLabel = m_mainWindow->findChild<QLabel*>("progressBarLabel");
    VERIFY_ACCESSIBLE(progressLabelIface, progressLabel, QStringLiteral("progressBarLabel"));

    // updateProgressBar() is a private slot. Invoking it through the
    // meta-object system (rather than a friend/public wrapper) matches how
    // KPToolBar::setExpanded() invokes a private method elsewhere in this
    // codebase -- Qt's meta-object system doesn't enforce C++ access
    // control on slots, and this is the smallest way to exercise the real
    // code path the Clipboard/DatabaseWidget signals actually drive.
    bool invoked = QMetaObject::invokeMethod(
        m_mainWindow.data(), "updateProgressBar", Q_ARG(int, 50), Q_ARG(QString, QString("Syncing...")));
    QVERIFY2(invoked, "Could not invoke MainWindow::updateProgressBar via the meta-object system");

    QCOMPARE(progressLabel->text(), QString("Syncing..."));
    QVERIFY2(progressLabel->isVisible(), "progressBarLabel should be visible while a message is set");
    QCOMPARE(progressLabelIface->text(QAccessible::Name), QString("Syncing..."));

    // What this does NOT verify -- and, given how Qt's accessibility bridge
    // works, what nothing running under this suite's offscreen QPA platform
    // *can* verify -- is that a screen reader is actually notified when
    // this happens live. QAccessible::updateAccessibility() only reaches a
    // platform AT bridge when QAccessible::isActive() is true, and
    // isActive() is answered entirely by QPlatformAccessibility -- i.e.
    // whether a real bridge (Windows UIA, AT-SPI, NSAccessibility) is
    // currently listening. Confirmed directly against Qt's own
    // qaccessible.cpp: there is no in-process way to force this to true
    // (QAccessible::setActive() only notifies observers of a state change,
    // it does not set the flag isActive() reads), so this is a hard
    // platform boundary, not a gap in this test.
    //
    // It's a materially bigger gap than "can't verify live delivery",
    // though: on Windows specifically, MainWindow::updateProgressBar() does
    // NOT fire QAccessible::NameChanged the way updateEntryCountLabel()
    // does for m_statusBarLabel. It fires QAccessible::ValueChanged
    // instead, because Qt's Windows UIA bridge
    // (qwindowsuiamainprovider.cpp, notifyNameChange()) only forwards
    // NameChanged into a real UIA notification for QAccessible::ComboBox-
    // role widgets -- for this QLabel's StaticText role, a NameChanged
    // event is received and silently dropped, never reaching JAWS no
    // matter how correctly it's fired or how "isActive" the bridge is.
    // ValueChanged with a QString value IS forwarded for any widget, via
    // UiaRaiseNotificationEvent(). This test can't distinguish the two --
    // both leave progressLabelIface->text(QAccessible::Name) equally
    // correct -- which is exactly why static/offscreen coverage isn't
    // sufficient evidence here on its own.
    //
    // Proving the event is actually delivered live requires either a
    // running JAWS session, or a genuine Windows UIA event subscription
    // (AddAutomationEventHandler for UIA_NotificationEventId, since that's
    // the actual event this now raises -- not
    // IUIAutomationPropertyChangedEventHandler) against the real
    // Windows-process target in tests/accessibility/windows/ -- materially
    // more test infrastructure than exists there today, which currently
    // only polls static tree state rather than subscribing to live events.
}

// The five tests below cover the QAccessibleAnnouncementEvent work in
// 1b59782 (MessageWidget, MessageBox, PasswordWidget, TagsEdit) plus the
// markup-stripping fix that followed it. They use QTestAccessibility
// (<QtTest/QTestAccessibility>, part of the public Qt::Test module --
// KeePassXC's test targets already link it, see TEST_LIBRARIES in
// tests/CMakeLists.txt) to capture every QAccessible::updateAccessibility()
// call via QAccessible::installUpdateHandler(), which is a real,
// Qt-sanctioned bypass of the normal isActive()/bridge-dispatch path this
// suite otherwise cannot exercise (see the comment on
// testProgressBarLabelAccessibleNameTracksMessages() above). That is
// exactly what makes it useful here and exactly what it does not prove:
// this confirms KeePassXC calls the right API with the right target,
// message, and politeness. It does NOT confirm a real Windows UIA client,
// JAWS, or NVDA ever receives it -- that is still the job of the live
// capture scripts under tests/accessibility/windows/ and an actual
// screen-reader session, neither of which this file can stand in for.
//
// Verified against the real qtbase source (not assumed): in Qt 6.4/6.5,
// QAccessible::updateAccessibility() only invokes the installed update
// handler *inside* its `if (isActive() && iface)` block, so without a real
// AT client attached (true for any offscreen/headless run) the handler --
// and therefore QTestAccessibility -- never sees the event at all. That
// nesting was removed in 6.6; from 6.6 through at least 6.8 the handler
// dispatch is unconditional. Reproduced this locally: the exact pattern
// below, compiled and run against a real Qt 6.4.2 install with no AT
// client, captured zero events, matching that source read exactly. Since
// the feature under test already requires Qt >= 6.8 (see the version guard
// on every check below), this suite is safely on the fixed side of that
// line -- but it is a real floor, not a formality, and it's the reason a
// naive isActive()-gated test design would look like it works in a normal
// debugging session (a real AT or Windows Narrator often is active there)
// and then silently capture nothing in CI.
//
// Every Announcement assertion below is inside the same
// QT_VERSION >= QT_VERSION_CHECK(6, 8, 0) guard as the production code,
// mirroring it exactly -- on an older Qt this suite simply skips those
// checks and falls through to the Alert assertion, which is the fallback
// behavior that guard exists to preserve. The Alert checks always run,
// and per the above will themselves only pass on Qt >= 6.6.

void TestAccessibility::testMessageWidgetAnnouncesErrorAssertiveAndStripsMarkup()
{
    MessageWidget widget;
    widget.setAnimate(false);
    widget.show();
    QVERIFY(QTest::qWaitForWindowExposed(&widget));

    QTestAccessibility::clearEvents();
    // "<b>...</b>" / "<br/>" mirror what BrowserSettingsWidget and
    // SettingsWidgetFdoSecrets actually pass to showMessage() today --
    // see the accessiblePlainText() fix in MessageWidget.cpp.
    widget.showMessage(QStringLiteral("<b>Error:</b> proxy location does not exist<br/>Check your settings"),
                        MessageWidget::Error);

    bool sawAlert = false;
    for (auto* event : QTestAccessibility::events()) {
        if (event->object() == &widget && event->type() == QAccessible::Alert) {
            sawAlert = true;
        }
    }
    QVERIFY2(sawAlert,
             "MessageWidget::showMessage() must keep raising QAccessible::Alert -- it's the only mechanism on "
             "macOS/Linux ATs and on Qt < 6.8");

#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    QAccessibleAnnouncementEvent* announcement = nullptr;
    for (auto* event : QTestAccessibility::events()) {
        if (event->object() == &widget && event->type() == QAccessible::Announcement) {
            announcement = static_cast<QAccessibleAnnouncementEvent*>(event);
        }
    }
    QVERIFY2(announcement, "MessageWidget::showMessage() did not raise a QAccessibleAnnouncementEvent for Error");
    QCOMPARE(announcement->politeness(), QAccessible::AnnouncementPoliteness::Assertive);
    QVERIFY2(!announcement->message().contains(QLatin1String("<b>")),
             "Announcement text still contains raw \"<b>\" markup");
    QVERIFY2(!announcement->message().contains(QLatin1String("<br")),
             "Announcement text still contains raw \"<br\" markup");
    QVERIFY2(announcement->message().contains(QStringLiteral("Error:")),
             "Announcement lost real content while stripping markup (before the <br/>)");
    QVERIFY2(announcement->message().contains(QStringLiteral("Check your settings")),
             "Announcement lost real content while stripping markup (after the <br/>)");
#endif
}

void TestAccessibility::testMessageWidgetAnnouncesPositivePolite()
{
    MessageWidget widget;
    widget.setAnimate(false);
    widget.show();
    QVERIFY(QTest::qWaitForWindowExposed(&widget));

    QTestAccessibility::clearEvents();
    widget.showMessage(QStringLiteral("Entry saved"), MessageWidget::Positive);

#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    QAccessibleAnnouncementEvent* announcement = nullptr;
    for (auto* event : QTestAccessibility::events()) {
        if (event->object() == &widget && event->type() == QAccessible::Announcement) {
            announcement = static_cast<QAccessibleAnnouncementEvent*>(event);
        }
    }
    QVERIFY2(announcement, "MessageWidget::showMessage() did not raise a QAccessibleAnnouncementEvent for Positive");
    QCOMPARE(announcement->message(), QStringLiteral("Entry saved"));
    QCOMPARE(announcement->politeness(), QAccessible::AnnouncementPoliteness::Polite);
#endif
}

void TestAccessibility::testPasswordWidgetAnnouncesRepeatStatusTransition()
{
    PasswordWidget primary;
    PasswordWidget repeat;
    primary.setRepeatPartner(&repeat);
    primary.show();
    repeat.show();
    QVERIFY(QTest::qWaitForWindowExposed(&primary));
    QVERIFY(QTest::qWaitForWindowExposed(&repeat));

    auto* repeatEdit = repeat.findChild<QLineEdit*>("passwordEdit");
    QVERIFY(repeatEdit);

    QTestAccessibility::clearEvents();
    // Mismatch first (repeat is still empty when primary changes), then a
    // transition to match -- updateRepeatStatus() only announces on an
    // actual accessibleDescription() change, not per keystroke, so this
    // also confirms that guard still holds.
    primary.setText(QStringLiteral("hunter2"));
    repeat.setText(QStringLiteral("hunter2"));
    QCOMPARE(repeatEdit->accessibleDescription(), QStringLiteral("Passwords match"));

#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    QAccessibleAnnouncementEvent* announcement = nullptr;
    for (auto* event : QTestAccessibility::events()) {
        if (event->object() == repeatEdit && event->type() == QAccessible::Announcement) {
            // Keep the last one -- both the mismatch and the match
            // transition announce; the final state is what matters here.
            announcement = static_cast<QAccessibleAnnouncementEvent*>(event);
        }
    }
    QVERIFY2(announcement, "PasswordWidget::updateRepeatStatus() did not raise a QAccessibleAnnouncementEvent");
    QCOMPARE(announcement->message(), QStringLiteral("Passwords match"));
#endif
}

void TestAccessibility::testTagsEditAnnouncesAddedTag()
{
    TagsEdit tagsEdit;
    tagsEdit.show();
    QVERIFY(QTest::qWaitForWindowExposed(&tagsEdit));

    tagsEdit.setFocus();
    QTRY_VERIFY(tagsEdit.hasFocus());

    // Clear after the focus-in announcement (announceTagsState() fires on
    // focusInEvent too) so only the tag-commit announcement is captured.
    QTestAccessibility::clearEvents();
    QTest::keyClicks(&tagsEdit, QStringLiteral("work"));
    QTest::keyClick(&tagsEdit, Qt::Key_Return);

#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    QAccessibleAnnouncementEvent* announcement = nullptr;
    for (auto* event : QTestAccessibility::events()) {
        if (event->object() == &tagsEdit && event->type() == QAccessible::Announcement) {
            announcement = static_cast<QAccessibleAnnouncementEvent*>(event);
        }
    }
    QVERIFY2(announcement,
             "TagsEdit::announceTagsState() did not raise a QAccessibleAnnouncementEvent for a committed tag");
    QVERIFY2(announcement->message().contains(QStringLiteral("work")),
             "Announcement did not mention the newly committed tag");
#endif
}

void TestAccessibility::testMessageBoxAnnouncesAssertiveAndStripsMarkup()
{
    MessageBox::setNextAnswer(MessageBox::NoButton); // force the real exec() path, not a canned answer

    // MessageBox::warning() blocks in QMessageBox::exec() until a button is
    // clicked. Schedule the click for once that nested event loop is
    // spinning, the same pattern KeePassXC's other modal-dialog tests use
    // elsewhere in this suite via QTRY_VERIFY/QTest::keyClick against a
    // dialog found through topLevelWidgets().
    QTimer::singleShot(0, [] {
        for (auto* topLevel : QApplication::topLevelWidgets()) {
            if (auto* box = qobject_cast<QMessageBox*>(topLevel)) {
                if (!box->buttons().isEmpty()) {
                    box->buttons().constFirst()->click();
                }
                return;
            }
        }
    });

    QTestAccessibility::clearEvents();
    // Hardcoded "<br><br>" mirrors DatabaseWidget's reload-conflict prompt
    // -- see the accessiblePlainText() fix in MessageBox.cpp.
    MessageBox::warning(nullptr,
                         QStringLiteral("Weak password"),
                         QStringLiteral("This password is weak.<br><br>Continue anyway?"),
                         MessageBox::Yes | MessageBox::No,
                         MessageBox::No);

#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    QAccessibleAnnouncementEvent* announcement = nullptr;
    for (auto* event : QTestAccessibility::events()) {
        if (event->type() == QAccessible::Announcement) {
            announcement = static_cast<QAccessibleAnnouncementEvent*>(event);
        }
    }
    QVERIFY2(announcement, "MessageBox::warning() did not raise a QAccessibleAnnouncementEvent");
    QCOMPARE(announcement->politeness(), QAccessible::AnnouncementPoliteness::Assertive);
    QVERIFY2(!announcement->message().contains(QLatin1String("<br>")),
             "MessageBox announcement text still contains raw \"<br>\" markup");
    QVERIFY2(announcement->message().contains(QStringLiteral("Continue anyway?")),
             "MessageBox announcement lost real content while stripping markup");
#endif
}
