#include "UsbUtils.hpp"

#include <QDir>
#include <QFileInfo>
#include <QStorageInfo>
#include <algorithm>

namespace UsbUtils {

static bool isFat32Type(const QByteArray& fileSystemType) {
    const QByteArray type = fileSystemType.toLower();
    return type == "vfat" || type == "msdos" || type == "fat" || type == "fat32";
}

static QString fileSystemName(const QByteArray& fileSystemType) {
    const QByteArray type = fileSystemType.toLower();
    if (isFat32Type(type))
        return QStringLiteral("FAT32");
    if (type == "exfat")
        return QStringLiteral("exFAT");
    if (type == "ntfs" || type == "ntfs3" || type == "fuseblk")   // ntfs-3g mounts show as fuseblk
        return QStringLiteral("NTFS");
    return QString::fromLatin1(fileSystemType);
}

QList<Device> listDevices(bool requireWritable) {
    QList<Device> devices;
    for (const QStorageInfo& volume : QStorageInfo::mountedVolumes()) {
        const QString root = volume.rootPath();
        const bool removableMount = root.startsWith("/media/") || root.startsWith("/run/media/");
        if (!removableMount || !volume.isValid() || !volume.isReady() ||
            (requireWritable && volume.isReadOnly()))
            continue;
        Device device;
        device.mountPath = root;
        device.name = volume.name().isEmpty() ? QFileInfo(root).fileName() : volume.name();
        device.fileSystem = fileSystemName(volume.fileSystemType());
        device.bytesFree = volume.bytesAvailable();
        device.bytesTotal = volume.bytesTotal();
        device.fat32 = isFat32Type(volume.fileSystemType());
        devices.append(device);
    }
    std::sort(devices.begin(), devices.end(),
              [](const Device& a, const Device& b) { return a.mountPath < b.mountPath; });
    return devices;
}

QString volumeRoot(const QString& path) {
    // A folder that doesn't exist yet (e.g. <stick>/SurgeryDownloads) has no storage info
    QString existing = QDir::cleanPath(path);
    while (!QFileInfo::exists(existing)) {
        const QString parent = QFileInfo(existing).path();
        if (parent == existing)
            break;
        existing = parent;
    }
    return QStorageInfo(existing).rootPath();
}

QString findUsbMount(bool requireWritable) {
    const QList<Device> devices = listDevices(requireWritable);
    return devices.isEmpty() ? QString() : devices.first().mountPath;
}

bool isFat32(const QString& path) {
    return isFat32Type(QStorageInfo(path).fileSystemType());
}

} // namespace UsbUtils
