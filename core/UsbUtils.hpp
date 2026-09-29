#pragma once

#include <QString>

// USB stick helpers shared by every page that reads from or writes to a pendrive.
namespace UsbUtils {

// Root of the first removable volume mounted by the desktop (/media/<user>/<label> or
// /run/media/<user>/<label>), or an empty string when no stick is mounted. With
// `requireWritable`, read-only volumes are skipped.
// Only real mounts count: an empty folder left behind under /media after a stick was pulled
// is ignored.
QString findUsbMount(bool requireWritable = true);

// True when the volume holding `path` is FAT32 (vfat/msdos), which cannot store files of
// 4 GiB or more.
bool isFat32(const QString& path);

// Largest file size FAT32 can store (4 GiB - 1).
constexpr qint64 kFat32MaxFileSize = (qint64(4) << 30) - 1;

} // namespace UsbUtils
