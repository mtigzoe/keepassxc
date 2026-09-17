/*
 *  Copyright (C) 2015 Pedro Alves <devel@pgalves.com>
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

#include "MessageWidget.h"

#include <QAccessible>
#include <QDesktopServices>
#include <QTimer>
#include <QUrl>

const int MessageWidget::DefaultAutoHideTimeout = 6000;
const int MessageWidget::LongAutoHideTimeout = 15000;
const int MessageWidget::DisableAutoHide = -1;

MessageWidget::MessageWidget(QWidget* parent)
    : KMessageWidget(parent)
    , m_autoHideTimer(new QTimer(this))
    , m_autoHideTimeout(DefaultAutoHideTimeout)
{
    m_autoHideTimer->setSingleShot(true);
    connect(m_autoHideTimer, SIGNAL(timeout()), this, SLOT(animatedHide()));
    connect(this, SIGNAL(hideAnimationFinished()), m_autoHideTimer, SLOT(stop()));
}

void MessageWidget::setAnimate(bool state)
{
    m_animate = state;
}

int MessageWidget::autoHideTimeout() const
{
    return m_autoHideTimeout;
}

void MessageWidget::showMessage(const QString& text, MessageWidget::MessageType type)
{
    showMessage(text, type, m_autoHideTimeout);
}

void MessageWidget::showMessage(const QString& text, KMessageWidget::MessageType type, int autoHideTimeout)
{
    setMessageType(type);
    setText(text);

    // KMessageWidget does not expose its visible message text as the
    // accessible name of the outer widget. Set it here so the Alert event
    // below has useful text for assistive technologies.
    setAccessibleName(text);

    emit showAnimationStarted();
    if (m_animate) {
        animatedShow();
    } else {
        show();
        emit showAnimationFinished();
    }

    // This banner can appear without moving keyboard focus (e.g. the
    // "Press ESC again to close this database" warning in
    // DatabaseOpenWidget, shown while focus stays in the password field).
    // Without this, a screen reader has no way to know the message
    // appeared at all -- pressing a key that only produces this warning
    // looks like it silently did nothing.
    //
    // QAccessible::Alert alone does NOT achieve this on Windows: confirmed
    // directly against qtbase's qwindowsuiaaccessibility.cpp
    // (QWindowsUiaAccessibility::notifyAccessibilityUpdate()), Alert only
    // triggers a system sound there and is never forwarded to UI Automation
    // as an event -- screen readers get no notification from it. Keep
    // firing it anyway for the sound cue, but pair it with
    // QAccessibleAnnouncementEvent (Qt 6.8+), which is what actually maps
    // to a real UIA notification (UiaRaiseNotificationEvent).
    QAccessibleEvent alertEvent(this, QAccessible::Alert);
    QAccessible::updateAccessibility(&alertEvent);
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    QAccessibleAnnouncementEvent announcementEvent(this, text);
    // Error/Warning need to interrupt whatever the user is doing (e.g. the
    // "Press ESC again to close this database" case above); other message
    // types can wait for a natural pause.
    if (type == KMessageWidget::Error || type == KMessageWidget::Warning) {
        announcementEvent.setPoliteness(QAccessible::AnnouncementPoliteness::Assertive);
    }
    QAccessible::updateAccessibility(&announcementEvent);
#endif

    if (autoHideTimeout > 0) {
        m_autoHideTimer->start(autoHideTimeout);
    } else {
        m_autoHideTimer->stop();
    }
}

void MessageWidget::hideMessage()
{
    emit hideAnimationStarted();
    if (m_animate) {
        animatedHide();
    } else {
        hide();
        emit hideAnimationFinished();
    }

    m_autoHideTimer->stop();
}

void MessageWidget::setAutoHideTimeout(int autoHideTimeout)
{
    m_autoHideTimeout = autoHideTimeout;
    if (autoHideTimeout <= 0) {
        m_autoHideTimer->stop();
    }
}

/**
 * Open a link using the system's default handler.
 * Links that are not HTTP(S) links are ignored.
 *
 * @param link link URL
 */
void MessageWidget::openHttpUrl(const QString& link)
{
    if (link.startsWith("http://") || link.startsWith("https://")) {
        QDesktopServices::openUrl(QUrl(link));
    }
}
