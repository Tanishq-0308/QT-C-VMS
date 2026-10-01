#include "RecordTargetDialog.hpp"
#include "UsbDeviceDialog.hpp"
#include "core/MediaInfo.hpp"
#include "core/TransferManager.hpp"

#include <QMessageBox>
#include <QPushButton>

namespace RecordTargetDialog {

int minutesFor(qint64 bytes, qint64 bytesPerSecond) {
    return bytesPerSecond > 0 ? int(bytes / bytesPerSecond / 60) : 0;
}

static bool confirm(QWidget* parent, const QString& title, const QString& text) {
    QMessageBox box(QMessageBox::Warning, title, text + "\n\nRecord anyway?", QMessageBox::NoButton, parent);
    QPushButton* yes = box.addButton("Record anyway", QMessageBox::AcceptRole);
    box.addButton("Don't record", QMessageBox::RejectRole);
    box.setDefaultButton(yes);
    box.exec();
    return box.clickedButton() == yes;
}

bool ask(QWidget* parent, TransferManager* transfers, qint64 bytesPerSecond, QList<UsbUtils::Device>* mirrorTo) {
    mirrorTo->clear();
    const QList<UsbUtils::Device> devices = UsbUtils::listDevices();
    if (devices.isEmpty())
        return true;   // no USB device: record on this system, nothing to ask

    QMessageBox where(QMessageBox::Question, "Start recording",
                      devices.size() == 1
                          ? QString("A USB device is connected (%1).\nWhere should this recording be saved?").arg(devices.first().name)
                          : QString("%1 USB devices are connected.\nWhere should this recording be saved?").arg(devices.size()),
                      QMessageBox::NoButton, parent);
    QPushButton* local = where.addButton("This system only", QMessageBox::AcceptRole);
    QPushButton* both = where.addButton("This system and USB", QMessageBox::AcceptRole);
    where.addButton(QMessageBox::Cancel);
    where.setDefaultButton(local);
    where.exec();
    if (where.clickedButton() == local)
        return true;
    if (where.clickedButton() != both)
        return false;   // cancelled: don't record

    const QList<UsbUtils::Device> chosen = UsbDeviceDialog::choose(
        parent, devices, 0, "Record to USB", "Also save this recording to:", "Record");
    if (chosen.isEmpty())
        return false;

    for (const UsbUtils::Device& device : chosen) {
        if (device.fat32) {
            const int minutes = minutesFor(UsbUtils::kFat32MaxFileSize, bytesPerSecond);
            if (!confirm(parent, "FAT32 device",
                         QString("%1 is FAT32: it can hold at most 4 GB per file, about %2 minutes of this "
                                 "recording.\nAfter that the copy on %1 stops; the recording continues on this system.")
                             .arg(device.name).arg(minutes)))
                return false;
        }
        const int minutesFree = minutesFor(device.bytesFree, bytesPerSecond);
        if (minutesFree < 30) {
            if (!confirm(parent, "Little space on USB",
                         QString("%1 has space for about %2 minutes of this recording (%3 free).\n"
                                 "When it is full the copy on %1 stops; the recording continues on this system.")
                             .arg(device.name).arg(minutesFree).arg(MediaInfo::formatSize(device.bytesFree))))
                return false;
        }
        if (transfers && transfers->isDeviceBusy(device.mountPath)) {
            if (!confirm(parent, "USB device busy",
                         QString("%1 is still copying downloaded files (Transfers).\n"
                                 "The recording copy shares its speed and may fall behind.").arg(device.name)))
                return false;
        }
    }
    *mirrorTo = chosen;
    return true;
}

} // namespace RecordTargetDialog
