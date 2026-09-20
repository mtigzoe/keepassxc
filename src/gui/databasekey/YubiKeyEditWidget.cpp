/*
 *  Copyright (C) 2018 KeePassXC Team <team@keepassxc.org>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 or (at your option)
 *  version 3 of the License.
 */

#include "YubiKeyEditWidget.h"

#include "ui_KeyComponentWidget.h"
#include "ui_YubiKeyEditWidget.h"

#include "core/AsyncTask.h"
#include "gui/Icons.h"
#include "keys/ChallengeResponseKey.h"
#include "keys/CompositeKey.h"
#include "keys/drivers/YubiKeyInterfaceUSB.h"

#include <QAccessible>

YubiKeyEditWidget::YubiKeyEditWidget(QWidget* parent)
    : KeyComponentWidget(parent)
    , m_compUi(new Ui::YubiKeyEditWidget())
    , m_deviceListener(new DeviceListener(this))
{
    initComponent();
    connect(YubiKey::instance(), SIGNAL(detectComplete(bool)), SLOT(hardwareKeyResponse(bool)), Qt::QueuedConnection);
    connect(m_deviceListener, &DeviceListener::devicePlugged, this, [&](bool, void*, void*) { pollYubikey(); });
}

YubiKeyEditWidget::~YubiKeyEditWidget() = default;

bool YubiKeyEditWidget::addToCompositeKey(QSharedPointer<CompositeKey> key)
{
    if (!m_isDetected || !m_compEditWidget) {
        return false;
    }

    int selectionIndex = m_compUi->comboChallengeResponse->currentIndex();
    auto slot = m_compUi->comboChallengeResponse->itemData(selectionIndex).value<YubiKeySlot>();
    key->addChallengeResponseKey(QSharedPointer<ChallengeResponseKey>::create(slot));
    return true;
}

bool YubiKeyEditWidget::validate(QString& errorMessage) const
{
    if (!m_isDetected) {
        errorMessage = tr("Could not find any hardware keys!");
        return false;
    }

    int selectionIndex = m_compUi->comboChallengeResponse->currentIndex();
    auto slot = m_compUi->comboChallengeResponse->itemData(selectionIndex).value<YubiKeySlot>();
    bool valid = AsyncTask::runAndWaitForFuture([&slot] { return YubiKey::instance()->testChallenge(slot); });
    if (!valid) {
        errorMessage = tr("Selected hardware key slot does not support challenge-response!");
    }
    return valid;
}

QWidget* YubiKeyEditWidget::componentEditWidget()
{
    m_compEditWidget = new QWidget();
    m_compUi->setupUi(m_compEditWidget);

    QSizePolicy sp = m_compUi->yubikeyProgress->sizePolicy();
    sp.setRetainSizeWhenHidden(true);
    m_compUi->yubikeyProgress->setSizePolicy(sp);
    m_compUi->yubikeyProgress->setVisible(false);

    return m_compEditWidget;
}

void YubiKeyEditWidget::showEvent(QShowEvent* event)
{
    KeyComponentWidget::showEvent(event);

#ifdef Q_OS_WIN
    m_deviceListener->registerHotplugCallback(true,
                                              true,
                                              YubiKeyInterfaceUSB::YUBICO_USB_VID,
                                              DeviceListener::MATCH_ANY,
                                              &DeviceListenerWin::DEV_CLS_KEYBOARD);
    m_deviceListener->registerHotplugCallback(true,
                                              true,
                                              YubiKeyInterfaceUSB::ONLYKEY_USB_VID,
                                              DeviceListener::MATCH_ANY,
                                              &DeviceListenerWin::DEV_CLS_KEYBOARD);
#else
    m_deviceListener->registerHotplugCallback(true, true, YubiKeyInterfaceUSB::YUBICO_USB_VID);
    m_deviceListener->registerHotplugCallback(true, true, YubiKeyInterfaceUSB::ONLYKEY_USB_VID);
#endif
}

void YubiKeyEditWidget::hideEvent(QHideEvent* event)
{
    KeyComponentWidget::hideEvent(event);
    m_deviceListener->deregisterAllHotplugCallbacks();
}

void YubiKeyEditWidget::initComponentEditWidget(QWidget* widget)
{
    Q_UNUSED(widget);
    Q_ASSERT(m_compEditWidget);
    m_compUi->comboChallengeResponse->setFocus();
    m_compUi->refreshHardwareKeys->setIcon(icons()->icon("yubikey-refresh", true));
    connect(m_compUi->refreshHardwareKeys, &QPushButton::clicked, this, &YubiKeyEditWidget::pollYubikey);
    pollYubikey();
}

void YubiKeyEditWidget::initComponent()
{
    m_ui->groupBox->setTitle(tr("Challenge-Response"));
    m_ui->addButton->setText(tr("Add Challenge-Response"));
    m_ui->changeButton->setText(tr("Change Challenge-Response"));
    m_ui->removeButton->setText(tr("Remove Challenge-Response"));
    m_ui->changeOrRemoveLabel->setText(tr("Challenge-Response set, click to change or remove"));

    m_ui->componentDescription->setText(
        tr("<p>If you own a <a href=\"https://www.yubico.com/\">YubiKey</a> or "
           "<a href=\"https://onlykey.io\">OnlyKey</a>, you can use it for additional security.</p>"
           "<p>The key requires one of its slots to be programmed with "
           "<a href=\"https://keepassxc.org/docs/#faq-yubikey-howto\">"
           "Challenge-Response</a>.</p>"));
}

void YubiKeyEditWidget::pollYubikey()
{
    if (!m_compEditWidget) {
        return;
    }

    // Both controls can be focused when a refresh is triggered. Disabling a
    // focused widget leaves Qt/Windows screen-reader focus on an unusable
    // control. Move focus to the Cancel button before disabling either one.
    if (m_compUi->comboChallengeResponse->hasFocus() || m_compUi->refreshHardwareKeys->hasFocus()) {
        m_ui->cancelButton->setFocus();
    }

    m_isDetected = false;
    m_compUi->comboChallengeResponse->clear();
    m_compUi->comboChallengeResponse->addItem(tr("Detecting hardware keys…"));
    m_compUi->comboChallengeResponse->setEnabled(false);
    m_compUi->yubikeyProgress->setVisible(true);
    m_compUi->refreshHardwareKeys->setEnabled(false);

    YubiKey::instance()->findValidKeysAsync();
}

void YubiKeyEditWidget::hardwareKeyResponse(bool found)
{
    if (!m_compEditWidget) {
        return;
    }

    m_compUi->comboChallengeResponse->clear();
    m_compUi->refreshHardwareKeys->setEnabled(true);

    if (!found) {
        m_compUi->yubikeyProgress->setVisible(false);
        const auto message = YubiKey::instance()->connectedKeys() > 0
                                  ? tr("Hardware keys found, but no slots are configured")
                                  : tr("No hardware keys detected");
        m_compUi->comboChallengeResponse->addItem(message);
        m_isDetected = false;
        QAccessibleEvent alertEvent(m_compUi->comboChallengeResponse, QAccessible::Alert);
        QAccessible::updateAccessibility(&alertEvent);
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
        QAccessibleAnnouncementEvent announcementEvent(m_compUi->comboChallengeResponse, message);
        QAccessible::updateAccessibility(&announcementEvent);
#endif
        return;
    }

    const auto foundKeys = YubiKey::instance()->foundKeys();
    for (auto i = foundKeys.cbegin(); i != foundKeys.cend(); ++i) {
        m_compUi->comboChallengeResponse->addItem(i.value(), QVariant::fromValue(i.key()));
    }

    m_isDetected = true;
    m_compUi->yubikeyProgress->setVisible(false);
    m_compUi->comboChallengeResponse->setEnabled(true);
    QAccessibleEvent alertEvent(m_compUi->comboChallengeResponse, QAccessible::Alert);
    QAccessible::updateAccessibility(&alertEvent);
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    QAccessibleAnnouncementEvent announcementEvent(
        m_compUi->comboChallengeResponse, tr("%n hardware key slot(s) detected", "", foundKeys.size()));
    QAccessible::updateAccessibility(&announcementEvent);
#endif
}
