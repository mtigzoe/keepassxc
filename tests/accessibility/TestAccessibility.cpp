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
#include <QLineEdit>
#include <QPushButton>
#include <QTest>
#include <QToolBar>
#include <QTreeView>

#include "config-keepassx-tests.h"
#include "core/Config.h"
#include "core/Database.h"
#include "crypto/Crypto.h"
#include "gui/DatabaseTabWidget.h"
#include "gui/DatabaseWidget.h"
#include "gui/FileDialog.h"
#include "gui/MessageBox.h"
#include "gui/PasswordWidget.h"
#include "gui/entry/EditEntryWidget.h"
#include "gui/entry/EntryView.h"

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
        if (child && child->text(QAccessible::Name).startsWith(prefix)) {
            return child;
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
    QTRY_VERIFY2(welcomeWidget->isVisible(), "The welcome screen should be shown when no database is open");

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
        auto* button = toolBar->widgetForAction(action);
        VERIFY_ACCESSIBLE(buttonIface, button, QString(control.actionName));

        QCOMPARE(buttonIface->text(QAccessible::Name), QString(control.expectedName));
        QVERIFY2(!buttonIface->state().invisible,
                 qPrintable(QString("%1 should be visible").arg(control.actionName)));
        QVERIFY2(isAccessibleDescendantOf(buttonIface, toolBarIface),
                 qPrintable(QString("%1 should be exposed as a descendant of the toolbar in the accessibility tree")
                                .arg(control.actionName)));

        // Toolbar buttons must be reachable via Tab while enabled, not just
        // mouse click or the underlying action's shortcut -- regression
        // coverage in the same spirit as the PasswordWidget toggle/generator
        // focus fix. A disabled control should NOT be a Tab stop, and the
        // accessible tree must say so too -- a screen reader user landing on
        // a "focusable but disabled" control gets an inconsistent
        // experience. actionEntryEdit starts disabled here (no entry is
        // selected yet), which lets this loop check both states for free.
        QCOMPARE(buttonIface->state().disabled, !button->isEnabled());
        if (button->isEnabled()) {
            QVERIFY2(buttonIface->state().focusable,
                     qPrintable(QString("%1 should be keyboard-focusable while enabled").arg(control.actionName)));
        } else {
            QVERIFY2(!buttonIface->state().focusable,
                     qPrintable(QString("%1 should not be a Tab stop while disabled").arg(control.actionName)));
        }
    }

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
