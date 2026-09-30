#pragma once

#include <QString>

// Short facts about a recording or snapshot file, for the gallery cards
namespace MediaInfo {

QString formatSize(qint64 bytes);      // "566 KB", "1.21 GB"
QString formatDuration(qint64 ms);     // "0:45", "12:34", "1:02:03"

// Length of a video in milliseconds, -1 if it can't be read. Opens the file: not for the GUI thread.
qint64 videoDurationMs(const QString& path);

// One line for a card: "12:34 · 1.21 GB" for a video, "1920 × 1080 · 566 KB" for an image,
// "File not found" if it is gone. Opens the file: not for the GUI thread.
QString describe(const QString& path, bool isVideo);

} // namespace MediaInfo
