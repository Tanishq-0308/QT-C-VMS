// transfer_benchmark: measures how fast the app exports videos and snapshots to a USB device,
// using the app's own copy code (core/TransferManager, the Download button's queue).
//
//   transfer_benchmark <work dir> <snapshot source image> <output json> [reps]
//
// Videos are files of the size the app records (1080p60, ~121 MB per minute) filled with random
// bytes: H.264 is incompressible, so it copies exactly like a real recording. Snapshots are
// 1920x1080 JPEGs at quality 95, saved the way DeckLinkOpenGLWidget::saveSnapshot saves them.
// Each run copies into a folder on the device that is deleted afterwards; source files are
// dropped from the page cache before every run.

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QStorageInfo>
#include <QTimer>
#include <QRandomGenerator>
#include <fcntl.h>
#include <unistd.h>

#include "core/TransferManager.hpp"
#include "core/UsbUtils.hpp"

namespace {

constexpr qint64 MB = 1000 * 1000;
constexpr qint64 kVideoBytesPerMinute = 121 * MB;   // 1920*1080*60*0.13 bit/s (VideoRecorder)

void dropFromCache(const QString& path)
{
    const int fd = ::open(path.toLocal8Bit().constData(), O_RDONLY);
    if (fd >= 0) {
        ::posix_fadvise(fd, 0, 0, POSIX_FADV_DONTNEED);
        ::close(fd);
    }
}

bool makeRandomFile(const QString& path, qint64 size)
{
    if (QFileInfo(path).size() == size)
        return true;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    QByteArray block(8 << 20, Qt::Uninitialized);
    auto* words = reinterpret_cast<quint32*>(block.data());
    for (qint64 left = size; left > 0;) {
        QRandomGenerator::global()->fillRange(words, block.size() / 4);
        const qint64 n = qMin<qint64>(left, block.size());
        if (f.write(block.constData(), n) != n)
            return false;
        left -= n;
    }
    return true;
}

// Distinct 1080p frames from one camera image (crop moves a little, timestamp burned in)
QStringList makeSnapshots(const QString& dir, const QImage& camera, int count)
{
    QStringList files;
    for (int i = 0; i < count; ++i) {
        const QString path = QString("%1/snapshot_%2.jpg").arg(dir).arg(i, 3, 10, QChar('0'));
        files << path;
        if (QFile::exists(path))
            continue;
        const int dx = (i * 7) % 40, dy = (i * 5) % 24;
        QImage frame = camera.copy(dx, dy, camera.width() - 40, camera.height() - 24)
                           .scaled(1920, 1080, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                           .convertToFormat(QImage::Format_RGB32);
        QPainter p(&frame);
        p.setPen(Qt::white);
        p.setFont(QFont("Arial", 28, QFont::Bold));
        p.drawText(40, 60, QString("Snapshot %1").arg(i + 1));
        p.end();
        frame.save(path, "JPG", 95);
    }
    return files;
}

struct RunResult { double seconds = 0; qint64 bytes = 0; int files = 0; bool ok = false; double shownMBps = 0; };

// One export through TransferManager: enqueue -> queue drained, as the user experiences it
RunResult exportFiles(const QStringList& sources, const QString& destDir)
{
    for (const QString& s : sources)
        dropFromCache(s);
    QDir(destDir).removeRecursively();
    ::sync();

    RunResult r;
    for (const QString& s : sources)
        r.bytes += QFileInfo(s).size();
    r.files = sources.size();

    TransferManager m;
    QEventLoop loop;
    int succeeded = 0;
    QObject::connect(&m, &TransferManager::queueDrained, &loop, [&](int ok, int, int) {
        succeeded = ok;
        loop.quit();
    });
    // What the Transfers drawer shows while a file copies (sampled once it has settled)
    QList<double> shown;
    QObject::connect(&m, &TransferManager::jobChanged, &loop, [&](int id) {
        const TransferManager::Job* j = m.job(id);
        if (j && j->state == TransferManager::State::Copying && j->bytesPerSecond > 0 && j->bytesDone > 64 * MB)
            shown << j->bytesPerSecond;
    });

    QElapsedTimer clock;
    clock.start();
    m.enqueue(sources, destDir);
    loop.exec();
    r.seconds = clock.nsecsElapsed() / 1e9;
    r.ok = succeeded == sources.size();
    if (!shown.isEmpty()) {
        double sum = 0;
        for (double v : shown) sum += v;
        r.shownMBps = sum / shown.size() / MB;
    }
    QDir(destDir).removeRecursively();
    ::sync();
    return r;
}

// The device's own sequential write speed, without the app: one file, one sync at the end
double rawWriteMBps(const QString& destDir, qint64 size)
{
    QDir().mkpath(destDir);
    const QString path = destDir + "/raw.bin";
    QByteArray block(8 << 20, Qt::Uninitialized);
    QRandomGenerator::global()->fillRange(reinterpret_cast<quint32*>(block.data()), block.size() / 4);
    const int fd = ::open(path.toLocal8Bit().constData(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
        return -1;
    QElapsedTimer clock;
    clock.start();
    for (qint64 left = size; left > 0; left -= block.size())
        if (::write(fd, block.constData(), block.size()) != block.size()) { ::close(fd); return -1; }
    ::fdatasync(fd);
    const double seconds = clock.nsecsElapsed() / 1e9;
    ::close(fd);
    QDir(destDir).removeRecursively();
    ::sync();
    return size / seconds / MB;
}

} // namespace

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);   // QImage/QPainter for the snapshots
    if (argc < 4) {
        fprintf(stderr, "usage: %s <work dir> <snapshot source image> <output json> [reps]\n", argv[0]);
        return 2;
    }
    const QString work = argv[1];
    const QImage camera(argv[2]);
    const QString outJson = argv[3];
    const int reps = argc > 4 ? atoi(argv[4]) : 3;
    if (camera.isNull()) { fprintf(stderr, "cannot read %s\n", argv[2]); return 2; }

    const QString mount = UsbUtils::findUsbMount();
    if (mount.isEmpty()) { fprintf(stderr, "no USB device mounted\n"); return 1; }
    const QString dest = mount + "/transfer_benchmark_tmp";
    QDir().mkpath(work + "/snapshots");

    QJsonObject out;
    const QStorageInfo st(mount);
    out["mount"] = mount;
    out["filesystem"] = QString::fromLatin1(st.fileSystemType());
    out["capacity_bytes"] = double(st.bytesTotal());

    auto log = [](const QString& s) { printf("%s\n", qPrintable(s)); fflush(stdout); };

    // Baseline
    QJsonArray raw;
    for (int i = 0; i < 2; ++i) {
        const double v = rawWriteMBps(dest, 1024LL << 20);
        raw.append(v);
        log(QString("baseline raw write 1 GiB: %1 MB/s").arg(v, 0, 'f', 1));
    }
    out["raw_write_MBps"] = raw;

    struct Scenario { QString name, kind; QStringList files; int reps; };
    QList<Scenario> scenarios;
    for (const auto& v : QList<QPair<int, int>>{{1, reps}, {10, reps}, {30, qMax(1, reps - 1)}}) {
        const QString path = QString("%1/video_%2min.mp4").arg(work).arg(v.first);
        if (!makeRandomFile(path, v.first * kVideoBytesPerMinute)) { fprintf(stderr, "cannot create %s\n", qPrintable(path)); return 1; }
        scenarios.append({QString("Video, %1 min of 1080p60").arg(v.first), "video", {path}, v.second});
    }
    const QStringList snaps = makeSnapshots(work + "/snapshots", camera, 100);
    for (int n : {1, 10, 50, 100})
        scenarios.append({QString("%1 snapshot%2").arg(n).arg(n > 1 ? "s" : ""), "snapshot", snaps.mid(0, n), reps});
    scenarios.append({"10-min video + 20 snapshots", "mixed",
                      QStringList{work + "/video_10min.mp4"} + snaps.mid(0, 20), qMax(1, reps - 1)});

    QJsonArray results;
    for (const Scenario& sc : scenarios) {
        QJsonArray runs;
        for (int i = 0; i < sc.reps; ++i) {
            const RunResult r = exportFiles(sc.files, dest);
            if (!r.ok) { fprintf(stderr, "export failed: %s\n", qPrintable(sc.name)); return 1; }
            QJsonObject o;
            o["seconds"] = r.seconds;
            o["MBps"] = r.bytes / r.seconds / MB;
            o["files_per_s"] = r.files / r.seconds;
            if (r.shownMBps > 0) o["shown_MBps"] = r.shownMBps;
            runs.append(o);
            log(QString("%1 run %2: %3 MB in %4 s = %5 MB/s%6").arg(sc.name).arg(i + 1)
                    .arg(r.bytes / double(MB), 0, 'f', 1).arg(r.seconds, 0, 'f', 2)
                    .arg(r.bytes / r.seconds / MB, 0, 'f', 1)
                    .arg(r.shownMBps > 0 ? QString(" (app showed %1 MB/s)").arg(r.shownMBps, 0, 'f', 1) : QString()));
        }
        qint64 bytes = 0;
        for (const QString& f : sc.files) bytes += QFileInfo(f).size();
        QJsonObject s;
        s["name"] = sc.name;
        s["kind"] = sc.kind;
        s["files"] = sc.files.size();
        s["bytes"] = double(bytes);
        s["runs"] = runs;
        results.append(s);
    }
    out["scenarios"] = results;

    QFile f(outJson);
    if (!f.open(QIODevice::WriteOnly)) return 1;
    f.write(QJsonDocument(out).toJson());
    QDir(dest).removeRecursively();
    log("done: " + outJson);
    return 0;
}
