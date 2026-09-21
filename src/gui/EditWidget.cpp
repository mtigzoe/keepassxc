/*
 *  Copyright (C) 2012 Felix Geyer <debfx@fobos.de>
 *  Copyright (C) 2017 KeePassXC Team <team@keepassxc.org>
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

#include "EditWidget.h"
#include "ui_EditWidget.h"

#include <QApplication>
#include <QPushButton>
#include <QScrollArea>

EditWidget::EditWidget(QWidget* parent)
    : DialogyWidget(parent)
    , m_ui(new Ui::EditWidget())
{
    m_ui->setupUi(this);
    setReadOnly(false);
    setModified(false);

    m_ui->messageWidget->setHidden(true);
    m_ui->headerLabel->setHidden(true);

    QFont headerLabelFont = m_ui->headerLabel->font();
    headerLabelFont.setBold(true);
    headerLabelFont.setPointSize(headerLabelFont.pointSize() + 2);
    headlineLabel()->setFont(headerLabelFont);
    headlineLabel()->setTextFormat(Qt::PlainText);

    connect(m_ui->categoryList, SIGNAL(categoryChanged(int)), m_ui->stackedWidget, SLOT(setCurrentIndex(int)));

    connect(m_ui->buttonBox, SIGNAL(accepted()), SIGNAL(accepted()));
    connect(m_ui->buttonBox, SIGNAL(rejected()), SIGNAL(rejected()));
    connect(m_ui->buttonBox, SIGNAL(clicked(QAbstractButton*)), SLOT(buttonClicked(QAbstractButton*)));
}

EditWidget::~EditWidget() = default;

void EditWidget::addPage(const QString& labelText, const QIcon& icon, QWidget* widget)
{
    /*
     * Instead of just adding a widget we're going to wrap it into a scroll area. It will automatically show
     * scrollbars when the widget cannot fit into the page. This approach prevents the main window of the application
     * from automatic resizing and it now should be able to fit into a user's monitor even if the monitor is only 768
     * pixels high.
     */
    if (widget->inherits("QScrollArea")) {
        m_ui->stackedWidget->addWidget(widget);
    } else {
        auto* scrollArea = new QScrollArea(m_ui->stackedWidget);
        scrollArea->setFrameShape(QFrame::NoFrame);
        scrollArea->setFrameShadow(QFrame::Plain);
        scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scrollArea->setSizeAdjustPolicy(QScrollArea::AdjustToContents);
        scrollArea->setWidgetResizable(true);
        scrollArea->setWidget(widget);
        m_ui->stackedWidget->addWidget(scrollArea);
    }
    m_ui->categoryList->addCategory(labelText, icon);
}

bool EditWidget::hasPage(const QWidget* widget) const
{
    return pageIndex(widget) >= 0;
}

int EditWidget::pageIndex(const QWidget* widget) const
{
    if (!widget) {
        return -1;
    }

    for (int i = 0; i < m_ui->stackedWidget->count(); i++) {
        auto* scrollArea = qobject_cast<QScrollArea*>(m_ui->stackedWidget->widget(i));
        if (scrollArea && (scrollArea == widget || scrollArea->widget() == widget)) {
            return i;
        }
    }

    return -1;
}

void EditWidget::setPageHidden(QWidget* widget, bool hidden)
{
    int index = -1;

    for (int i = 0; i < m_ui->stackedWidget->count(); i++) {
        auto* scrollArea = qobject_cast<QScrollArea*>(m_ui->stackedWidget->widget(i));
        if (scrollArea && scrollArea->widget() == widget) {
            index = i;
            break;
        }
    }

    if (index == -1) {
        return;
    }

    bool changed = m_ui->categoryList->isCategoryHidden(index) != hidden;

    // Hiding a page can remove the currently focused editor or button from
    // the UI while leaving keyboard and screen-reader focus on that hidden
    // widget. Return focus to the category list before hiding the page.
    if (changed && hidden && index == m_ui->stackedWidget->currentIndex()) {
        QWidget* focusedWidget = QApplication::focusWidget();
        if (focusedWidget && (focusedWidget == widget || widget->isAncestorOf(focusedWidget))) {
            m_ui->categoryList->setFocus();
        }
    }

    m_ui->categoryList->setCategoryHidden(index, hidden);

    if (changed && index == m_ui->stackedWidget->currentIndex()) {
        // Select the nearest visible page. A hidden page immediately before
        // the current page must not become the new category selection.
        const int currentIndex = m_ui->stackedWidget->currentIndex();
        int newIndex = currentIndex;
        for (int offset = 1; offset <= m_ui->stackedWidget->count(); ++offset) {
            const int candidate = (currentIndex - offset + m_ui->stackedWidget->count())
                % m_ui->stackedWidget->count();
            if (!m_ui->categoryList->isCategoryHidden(candidate)) {
                newIndex = candidate;
                break;
            }
        }
        m_ui->categoryList->setCurrentCategory(newIndex);
    }
}

void EditWidget::setCurrentPage(int index)
{
    m_ui->categoryList->setCurrentCategory(index);
    m_ui->stackedWidget->setCurrentIndex(index);
}

void EditWidget::setHeadline(const QString& text)
{
    m_ui->headerLabel->setHidden(text.isEmpty());
    m_ui->headerLabel->setText(text);
}

QLabel* EditWidget::headlineLabel()
{
    return m_ui->headerLabel;
}

void EditWidget::setReadOnly(bool readOnly)
{
    m_readOnly = readOnly;

    // Changing the standard buttons can remove the button that currently has
    // keyboard or screen-reader focus. Preserve focus on the replacement
    // standard button instead of leaving focus on a removed widget.
    QWidget* focusedWidget = QApplication::focusWidget();
    const bool focusIsOnButton = focusedWidget && m_ui->buttonBox->standardButton(qobject_cast<QAbstractButton*>(focusedWidget)) != QDialogButtonBox::NoButton;

    if (readOnly) {
        m_ui->buttonBox->setStandardButtons(QDialogButtonBox::Close);
        if (focusIsOnButton) {
            m_ui->buttonBox->button(QDialogButtonBox::Close)->setFocus();
        }
    } else {
        m_ui->buttonBox->setStandardButtons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply);
        if (focusIsOnButton && m_ui->buttonBox->button(QDialogButtonBox::Ok)) {
            m_ui->buttonBox->button(QDialogButtonBox::Ok)->setFocus();
        }
    }
}

bool EditWidget::readOnly() const
{
    return m_readOnly;
}

void EditWidget::setModified(bool state)
{
    m_modified = state;
    enableApplyButton(state);
}

bool EditWidget::isModified() const
{
    return m_modified;
}

void EditWidget::enableApplyButton(bool enabled)
{
    QPushButton* applyButton = m_ui->buttonBox->button(QDialogButtonBox::Apply);
    if (applyButton) {
        // Applying a change can immediately mark the editor unmodified. If
        // Apply currently has keyboard or screen-reader focus, move focus to
        // OK before disabling it so focus does not remain on a disabled button.
        if (!enabled && applyButton->hasFocus()) {
            if (auto* okButton = m_ui->buttonBox->button(QDialogButtonBox::Ok)) {
                okButton->setFocus();
            }
        }
        applyButton->setEnabled(enabled);
    }
}

void EditWidget::showApplyButton(bool state)
{
    if (!m_readOnly) {
        auto buttons = m_ui->buttonBox->standardButtons();
        const bool applyHasFocus = m_ui->buttonBox->button(QDialogButtonBox::Apply)
            && m_ui->buttonBox->button(QDialogButtonBox::Apply)->hasFocus();

        if (state) {
            buttons |= QDialogButtonBox::Apply;
        } else {
            buttons &= ~QDialogButtonBox::Apply;
        }

        if (applyHasFocus && !state) {
            // Removing the Apply button while it has focus would strand
            // keyboard and screen-reader focus on the removed control.
            if (m_ui->buttonBox->button(QDialogButtonBox::Ok)) {
                m_ui->buttonBox->button(QDialogButtonBox::Ok)->setFocus();
            }
        }

        m_ui->buttonBox->setStandardButtons(buttons);

        // QDialogButtonBox may recreate its buttons when the standard button
        // set changes. Restore focus after the replacement has been created.
        if (applyHasFocus && !state && m_ui->buttonBox->button(QDialogButtonBox::Ok)) {
            m_ui->buttonBox->button(QDialogButtonBox::Ok)->setFocus();
        }
    }
}

void EditWidget::buttonClicked(QAbstractButton* button)
{
    auto stdButton = m_ui->buttonBox->standardButton(button);
    if (stdButton == QDialogButtonBox::Apply) {
        emit apply();
    }
}

void EditWidget::showMessage(const QString& text, MessageWidget::MessageType type)
{
    // Show error messages for a longer time to make sure the user can read them
    if (type == MessageWidget::Error) {
        m_ui->messageWidget->setCloseButtonVisible(true);
        m_ui->messageWidget->showMessage(text, type, 15000);
    } else {
        m_ui->messageWidget->setCloseButtonVisible(false);
        m_ui->messageWidget->showMessage(text, type, 2000);
    }
}

void EditWidget::hideMessage()
{
    if (m_ui->messageWidget->isVisible()) {
        m_ui->messageWidget->animatedHide();
    }
}
