#include "UsbDeviceDialog.hpp"
#include "core/MediaInfo.hpp"
#include "core/UIScale.hpp"

#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLabel>
#include <QPushButton>
#include <QSet>
#include <QVBoxLayout>

namespace UsbDeviceDialog {

QString describe(const UsbUtils::Device& device) {
    return QString("%1 — %2 free of %3 · %4")
        .arg(device.name, MediaInfo::formatSize(device.bytesFree), MediaInfo::formatSize(device.bytesTotal),
             device.fileSystem);
}

QList<UsbUtils::Device> choose(QWidget* parent, const QList<UsbUtils::Device>& devices, qint64 largestFileBytes) {
    if (devices.size() <= 1)
        return devices;

    static QSet<QString> lastChoice;   // mount paths ticked last time

    const int fontPx = UIScale::fontSize(36, 16, 28, parent);
    QDialog dialog(parent);
    dialog.setWindowTitle("Download to USB");
    dialog.setStyleSheet(QString("QLabel, QCheckBox, QPushButton { font-size: %1px; }"
                                 "QCheckBox { padding: %2px 0; }"
                                 "QCheckBox::indicator { width: %3px; height: %3px; }"
                                 "QPushButton { padding: %2px %4px; }")
                             .arg(fontPx).arg(fontPx / 3).arg(fontPx * 5 / 4).arg(fontPx));

    auto* layout = new QVBoxLayout(&dialog);
    layout->setSpacing(fontPx / 2);
    layout->addWidget(new QLabel("Choose the USB device(s) to download to:"));

    QList<QCheckBox*> boxes;
    bool anyTicked = false;
    for (const UsbUtils::Device& device : devices) {
        auto* box = new QCheckBox(describe(device));
        box->setChecked(lastChoice.contains(device.mountPath));
        anyTicked = anyTicked || box->isChecked();
        boxes << box;
        layout->addWidget(box);
        if (device.fat32 && largestFileBytes > UsbUtils::kFat32MaxFileSize) {
            auto* note = new QLabel("     FAT32: files over 4 GB will not be copied to this device");
            note->setStyleSheet(QString("color: #c40000; font-size: %1px;").arg(fontPx * 4 / 5));
            layout->addWidget(note);
        }
    }
    if (!anyTicked)
        boxes.first()->setChecked(true);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
    QPushButton* download = buttons->addButton("Download", QDialogButtonBox::AcceptRole);
    layout->addWidget(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    // Nothing ticked: nothing to download to
    auto updateDownload = [&]() {
        bool any = false;
        for (QCheckBox* box : boxes)
            any = any || box->isChecked();
        download->setEnabled(any);
    };
    for (QCheckBox* box : boxes)
        QObject::connect(box, &QCheckBox::toggled, &dialog, updateDownload);
    updateDownload();

    if (dialog.exec() != QDialog::Accepted)
        return {};

    QList<UsbUtils::Device> chosen;
    lastChoice.clear();
    for (int i = 0; i < devices.size(); ++i) {
        if (boxes[i]->isChecked()) {
            chosen << devices[i];
            lastChoice.insert(devices[i].mountPath);
        }
    }
    return chosen;
}

} // namespace UsbDeviceDialog
