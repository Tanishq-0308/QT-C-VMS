#include "MediaInfo.hpp"

#include <QFileInfo>
#include <QImageReader>
#include <QSize>

extern "C" {
#include <libavformat/avformat.h>
}

namespace MediaInfo {

QString formatSize(qint64 bytes) {
    if (bytes < 0)
        return QString();
    const double kb = bytes / 1000.0, mb = kb / 1000.0, gb = mb / 1000.0;
    if (gb >= 1.0)
        return QString("%1 GB").arg(gb, 0, 'f', gb >= 10.0 ? 1 : 2);
    if (mb >= 1.0)
        return QString("%1 MB").arg(mb, 0, 'f', mb >= 10.0 ? 0 : 1);
    if (kb >= 1.0)
        return QString("%1 KB").arg(kb, 0, 'f', 0);
    return QString("%1 B").arg(bytes);
}

QString formatDuration(qint64 ms) {
    if (ms < 0)
        return QString();
    const qint64 total = (ms + 500) / 1000;   // nearest second
    const qint64 h = total / 3600, m = (total % 3600) / 60, s = total % 60;
    if (h > 0)
        return QString("%1:%2:%3").arg(h).arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0'));
    return QString("%1:%2").arg(m).arg(s, 2, 10, QChar('0'));
}

qint64 videoDurationMs(const QString& path) {
    AVFormatContext* ctx = nullptr;
    if (avformat_open_input(&ctx, path.toUtf8().constData(), nullptr, nullptr) < 0)
        return -1;
    qint64 ms = -1;
    // The recorder writes fragmented MP4 (no total length in the header), so the length comes
    // from the stream's timestamps, which find_stream_info works out
    if (avformat_find_stream_info(ctx, nullptr) >= 0 && ctx->duration != AV_NOPTS_VALUE && ctx->duration > 0)
        ms = ctx->duration / (AV_TIME_BASE / 1000);
    avformat_close_input(&ctx);
    return ms;
}

QString describe(const QString& path, bool isVideo) {
    const QFileInfo info(path);
    if (!info.exists())
        return QStringLiteral("File not found");
    const QString size = formatSize(info.size());
    if (isVideo) {
        const QString duration = formatDuration(videoDurationMs(path));
        return duration.isEmpty() ? size : duration + QStringLiteral(" · ") + size;
    }
    const QSize pixels = QImageReader(path).size();   // reads the header only
    return pixels.isValid() ? QString("%1 × %2 · %3").arg(pixels.width()).arg(pixels.height()).arg(size) : size;
}

} // namespace MediaInfo
