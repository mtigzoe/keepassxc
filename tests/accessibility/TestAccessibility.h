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

#ifndef KEEPASSX_TESTACCESSIBILITY_H
#define KEEPASSX_TESTACCESSIBILITY_H

#include "gui/MainWindow.h"
#include "util/TemporaryFile.h"

class Database;
class DatabaseTabWidget;
class DatabaseWidget;
class QAccessibleInterface;

// Tests in this file verify KeePassXC's Qt widget accessibility tree using
// QAccessible::queryAccessibleInterface() directly -- the same in-process
// bridge Qt uses to answer AT-SPI2 (Linux), UI Automation/MSAA (Windows),
// and NSAccessibility (macOS) requests. This intentionally checks what Qt
// exposes to those native backends without requiring one to be running, so
// it is fast and reliable in headless CI. It does NOT prove that a given
// platform's native AT stack renders the tree correctly end-to-end -- that
// is what tools/dump_tree_windows.py and .github/workflows/accessibility-atspi.yml
// verify. Both layers matter and neither replaces the other.
class TestAccessibility : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();
    void cleanupTestCase();

    void testMainWindowAccessible();
    void testWelcomeScreenControlsAccessible();
    void testToolbarButtonsAccessible();
    void testSearchWidgetAccessible();
    void testEntryAndGroupViewsAccessible();
    void testEditEntryDialogAccessible();

private:
    void triggerAction(const QString& name);
    void openTestDatabase();
    void closeTestDatabase();

    // Plain lookups with no QVERIFY/QCOMPARE inside them (those macros expand
    // to a bare "return;", which only compiles in a void function -- see the
    // VERIFY_ACCESSIBLE() macro in the .cpp file for the asserting counterpart
    // used directly inside test slots).
    QAccessibleInterface* queryAccessible(QWidget* widget) const;
    QAccessibleInterface* findAccessibleChildByNamePrefix(QAccessibleInterface* parent, const QString& prefix) const;
    bool isAccessibleDescendantOf(QAccessibleInterface* candidate, QAccessibleInterface* ancestor) const;

    QScopedPointer<MainWindow> m_mainWindow;
    QPointer<DatabaseTabWidget> m_tabWidget;
    QPointer<DatabaseWidget> m_dbWidget;
    QSharedPointer<Database> m_db;
    TemporaryFile m_dbFile;
    QString m_dbFilePath;
    bool m_dbOpen = false;
};

#endif // KEEPASSX_TESTACCESSIBILITY_H
