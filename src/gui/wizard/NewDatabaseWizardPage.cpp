
/*
 *  Copyright (C) 2023 KeePassXC Team <team@keepassxc.org>
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

#include "NewDatabaseWizardPage.h"
#include "ui_NewDatabaseWizardPage.h"

#include "core/Database.h"
#include "gui/dbsettings/DatabaseSettingsWidget.h"

#include <QComboBox>
#include <QLineEdit>

NewDatabaseWizardPage::NewDatabaseWizardPage(QWidget* parent)
    : QWizardPage(parent)
    , m_ui(new Ui::NewDatabaseWizardPage())
{
    m_ui->setupUi(this);
}

NewDatabaseWizardPage::~NewDatabaseWizardPage() = default;

/**
 * Set the database settings page widget for this wizard page.
 * The wizard page will take ownership of the settings page widget.
 *
 * @param page database settings page widget
 */
void NewDatabaseWizardPage::setPageWidget(DatabaseSettingsWidget* page)
{
    m_pageWidget = page;
    m_ui->pageContent->setWidget(m_pageWidget);
}

/**
 * @return database settings widget of this page widget.
 */
DatabaseSettingsWidget* NewDatabaseWizardPage::pageWidget()
{
    return m_pageWidget;
}

/**
 * Set the database to be configured by the wizard page.
 * The wizard will NOT take ownership of the database object.
 *
 * @param db database object to be configured
 */
void NewDatabaseWizardPage::setDatabase(QSharedPointer<Database> db)
{
    m_db = std::move(db);
}

void NewDatabaseWizardPage::initializePage()
{
    Q_ASSERT(m_pageWidget && m_db);
    if (!m_pageWidget || !m_db) {
        return;
    }

    // JAWS reads QWizard subTitle; braille typically only shows the focused
    // control's accessibleName. Put the page guidance on the first combo so
    // both speech and braille get it when Tab lands on Database format.
    if (auto* formatCombo = m_pageWidget->findChild<QComboBox*>(QStringLiteral("compatibilitySelection"))) {
        const QString guidance = tr("%1. %2").arg(title(), subTitle());
        formatCombo->setAccessibleName(
            tr("Database format. %1").arg(guidance));
        formatCombo->setAccessibleDescription(
            tr("Database Format and Encryption. Choose the database format and configure encryption settings."));
    }
    // Same fix, replicated for the MetaData page's first field (the database
    // name, see DatabaseSettingsWidgetMetaDataSimple.ui) and the DatabaseKey
    // page's first field (the master password, see PasswordEditWidget.ui,
    // which is also where loadSettings() below sends initial focus).
    // findChild() no-ops on pages that don't have a control by that name, so
    // this is safe to leave unconditional across all three pages.
    if (auto* nameField = m_pageWidget->findChild<QLineEdit*>(QStringLiteral("databaseName"))) {
        const QString guidance = tr("%1. %2").arg(title(), subTitle());
        nameField->setAccessibleName(tr("Database name field. %1").arg(guidance));
        nameField->setAccessibleDescription(
            tr("General Database Information. Enter a display name and optional description for your new database."));
    }
    if (auto* passwordField = m_pageWidget->findChild<QWidget*>(QStringLiteral("enterPasswordEdit"))) {
        const QString guidance = tr("%1. %2").arg(title(), subTitle());
        passwordField->setAccessibleName(tr("Password field. %1").arg(guidance));
        passwordField->setAccessibleDescription(
            tr("Database Credentials. Enter and confirm a password to protect your new database."));
    }
    // Also expose on the page itself for ATs that read the container.
    setAccessibleDescription(tr("%1. %2").arg(title(), subTitle()));

    m_pageWidget->loadSettings(m_db);
}

bool NewDatabaseWizardPage::validatePage()
{
    Q_ASSERT(m_pageWidget && m_db);
    if (!m_pageWidget || !m_db) {
        return false;
    }

    bool valid = m_pageWidget->saveSettings();
    m_pageWidget->uninitialize();
    return valid;
}
