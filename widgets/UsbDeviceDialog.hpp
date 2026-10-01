#pragma once

#include <QList>
#include <QString>
#include "core/UsbUtils.hpp"

class QWidget;

namespace UsbDeviceDialog {

// Which of the connected USB devices to download to. With one device there is nothing to ask:
// it is returned as is. With several, a dialog lets the user tick one or more (the last choice
// is pre-ticked); an empty list means the user cancelled.
// `largestFileBytes` marks FAT32 devices that can't take the largest file (over 4 GB).
QList<UsbUtils::Device> choose(QWidget* parent, const QList<UsbUtils::Device>& devices,
                               qint64 largestFileBytes = 0,
                               const QString& title = "Download to USB",
                               const QString& prompt = "Choose the USB device(s) to download to:",
                               const QString& acceptText = "Download");

// One line describing a device: "KINGSTON — 28.1 GB free of 29.0 GB · exFAT"
QString describe(const UsbUtils::Device& device);

} // namespace UsbDeviceDialog
