/*
 *  Copyright (C) 2026 KeePassXC Team <team@keepassxc.org>
 *  Copyright (C) 2010 Felix Geyer <debfx@fobos.de>
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

#include "EditEntryWidget.h"
#include "ui_EditEntryWidgetAdvanced.h"
#include "ui_EditEntryWidgetAutoType.h"
#include "ui_EditEntryWidgetBrowser.h"
#include "ui_EditEntryWidgetHistory.h"
#include "ui_EditEntryWidgetMain.h"
#include "ui_EditEntryWidgetSSHAgent.h"

#include <QColorDialog>
#include <QDesktopServices>
#include <QSortFilterProxyModel>
#include <QStringListModel>

#include "autotype/AutoType.h"
#include "core/AutoTypeAssociations.h"
#include "core/Clock.h"
#include "core/Config.h"
#include "core/Database.h"
#include "core/Entry.h"
#include "core/EntryAttributes.h"
#include "core/Group.h"
#include "core/Metadata.h"
#include "core/PasswordGenerator.h"
#include "core/TimeDelta.h"
#include "gui/PasswordWidget.h"
#ifdef KPXC_FEATURE_SSHAGENT
#include "sshagent/OpenSSHKey.h"
#include "sshagent/OpenSSHKeyGenDialog.h"
#include "sshagent/SSHAgent.h"
#include <QSignalBlocker>
#endif
#ifdef KPXC_FEATURE_BROWSER
#include "EntryURLModel.h"
#include "browser/BrowserService.h"
#endif
#include "gui/Clipboard.h"
#include "gui/EditWidgetIcons.h"
#include "gui/EditWidgetProperties.h"
#include "gui/FileDialog.h"
#include "gui/Font.h"
#include "gui/GuiTools.h"
#include "gui/Icons.h"
#include "gui/MessageBox.h"
#include "gui/entry/AutoTypeAssociationsModel.h"
#include "gui/entry/EntryAttributesModel.h"
#include "gui/entry/EntryHistoryModel.h"

EditEntryWidget::EditEntryWidget(QWidget* parent)
    : EditWidget(parent)
    , m_entry(nullptr)
    , m_mainUi(new Ui::EditEntryWidgetMain())
    , m_advancedUi(new Ui::EditEntryWidgetAdvanced())
    , m_autoTypeUi(new Ui::EditEntryWidgetAutoType())
    , m_sshAgentUi(new Ui::EditEntryWidgetSSHAgent())
    , m_historyUi(new Ui::EditEntryWidgetHistory())
    , m_browserUi(new Ui::EditEntryWidgetBrowser())
    , m_attachments(new EntryAttachments())
    , m_customData(new CustomData())
    , m_mainWidget(new QScrollArea(this))
    , m_advancedWidget(new QWidget(this))
    , m_iconsWidget(new EditWidgetIcons(this))
    , m_autoTypeWidget(new QWidget(this))
#ifdef KPXC_FEATURE_SSHAGENT
    , m_sshAgentWidget(new QWidget(this))
#endif
#ifdef KPXC_FEATURE_BROWSER
    , m_browserSettingsChanged(false)
    , m_browserWidget(new QWidget(this))
    , m_additionalURLsDataModel(new EntryURLModel(this))
#endif
    , m_editWidgetProperties(new EditWidgetProperties(this))
    , m_historyWidget(new QWidget(this))
    , m_entryAttributes(new EntryAttributes(this))
    , m_attributesModel(new EntryAttributesModel(m_advancedWidget))
    , m_historyModel(new EntryHistoryModel(this))
    , m_sortModel(new QSortFilterProxyModel(this))
    , m_autoTypeAssoc(new AutoTypeAssociations(this))
    , m_autoTypeAssocModel(new AutoTypeAssociationsModel(this))
    , m_autoTypeDefaultSequenceGroup(new QButtonGroup(this))
    , m_autoTypeWindowSequenceGroup(new QButtonGroup(this))
    , m_usernameCompleter(new QCompleter(this))
    , m_usernameCompleterModel(new QStringListModel(this))
{
    setupMain();
    setupAdvanced();
    setupIcon();
    setupAutoType();

#ifdef KPXC_FEATURE_SSHAGENT
    setupSSHAgent();
#endif

#ifdef KPXC_FEATURE_BROWSER
    setupBrowser();
#endif

    setupProperties();
    setupHistory();
    setupEntryUpdate();

    m_entryModifiedTimer.setSingleShot(true);
    m_entryModifiedTimer.setInterval(0);
    connect(&m_entryModifiedTimer, &QTimer::timeout, this, [this] {
        // TODO: Upon refactor of this widget, this needs to merge unsaved changes in the UI
        if (isVisible() && m_entry) {
            setForms(m_entry);
        }
    });

    connect(this, SIGNAL(accepted()), SLOT(acceptEntry()));
    connect(this, SIGNAL(rejected()), SLOT(cancel()));
    connect(this, SIGNAL(apply()), SLOT(commitEntry()));
    // clang-format off
    connect(m_iconsWidget,
            SIGNAL(messageEditEntry(QString,MessageWidget::MessageType)),
            SLOT(showMessage(QString,MessageWidget::MessageType)));
    // clang-format on

    connect(m_iconsWidget, SIGNAL(messageEditEntryDismiss()), SLOT(hideMessage()));

    m_editWidgetProperties->setCustomData(m_customData.data());

    m_mainUi->passwordEdit->setQualityVisible(true);

    connect(m_mainUi->passwordEdit,
            &PasswordWidget::requestPlaceholderResolution,
            this,
            [this](const QString& rawText, QString& resolvedText) {
                if (m_entry) {
                    // Dereferencing the password of the entry
                    resolvedText = m_entry->resolveMultiplePlaceholders(rawText);
                }
            });
}

EditEntryWidget::~EditEntryWidget() = default;

bool EditEntryWidget::switchToPage(Page page)
{
    auto index = pageIndex(widgetForPage(page));
    if (index >= 0) {
        setCurrentPage(index);
        return true;
    }
    return false;
}

QWidget* EditEntryWidget::widgetForPage(Page page) const
{
    switch (page) {
    case Page::Main:
        return m_mainWidget;
    case Page::Advanced:
        return m_advancedWidget;
    case Page::Icon:
        return m_iconsWidget;
    case Page::AutoType:
        return m_autoTypeWidget;
    case Page::Browser:
#ifdef KPXC_FEATURE_BROWSER
        return m_browserWidget;
#else
        return nullptr;
#endif
    case Page::SSHAgent:
#ifdef KPXC_FEATURE_SSHAGENT
        return m_sshAgentWidget;
#else
        return nullptr;
#endif
    case Page::Properties:
        return m_editWidgetProperties;
    case Page::History:
        return m_historyWidget;
    }
    return nullptr;
}

void EditEntryWidget::setupMain()
{
    m_mainUi->setupUi(m_mainWidget);
    addPage(tr("Entry"), icons()->icon("document-edit"), m_mainWidget);

    // QPlainTextEdit swallows Tab as a literal tab character by default,
    // trapping keyboard/screen-reader users in the field. Match the group
    // Notes field, which already opts out of that via setTabChangesFocus.
    m_mainUi->notesEdit->setTabChangesFocus(true);
    m_mainUi->notesEdit->setAccessibleName(tr("Notes"));
    m_mainUi->notesEdit->setAccessibleDescription(tr("Notes field for this entry"));
    m_mainUi->notesLabel->setBuddy(m_mainUi->notesEdit);

    // Disable mouse wheel grab when scrolling
    m_mainUi->usernameComboBox->installEventFilter(new MouseWheelEventFilter(this));
    m_mainUi->usernameComboBox->setEditable(true);
    m_mainUi->usernameComboBox->lineEdit()->setFocusPolicy(Qt::StrongFocus);
    m_usernameCompleter->setCompletionMode(QCompleter::InlineCompletion);
    m_usernameCompleter->setCaseSensitivity(Qt::CaseSensitive);
    m_usernameCompleter->setModel(m_usernameCompleterModel);
    m_mainUi->usernameComboBox->setCompleter(m_usernameCompleter);

#ifdef KPXC_FEATURE_NETWORK
    m_mainUi->fetchFaviconButton->setIcon(icons()->icon("favicon-download"));
    m_mainUi->fetchFaviconButton->setDisabled(true);
#else
    m_mainUi->fetchFaviconButton->setVisible(false);
#endif

#ifdef KPXC_FEATURE_NETWORK
    connect(m_mainUi->fetchFaviconButton, SIGNAL(clicked()), m_iconsWidget, SLOT(downloadFavicon()));
    connect(m_mainUi->urlEdit, SIGNAL(textChanged(QString)), m_iconsWidget, SLOT(setUrl(QString)));
    m_mainUi->urlEdit->enableVerifyMode();
#endif
#ifdef KPXC_FEATURE_BROWSER
    connect(m_mainUi->urlEdit, SIGNAL(textChanged(QString)), this, SLOT(entryURLEdited(const QString&)));
#endif
    connect(m_mainUi->expireCheck, &QCheckBox::toggled, [&](bool enabled) {
        m_mainUi->expireDatePicker->setEnabled(enabled);
        if (enabled) {
            m_mainUi->expireDatePicker->setDateTime(Clock::currentDateTime());
        }
    });

    connect(m_mainUi->revealNotesButton, &QToolButton::clicked, this, &EditEntryWidget::toggleHideNotes);

    m_mainUi->expirePresets->setMenu(createPresetsMenu());
    connect(m_mainUi->expirePresets->menu(), SIGNAL(triggered(QAction*)), this, SLOT(useExpiryPreset(QAction*)));
}
