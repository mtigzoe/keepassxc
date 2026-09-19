/*
 *  Copyright (C) 2021 KeePassXC Team <team@keepassxc.org>
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

#include "KPToolBar.h"

#include <QAbstractButton>
#include <QAction>
#include <QEvent>
#include <QLayout>
#include <QToolButton>

KPToolBar::KPToolBar(const QString& title, QWidget* parent)
    : QToolBar(title, parent)
{
    init();
}

KPToolBar::KPToolBar(QWidget* parent)
    : QToolBar(parent)
{
    init();
}

void KPToolBar::init()
{
    m_expandButton = findChild<QAbstractButton*>("qt_toolbar_ext_button");
    m_expandTimer.setSingleShot(true);
    connect(&m_expandTimer, &QTimer::timeout, this, [this] { setExpanded(false); });
}

bool KPToolBar::isExpanded()
{
    return !canExpand() || (canExpand() && m_expandButton->isChecked());
}

bool KPToolBar::canExpand()
{
    return m_expandButton && m_expandButton->isVisible();
}

void KPToolBar::setExpanded(bool state)
{
    if (canExpand() && !QMetaObject::invokeMethod(layout(), "setExpanded", Q_ARG(bool, state))) {
        qWarning("Toolbar: Cannot invoke setExpanded!");
    }
}

void KPToolBar::updateButtonAccessibility()
{
    // m_expandButton is normally captured once, in init(), when this
    // KPToolBar is constructed. But QToolBarLayout creates its internal
    // overflow/"extension" button (Qt's own "qt_toolbar_ext_button")
    // lazily -- the first time it actually computes whether every action
    // fits -- which has not necessarily happened yet at construction
    // time, before the toolbar has ever been shown or laid out. If that
    // first findChild() in init() ran before Qt had created the button,
    // m_expandButton is left permanently null (nothing ever re-queries
    // it), even though the real widget exists and is visible once the
    // toolbar actually lays out. Re-resolve it here too, so a still-lazy
    // button on that first pass isn't permanently missed. This is exactly
    // why the previous fix (which only ever wrote through the possibly-
    // still-null m_expandButton) had no visible effect: the accessible
    // name was written, if at all, to a pointer that was never the live
    // widget.
    if (!m_expandButton) {
        m_expandButton = findChild<QAbstractButton*>(QStringLiteral("qt_toolbar_ext_button"));
    }

    for (QAction* action : actions()) {
        if (action->isSeparator()) {
            continue;
        }
        QWidget* widget = widgetForAction(action);
        if (auto* button = qobject_cast<QToolButton*>(widget)) {
            const QString name = action->toolTip().isEmpty()
                ? action->text().remove(QLatin1Char('&'))
                : action->toolTip();
            if (!name.isEmpty()) {
                button->setAccessibleName(name);
            }
            button->setFocusPolicy(Qt::TabFocus);
        }
    }

    // m_expandButton is not backed by a QAction, so the loop above never
    // reaches it -- Qt never gives it an accessible name of its own.
    // Screen readers therefore announced it as an unlabeled checkbox. It's
    // the control that toggles this KPToolBar between its compact and
    // expanded states (see isExpanded()/canExpand()/setExpanded()), so name
    // it accordingly and make sure it's a Tab stop like every other button
    // here, consistent with the "Show <panel>" naming already used for the
    // other checkable toolbar toggles (actionDatabaseSettings, actionReports,
    // actionPasswordGenerator, actionSettings).
    if (m_expandButton) {
        m_expandButton->setAccessibleName(tr("Show More Toolbar Buttons"));
        m_expandButton->setFocusPolicy(Qt::TabFocus);
    }
}

bool KPToolBar::event(QEvent* event)
{
    // Override events handled by the base class for better UX when using an expandable toolbar.
    switch (event->type()) {
    case QEvent::Leave:
        // Hide the toolbar after 2 seconds of mouse exit
        m_expandTimer.start(2000);
        return true;
    case QEvent::Enter:
        // Mouse came back in, stop hiding timer
        m_expandTimer.stop();
        return true;
    case QEvent::Show:
    case QEvent::LayoutRequest:
        updateButtonAccessibility();
        break;
    default:
        break;
    }
    return QToolBar::event(event);
}
