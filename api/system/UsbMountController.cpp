#include "UsbMountController.hpp"
#include <QProcess>
#include <QDebug>

UsbMountController::UsbMountController(QObject *parent) : QObject(parent) {}

QJsonObject UsbMountController::mount() {
    QJsonObject response;
    QProcess proc;
    proc.start("lsblk", {"-o", "NAME,TYPE,RM,MOUNTPOINT", "-r"});
    proc.waitForFinished();
    QStringList lines = QString(proc.readAllStandardOutput()).split('\n');

    QString device;
    for (const QString &line : lines) {
        QStringList parts = line.split(QRegExp("\\s+"));
        if (parts.size() == 3 && parts[1] == "part" && parts[2] == "1") {
            device = "/dev/" + parts[0];
            break;
        }
    }

    if (device.isEmpty()) {
        response["error"] = "No removable device found.";
        return response;
    }

    proc.start("sudo", {"mount", device, "/mnt/usb"});
    proc.waitForFinished();
    response["message"] = QString("Mounted %1 to /mnt/usb").arg(device);
    return response;
}

QJsonObject UsbMountController::unmount() {
    QJsonObject response;
    QProcess proc;
    proc.start("lsblk", {"-o", "MOUNTPOINT", "-r"});
    proc.waitForFinished();

    if (!QString(proc.readAllStandardOutput()).contains("/mnt/usb")) {
        response["error"] = "No device mounted at /mnt/usb.";
        return response;
    }

    proc.start("sudo", {"umount", "/mnt/usb"});
    proc.waitForFinished();
    response["message"] = "Unmounted device from /mnt/usb";
    return response;
}