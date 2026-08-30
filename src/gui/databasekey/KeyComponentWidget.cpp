/*
 *  Copyright (C) 2018 KeePassXC Team <team@keepassxc.org>
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

#include "KeyComponentWidget.h"
#include "ui_KeyComponentWidget.h"

#include <QTimer>

KeyComponentWidget::KeyComponentWidget(QWidget* parent)
    : QWidget(parent)
    , m_ui(new Ui::KeyComponentWidget())
{
    m_ui->setupUi(this);

    connect(m_ui->addButton, SIGNAL(clicked(bool)), SIGNAL(componentAddRequested()));
    connect(m_ui->changeButton, SIGNAL(clicked(bool)), SIGNAL(componentEditRequested()));
    connect(m_ui->removeButton, SIGNAL(clicked(bool)), SIGNAL(componentRemovalRequested()));
    connect(m_ui->cancelButton, SIGNAL(clicked(bool)), SLOT(cancelEdit()));

    connect(m_ui->stackedWidget, SIGNAL(currentChanged(int)), SLOT(resetComponentEditWidget()));

    connect(this, SIGNAL(componentAddRequested()), SLOT(doAdd()));
    connect(this, SIGNAL(componentEditRequested()), SLOT(doEdit()));
    connect(this, SIGNAL(componentRemovalRequested()), SLOT(doRemove()));
    connect(this, SIGNAL(componentAddChanged(bool)), SLOT(updateAddStatus(bool)));

    bool prev = m_ui->stackedWidget->blockSignals(true);
    m_ui->stackedWidget->setCurrentIndex(Page::AddNew);
    m_ui->stackedWidget->blockSignals(prev);
}

KeyComponentWidget::~KeyComponentWidget() = default;

void KeyComponentWidget::setComponentAdded(bool added)
{
    if (m_isComponentAdded == added) {
        return;
    }

    m_isComponentAdded = added;
    emit componentAddChanged(added);
}

bool KeyComponentWidget::componentAdded() const
{
    return m_isComponentAdded;
}

void KeyComponentWidget::changeVisiblePage(KeyComponentWidget::Page page)
{
    m_previousPage = static_cast<Page>(m_ui->stackedWidget->currentIndex());
    m_ui->stackedWidget->setCurrentIndex(page);
}

KeyComponentWidget::Page KeyComponentWidget::visiblePage() const
{
    return static_cast<Page>(m_ui->stackedWidget->currentIndex());
}

void KeyComponentWidget::updateAddStatus(bool added)
{
    if (m_ui->stackedWidget->currentIndex() == Page::Edit) {
        emit editCanceled();
    }

    if (added) {
        m_ui->stackedWidget->setCurrentIndex(Page::LeaveOrRemove);
    } else {
        m_ui->stackedWidget->setCurrentIndex(Page::AddNew);
    }
}

void KeyComponentWidget::doAdd()
{
    changeVisiblePage(Page::Edit);
}

void KeyComponentWidget::doEdit()
{
    changeVisiblePage(Page::Edit);
}

void KeyComponentWidget::doRemove()
{
    changeVisiblePage(Page::AddNew);
    // setCurrentIndex() above hides the previously-focused widget inside the
    // Edit page (e.g. a password field) without moving focus anywhere else --
    // Qt does not automatically refocus a sensible visible widget when the
    // one that currently has focus is hidden. Left alone, keyboard/screen
    // reader focus is silently orphaned on a widget that no longer exists in
    // the visible page. addButton is what a keyboard user landing on this
    // now-visible page would expect to reach.
    m_ui->addButton->setFocus();
}

void KeyComponentWidget::cancelEdit()
{
    m_ui->stackedWidget->setCurrentIndex(m_previousPage);
    // Same reasoning as doRemove(): hiding the Edit page's focused field
    // doesn't move focus anywhere on its own. Refocus the primary control of
    // whichever page we're returning to.
    if (m_previousPage == Page::LeaveOrRemove) {
        m_ui->changeButton->setFocus();
    } else {
        m_ui->addButton->setFocus();
    }
    emit editCanceled();
}

void KeyComponentWidget::resetComponentEditWidget()
{
    if (!m_componentWidget || static_cast<Page>(m_ui->stackedWidget->currentIndex()) == Page::Edit) {
        if (m_componentWidget) {
            delete m_componentWidget;
        }

        m_componentWidget = componentEditWidget();
        m_ui->componentWidgetLayout->addWidget(m_componentWidget);
        initComponentEditWidget(m_componentWidget);
        fixTabOrder();
    }

    QTimer::singleShot(0, this, SLOT(updateSize()));
}

void KeyComponentWidget::fixTabOrder()
{
    // componentEditWidget() implementations build m_componentWidget (and its
    // children, e.g. the password fields) as a parentless QWidget and only
    // reparent it into componentWidgetContainer afterwards, via
    // m_ui->componentWidgetLayout->addWidget() above. Qt's default tab-focus
    // chain is ordered by widget creation/reparenting, not by layout
    // position: reparenting a widget subtree appends it to the *end* of the
    // top-level window's chain rather than splicing it in where it now
    // visually sits. Left uncorrected, Tab from the last field inside
    // m_componentWidget can land somewhere unexpected far later in the
    // window (or, depending on what else is hidden at the time, appear not
    // to move focus anywhere useful at all) instead of advancing to
    // cancelButton as a user tabbing through the form would expect. This
    // resplices m_componentWidget's own (internally correct) chain back
    // in between componentWidgetContainer and cancelButton.
    QWidget* firstFocusable = nullptr;
    QWidget* lastFocusable = nullptr;
    QWidget* w = m_componentWidget;
    // m_componentWidget's descendants were appended as a contiguous block;
    // walk forward only while still inside that subtree.
    for (int guard = 0; guard < 1000 && w; ++guard) {
        w = w->nextInFocusChain();
        if (!m_componentWidget->isAncestorOf(w)) {
            break;
        }
        if (w->focusPolicy() != Qt::NoFocus) {
            if (!firstFocusable) {
                firstFocusable = w;
            }
            lastFocusable = w;
        }
    }

    if (firstFocusable && lastFocusable) {
        QWidget::setTabOrder(m_ui->componentWidgetContainer, firstFocusable);
        QWidget::setTabOrder(lastFocusable, m_ui->cancelButton);
    }
}

void KeyComponentWidget::updateSize()
{
    for (int i = 0; i < m_ui->stackedWidget->count(); ++i) {
        if (m_ui->stackedWidget->currentIndex() == i) {
            m_ui->stackedWidget->widget(i)->setSizePolicy(
                m_ui->stackedWidget->widget(i)->sizePolicy().horizontalPolicy(), QSizePolicy::Preferred);
        } else {
            m_ui->stackedWidget->widget(i)->setSizePolicy(
                m_ui->stackedWidget->widget(i)->sizePolicy().horizontalPolicy(), QSizePolicy::Ignored);
        }
    }
}
