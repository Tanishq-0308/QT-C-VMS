#include "UsbUtils.hpp"

#include <QStorageInfo>

namespace UsbUtils {

QString findUsbMount(bool requireWritable) {
    for (const QStorageInfo& volume : QStorageInfo::mountedVolumes()) {
        const QString root = volume.rootPath();
        const bool removableMount = root.startsWith("/media/") || root.startsWith("/run/media/");
        if (removableMount && volume.isValid() && volume.isReady() &&
            !(requireWritable && volume.isReadOnly()))
            return root;
    }
    return QString();
}

bool isFat32(const QString& path) {
    const QByteArray type = QStorageInfo(path).fileSystemType().toLower();
    return type == "vfat" || type == "msdos" || type == "fat" || type == "fat32";
}

} // namespace UsbUtils
