// tst_features (offscreen): the background USB copy queue, USB detection, the "Generate Report"
// button, the Archive heading and the SVG icons.
//
//  * TransferManager / UsbUtils: real copies between temporary folders. The FAT32 test only
//    *checks* a mounted FAT32 stick (it is skipped without one) and never writes to it.
//  * Generate Report: a fake service listens on the real port (127.0.0.1:8001). The real report
//    service must be stopped for these tests (they skip when the port is busy), so no report is
//    ever generated.
//  * Copy speed: measured on the USB stick if one is mounted, in a test folder that is removed.
//
// Uses an EMPTY database built from database/migrations/init.sql (no patient data).

#include <QtTest>
#include <QApplication>
#include <QFrame>
#include <QIcon>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QCheckBox>
#include <QDialog>
#include <QInputDialog>
#include <QLineEdit>
#include <QProcess>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStorageInfo>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>

#include "common/TestSupport.hpp"
#include "common/ModalCloser.hpp"

#include "core/MediaInfo.hpp"
#include "core/MediaRename.hpp"
#include "core/TransferManager.hpp"
#include "core/UIScale.hpp"
#include "core/UsbUtils.hpp"
#include "ui/SurgeryRecordPage/SurgeryRecordingPage.hpp"
#include "widgets/Toast.hpp"
#include "widgets/UsbDeviceDialog.hpp"

namespace {

using State = TransferManager::State;

QByteArray patternBytes(qint64 size, int seed)
{
    QByteArray b(int(size), Qt::Uninitialized);
    for (int i = 0; i < b.size(); ++i) {
        const quint32 u = quint32(i);   // unsigned: wraps instead of overflowing on large files
        b[i] = char((u * 31u + quint32(seed) * 7u + (u >> 11)) & 0xffu);
    }
    return b;
}

bool writeFile(const QString& path, const QByteArray& data)
{
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(data) == data.size();
}

// A file of the given size without using disk space (holes); false if the filesystem can't
bool makeSparse(const QString& path, qint64 size)
{
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.resize(size) && f.size() == size;
}

QByteArray readAll(const QString& path)
{
    QFile f(path);
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

// QSaveFile leaves "<name>.XXXXXX" next to the target while writing
QStringList leftovers(const QString& dir)
{
    return QDir(dir).entryList(QDir::Files).filter(QRegularExpression("\\.[A-Za-z0-9]{6}$"));
}

const TransferManager::Job* jobFor(const TransferManager& m, const QString& fileName)
{
    for (const TransferManager::Job& j : m.jobs())
        if (j.fileName == fileName)
            return m.job(j.id);
    return nullptr;
}

QPushButton* findButton(QWidget* root, const QString& text)
{
    for (QPushButton* b : root->findChildren<QPushButton*>())
        if (b->text() == text) return b;
    return nullptr;
}

// Stands in for the Flask report service on its port
class FakeReportService : public QObject
{
public:
    enum class Mode { Respond, Drop };

    FakeReportService()
    {
        QObject::connect(&m_server, &QTcpServer::newConnection, this, [this]() {
            while (QTcpSocket* s = m_server.nextPendingConnection())
                QObject::connect(s, &QTcpSocket::readyRead, this, [this, s]() { onData(s); });
        });
    }
    bool listen() { return m_server.listen(QHostAddress::Any, 8001); }
    quint16 port() const { return m_server.serverPort(); }

    Mode mode = Mode::Respond;
    int status = 200;
    QByteArray body;
    int delayMs = 0;

    QStringList requestLines;   // e.g. "POST /generate-pdf HTTP/1.1"
    QList<QByteArray> requestBodies;

private:
    void onData(QTcpSocket* s)
    {
        QByteArray& buf = m_buffers[s];
        buf += s->readAll();
        const int headerEnd = buf.indexOf("\r\n\r\n");
        if (headerEnd < 0)
            return;
        const QByteArray headers = buf.left(headerEnd);
        int length = 0;
        for (const QByteArray& line : headers.split('\n'))
            if (line.toLower().startsWith("content-length:"))
                length = line.mid(15).trimmed().toInt();
        if (buf.size() < headerEnd + 4 + length)
            return;

        requestLines << QString::fromLatin1(headers.left(headers.indexOf("\r\n")));
        requestBodies << buf.mid(headerEnd + 4, length);
        m_buffers.remove(s);

        QTimer::singleShot(delayMs, s, [this, s]() {
            if (mode == Mode::Drop) {
                s->abort();
                s->deleteLater();
                return;
            }
            const QByteArray reason = status == 200 ? "OK" : "INTERNAL SERVER ERROR";
            s->write("HTTP/1.1 " + QByteArray::number(status) + " " + reason + "\r\n"
                     "Content-Type: application/json\r\n"
                     "Content-Length: " + QByteArray::number(body.size()) + "\r\n"
                     "Connection: close\r\n\r\n" + body);
            s->disconnectFromHost();
            QObject::connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
        });
    }

    QTcpServer m_server;
    QHash<QTcpSocket*, QByteArray> m_buffers;
};

} // namespace

class TstFeatures : public QObject
{
    Q_OBJECT
private:
    QTemporaryDir m_dbDir;
    QSqlDatabase m_db;

private slots:
    void initTestCase()
    {
        QVERIFY(m_dbDir.isValid());
        m_db = QSqlDatabase::addDatabase("QSQLITE");
        m_db.setDatabaseName(m_dbDir.filePath("empty.db"));
        QVERIFY(m_db.open());
        QVERIFY2(ts::runMigrations(ts::initSqlPath()), "init.sql failed on an empty database");
        qInfo("platform=%s", qPrintable(QGuiApplication::platformName()));
    }

    // ======================================================================= UsbUtils
    void usb_isFat32_falseForLocalFolder()
    {
        QTemporaryDir dir;
        QVERIFY(!UsbUtils::isFat32(dir.path()));
    }

    void usb_findUsbMount_onlyRemovableMounts()
    {
        const QString mount = UsbUtils::findUsbMount();
        if (mount.isEmpty())
            QSKIP("no USB stick mounted");
        QVERIFY2(mount.startsWith("/media/") || mount.startsWith("/run/media/"), qPrintable(mount));
        QVERIFY(QStorageInfo(mount).isValid());
        qInfo("stick: %s (%s)", qPrintable(mount), QStorageInfo(mount).fileSystemType().constData());
    }

    // ================================================================ TransferManager
    void transfer_copiesFilesIntact()
    {
        QTemporaryDir src, dst;
        const QList<QPair<QString, qint64>> files = {
            {"empty.bin", 0}, {"small.bin", 1000}, {"multi_chunk.bin", (8 << 20) * 3 + 12345}};
        QStringList sources;
        for (int i = 0; i < files.size(); ++i) {
            const QString p = src.filePath(files[i].first);
            QVERIFY(writeFile(p, patternBytes(files[i].second, i)));
            sources << p;
        }
        const QString destDir = dst.filePath("SurgeryDownloads");   // created by the manager

        TransferManager m;
        QSignalSpy drained(&m, &TransferManager::queueDrained);
        QCOMPARE(m.enqueue(sources, destDir), 3);
        QVERIFY(m.activeCount() > 0);
        QTRY_COMPARE_WITH_TIMEOUT(drained.count(), 1, 30000);
        QCOMPARE(drained.at(0).at(0).toInt(), 3);   // succeeded
        QCOMPARE(drained.at(0).at(1).toInt(), 0);   // failed
        QCOMPARE(m.activeCount(), 0);
        QVERIFY(!m.hasUnseenFailures());

        for (int i = 0; i < files.size(); ++i) {
            QCOMPARE(readAll(QDir(destDir).filePath(files[i].first)), patternBytes(files[i].second, i));
            const TransferManager::Job* j = jobFor(m, files[i].first);
            QVERIFY(j);
            QCOMPARE(int(j->state), int(State::Done));
            QCOMPARE(j->bytesDone, files[i].second);
        }
        QVERIFY2(leftovers(destDir).isEmpty(), qPrintable(leftovers(destDir).join(", ")));
    }

    void transfer_skipsDuplicateWhileQueued()
    {
        QTemporaryDir src, dst;
        const QString a = src.filePath("a.bin"), b = src.filePath("b.bin");
        QVERIFY(makeSparse(a, 64 << 20));
        QVERIFY(writeFile(b, "b"));

        TransferManager m;
        QSignalSpy drained(&m, &TransferManager::queueDrained);
        QCOMPARE(m.enqueue({a, b}, dst.path()), 2);
        QCOMPARE(m.enqueue({b}, dst.path()), 0);       // already queued for the same folder
        QCOMPARE(m.enqueue({a, b}, dst.path()), 0);
        QTRY_COMPARE_WITH_TIMEOUT(drained.count(), 1, 30000);
        QCOMPARE(drained.at(0).at(0).toInt(), 2);
        QCOMPARE(m.enqueue({b}, dst.path()), 1);       // finished jobs don't block a new copy
        QTRY_COMPARE_WITH_TIMEOUT(drained.count(), 2, 30000);
    }

    void transfer_cancelQueuedJob()
    {
        QTemporaryDir src, dst;
        const QString big = src.filePath("big.bin"), next = src.filePath("next.bin");
        QVERIFY(makeSparse(big, 256 << 20));
        QVERIFY(writeFile(next, "next"));

        TransferManager m;
        QSignalSpy drained(&m, &TransferManager::queueDrained);
        QCOMPARE(m.enqueue({big, next}, dst.path()), 2);
        const TransferManager::Job* queued = jobFor(m, "next.bin");
        QVERIFY(queued);
        QCOMPARE(int(queued->state), int(State::Queued));
        m.cancel(queued->id);
        QCOMPARE(int(jobFor(m, "next.bin")->state), int(State::Cancelled));

        QTRY_COMPARE_WITH_TIMEOUT(drained.count(), 1, 60000);
        QCOMPARE(drained.at(0).at(0).toInt(), 1);   // big.bin copied
        QCOMPARE(drained.at(0).at(2).toInt(), 1);   // next.bin cancelled
        QVERIFY(!QFile::exists(dst.filePath("next.bin")));
    }

    // Cancelling mid-copy keeps the previous copy on the stick and leaves no temp file
    void transfer_cancelDuringCopyKeepsOldFile()
    {
        QTemporaryDir src, dst;
        const QString big = src.filePath("rec.mp4");
        QVERIFY(makeSparse(big, 512 << 20));
        QVERIFY(writeFile(dst.filePath("rec.mp4"), "OLD COPY"));

        TransferManager m;
        QSignalSpy drained(&m, &TransferManager::queueDrained);
        QCOMPARE(m.enqueue({big}, dst.path()), 1);
        const TransferManager::Job* j = jobFor(m, "rec.mp4");
        QCOMPARE(int(j->state), int(State::Copying));
        m.cancel(j->id);
        QTRY_COMPARE_WITH_TIMEOUT(drained.count(), 1, 60000);
        QCOMPARE(int(jobFor(m, "rec.mp4")->state), int(State::Cancelled));
        QCOMPARE(drained.at(0).at(2).toInt(), 1);
        QCOMPARE(readAll(dst.filePath("rec.mp4")), QByteArray("OLD COPY"));
        QVERIFY2(leftovers(dst.path()).isEmpty(), qPrintable(leftovers(dst.path()).join(", ")));
    }

    void transfer_cancelAll()
    {
        QTemporaryDir src, dst;
        QStringList sources;
        for (int i = 0; i < 3; ++i) {
            sources << src.filePath(QString("f%1.bin").arg(i));
            QVERIFY(makeSparse(sources.last(), 256 << 20));
        }
        TransferManager m;
        QSignalSpy drained(&m, &TransferManager::queueDrained);
        QCOMPARE(m.enqueue(sources, dst.path()), 3);
        m.cancelAll();
        QTRY_COMPARE_WITH_TIMEOUT(drained.count(), 1, 60000);
        QCOMPARE(drained.at(0).at(2).toInt(), 3);
        QCOMPARE(m.activeCount(), 0);
        QVERIFY(QDir(dst.path()).entryList(QDir::Files).isEmpty());
    }

    void transfer_missingSourceFails()
    {
        QTemporaryDir dst;
        TransferManager m;
        QSignalSpy drained(&m, &TransferManager::queueDrained);
        QSignalSpy failures(&m, &TransferManager::failuresChanged);
        m.enqueue({"/nonexistent/recording.mp4"}, dst.path());
        QTRY_COMPARE_WITH_TIMEOUT(drained.count(), 1, 5000);
        QCOMPARE(drained.at(0).at(1).toInt(), 1);
        const TransferManager::Job* j = jobFor(m, "recording.mp4");
        QCOMPARE(int(j->state), int(State::Failed));
        QVERIFY2(j->error.contains("no longer exists"), qPrintable(j->error));
        QVERIFY(m.hasUnseenFailures());
        QCOMPARE(failures.count(), 1);
        m.markFailuresSeen();
        QVERIFY(!m.hasUnseenFailures());
    }

    void transfer_unwritableDestinationFails()
    {
        QTemporaryDir src;
        QVERIFY(writeFile(src.filePath("a.bin"), "a"));
        TransferManager m;
        QSignalSpy drained(&m, &TransferManager::queueDrained);
        m.enqueue({src.filePath("a.bin")}, "/proc/not_a_usb_stick");
        QTRY_COMPARE_WITH_TIMEOUT(drained.count(), 1, 5000);
        const TransferManager::Job* j = jobFor(m, "a.bin");
        QCOMPARE(int(j->state), int(State::Failed));
        QVERIFY2(j->error.contains("not found or not writable"), qPrintable(j->error));
    }

    void transfer_notEnoughSpaceFailsBeforeCopying()
    {
        QTemporaryDir src, dst;
        const qint64 free = QStorageInfo(dst.path()).bytesAvailable();
        QVERIFY(free > 0);
        const QString huge = src.filePath("huge.bin");
        if (!makeSparse(huge, free + (1LL << 30)))
            QSKIP("filesystem can't create a sparse file that large");
        TransferManager m;
        QSignalSpy drained(&m, &TransferManager::queueDrained);
        m.enqueue({huge}, dst.path());
        QTRY_COMPARE_WITH_TIMEOUT(drained.count(), 1, 5000);
        const TransferManager::Job* j = jobFor(m, "huge.bin");
        QCOMPARE(int(j->state), int(State::Failed));
        QVERIFY2(j->error.contains("Not enough free space"), qPrintable(j->error));
        QVERIFY(QDir(dst.path()).entryList(QDir::Files).isEmpty());
    }

    // Checks only: the >4 GB file is refused before anything is written to the stick
    void transfer_fat32RefusesFileOver4GB()
    {
        const QString stick = UsbUtils::findUsbMount();
        if (stick.isEmpty() || !UsbUtils::isFat32(stick))
            QSKIP("no FAT32 USB stick mounted");
        QTemporaryDir src;
        const QString name = "tst_features_over_4gb_never_written.bin";
        const QString big = src.filePath(name);
        if (!makeSparse(big, UsbUtils::kFat32MaxFileSize + 1))
            QSKIP("filesystem can't create the sparse test file");
        QVERIFY(!QFile::exists(QDir(stick).filePath(name)));

        TransferManager m;
        QSignalSpy drained(&m, &TransferManager::queueDrained);
        m.enqueue({big}, stick);
        QTRY_COMPARE_WITH_TIMEOUT(drained.count(), 1, 5000);
        const TransferManager::Job* j = jobFor(m, name);
        QCOMPARE(int(j->state), int(State::Failed));
        QVERIFY2(j->error.contains("FAT32"), qPrintable(j->error));
        QVERIFY(!QFile::exists(QDir(stick).filePath(name)));
    }

    void transfer_clearFinished()
    {
        QTemporaryDir src, dst;
        QVERIFY(writeFile(src.filePath("a.bin"), "a"));
        TransferManager m;
        QSignalSpy drained(&m, &TransferManager::queueDrained);
        QSignalSpy removed(&m, &TransferManager::jobsRemoved);
        m.enqueue({src.filePath("a.bin"), "/nonexistent/b.bin"}, dst.path());
        QTRY_COMPARE_WITH_TIMEOUT(drained.count(), 1, 5000);
        QCOMPARE(m.jobs().size(), 2);
        m.clearFinished();
        QCOMPARE(m.jobs().size(), 0);
        QCOMPARE(removed.count(), 1);
        QVERIFY(!m.hasUnseenFailures());
    }

    // Closing the app mid-copy: no crash, no half-written file left on the stick
    void transfer_destroyDuringCopy()
    {
        QTemporaryDir src, dst;
        QVERIFY(makeSparse(src.filePath("big.bin"), 512 << 20));
        auto* m = new TransferManager;
        m->enqueue({src.filePath("big.bin")}, dst.path());
        QCOMPARE(int(jobFor(*m, "big.bin")->state), int(State::Copying));
        delete m;
        QVERIFY(!QFile::exists(dst.filePath("big.bin")));
        QVERIFY2(leftovers(dst.path()).isEmpty(), qPrintable(leftovers(dst.path()).join(", ")));
    }

    // Speed figure is reported while copying (the drawer shows it). Measured on the USB stick if
    // one is mounted (a local copy finishes too fast); the test folder is removed afterwards.
    void transfer_reportsSpeedWhileCopying()
    {
        QTemporaryDir src;
        const QByteArray data = patternBytes(48 << 20, 3);
        QVERIFY(writeFile(src.filePath("speed.bin"), data));

        const QString stick = UsbUtils::findUsbMount();
        QTemporaryDir localDst;
        const QString destDir = stick.isEmpty() ? localDst.path()
                                                : QDir(stick).filePath("tst_features_speed_test");
        TransferManager m;
        QSignalSpy drained(&m, &TransferManager::queueDrained);
        double maxSpeed = 0;
        connect(&m, &TransferManager::jobChanged, this, [&](int id) {
            if (const TransferManager::Job* j = m.job(id))
                maxSpeed = qMax(maxSpeed, j->bytesPerSecond);
        });
        QElapsedTimer clock;
        clock.start();
        m.enqueue({src.filePath("speed.bin")}, destDir);
        QTRY_COMPARE_WITH_TIMEOUT(drained.count(), 1, 300000);
        const double seconds = clock.elapsed() / 1000.0;
        const bool intact = readAll(QDir(destDir).filePath("speed.bin")) == data;
        if (!stick.isEmpty())
            QDir(destDir).removeRecursively();

        QCOMPARE(drained.at(0).at(0).toInt(), 1);
        QVERIFY(intact);
        qInfo("48 MB to %s in %.1f s (%.1f MB/s overall); drawer showed up to %.1f MB/s",
              qPrintable(stick.isEmpty() ? QString("a local folder") : stick), seconds,
              48 / seconds, maxSpeed / (1 << 20));
        if (seconds < 1.5)
            QSKIP("copy finished too quickly to show a speed");
        QVERIFY(maxSpeed > 0);
        // The drawer shows what reaches the stick, not how fast data enters memory
        const double overall = data.size() / seconds;
        QVERIFY2(maxSpeed < overall * 1.35, qPrintable(QString("drawer showed %1 MB/s for a real %2 MB/s")
                     .arg(maxSpeed / (1 << 20), 0, 'f', 1).arg(overall / (1 << 20), 0, 'f', 1)));
    }

    // ================================================================ Several USB devices
    void usb_listDevicesAndVolumeRoot()
    {
        QTemporaryDir a, b;
        QCOMPARE(UsbUtils::volumeRoot(a.path()), UsbUtils::volumeRoot(b.path()));           // same disk
        QCOMPARE(UsbUtils::volumeRoot(a.filePath("not/created/yet")), UsbUtils::volumeRoot(a.path()));
        QVERIFY(UsbUtils::volumeRoot("/dev/shm") != UsbUtils::volumeRoot(a.path()));        // another volume

        const QList<UsbUtils::Device> devices = UsbUtils::listDevices();
        if (devices.isEmpty())
            QSKIP("no USB device mounted");
        for (const UsbUtils::Device& d : devices) {
            qInfo("device: %s", qPrintable(UsbDeviceDialog::describe(d)));
            QVERIFY(d.mountPath.startsWith("/media/") || d.mountPath.startsWith("/run/media/"));
            QVERIFY(!d.name.isEmpty());
            QVERIFY(d.bytesTotal > 0 && d.bytesFree >= 0 && d.bytesFree <= d.bytesTotal);
            QCOMPARE(UsbUtils::volumeRoot(d.mountPath + "/SurgeryDownloads"), d.mountPath);
            QCOMPARE(d.fat32, UsbUtils::isFat32(d.mountPath));
        }
        QCOMPARE(UsbUtils::findUsbMount(), devices.first().mountPath);
    }

    // Two devices copy at the same time; one device copies its files one after the other
    void transfer_devicesCopyInParallel()
    {
        QTemporaryDir src, diskDst;
        QTemporaryDir shmDst("/dev/shm/tst_features_XXXXXX");   // a second volume: its own queue
        if (!shmDst.isValid() || UsbUtils::volumeRoot(shmDst.path()) == UsbUtils::volumeRoot(diskDst.path()))
            QSKIP("no second volume available for the test");
        const QString a = src.filePath("a.mp4"), b = src.filePath("b.mp4");
        QVERIFY(makeSparse(a, 96 << 20));
        QVERIFY(makeSparse(b, 96 << 20));

        TransferManager m;
        QSignalSpy drained(&m, &TransferManager::queueDrained);
        QSignalSpy deviceDone(&m, &TransferManager::deviceFinished);
        int maxCopying = 0;
        connect(&m, &TransferManager::jobChanged, this, [&]() { maxCopying = qMax(maxCopying, m.copyingCount()); });

        QCOMPARE(m.enqueue({a, b}, diskDst.path(), "DISK"), 2);
        QCOMPARE(m.copyingCount(), 1);                       // second file waits for the first
        QCOMPARE(m.enqueue({a, b}, shmDst.path(), "MEMORY"), 2);
        QCOMPARE(m.copyingCount(), 2);                       // the other device started at once
        QCOMPARE(m.activeCount(), 4);

        // Per device never more than one file at a time
        QHash<QString, int> copyingPerDevice;
        for (const TransferManager::Job& j : m.jobs())
            if (j.state == State::Copying) ++copyingPerDevice[j.deviceName];
        QCOMPARE(copyingPerDevice.value("DISK"), 1);
        QCOMPARE(copyingPerDevice.value("MEMORY"), 1);

        QTRY_COMPARE_WITH_TIMEOUT(drained.count(), 1, 120000);
        QCOMPARE(drained.at(0).at(0).toInt(), 4);
        QCOMPARE(maxCopying, 2);
        QCOMPARE(deviceDone.count(), 2);                     // one "you can remove it" per device
        QStringList names;
        for (const auto& args : deviceDone) {
            names << args.at(0).toString();
            QCOMPARE(args.at(1).toInt(), 2);
        }
        names.sort();
        QCOMPARE(names, QStringList({"DISK", "MEMORY"}));
        for (const QString& dir : {diskDst.path(), shmDst.path()}) {
            QCOMPARE(QFileInfo(dir + "/a.mp4").size(), qint64(96 << 20));
            QCOMPARE(QFileInfo(dir + "/b.mp4").size(), qint64(96 << 20));
        }
    }

    // A failure or a cancel on one device leaves the other device's copies alone
    void transfer_devicesAreIndependent()
    {
        QTemporaryDir src, diskDst;
        QTemporaryDir shmDst("/dev/shm/tst_features_XXXXXX");
        if (!shmDst.isValid() || UsbUtils::volumeRoot(shmDst.path()) == UsbUtils::volumeRoot(diskDst.path()))
            QSKIP("no second volume available for the test");
        const QString a = src.filePath("a.mp4"), b = src.filePath("b.mp4");
        QVERIFY(makeSparse(a, 256 << 20));
        QVERIFY(makeSparse(b, 8 << 20));

        TransferManager m;
        QSignalSpy drained(&m, &TransferManager::queueDrained);
        QSignalSpy deviceDone(&m, &TransferManager::deviceFinished);
        m.enqueue({a, b}, diskDst.path(), "DISK");
        m.enqueue({a, b}, shmDst.path(), "MEMORY");
        for (const TransferManager::Job& j : m.jobs())
            if (j.deviceName == "DISK") m.cancel(j.id);      // cancel everything going to DISK

        QTRY_COMPARE_WITH_TIMEOUT(drained.count(), 1, 120000);
        QCOMPARE(drained.at(0).at(0).toInt(), 2);            // MEMORY's two files
        QCOMPARE(drained.at(0).at(2).toInt(), 2);            // DISK's two cancelled
        QVERIFY(QDir(diskDst.path()).entryList(QDir::Files).isEmpty());
        QCOMPARE(QFileInfo(shmDst.path() + "/a.mp4").size(), qint64(256 << 20));
        QCOMPARE(QFileInfo(shmDst.path() + "/b.mp4").size(), qint64(8 << 20));
        QCOMPARE(deviceDone.count(), 2);
    }

    // Real hardware: the same file to two USB devices at once (skipped with fewer than two).
    // Test folders on the devices are removed afterwards.
    void transfer_twoRealUsbDevicesAtOnce()
    {
        const QList<UsbUtils::Device> devices = UsbUtils::listDevices();
        if (devices.size() < 2)
            QSKIP("needs two USB devices");
        QTemporaryDir src;
        const QByteArray data = patternBytes(96 << 20, 9);
        const QString file = src.filePath("parallel_test.bin");
        QVERIFY(writeFile(file, data));
        const QString folder = "/tst_features_parallel_test";
        const UsbUtils::Device d0 = devices[0], d1 = devices[1];
        auto cleanup = [&]() { for (const auto& d : {d0, d1}) QDir(d.mountPath + folder).removeRecursively(); };

        auto copyTo = [&](const QList<UsbUtils::Device>& targets, QHash<QString, double>* seconds, int* maxCopying) {
            cleanup();
            ::sync();
            TransferManager m;
            QSignalSpy drained(&m, &TransferManager::queueDrained);
            QElapsedTimer clock;
            connect(&m, &TransferManager::deviceFinished, this, [&](const QString& name) {
                (*seconds)[name] = clock.elapsed() / 1000.0; });
            connect(&m, &TransferManager::jobChanged, this, [&]() { *maxCopying = qMax(*maxCopying, m.copyingCount()); });
            clock.start();
            for (const UsbUtils::Device& d : targets)
                m.enqueue({file}, d.mountPath + folder, d.name);
            *maxCopying = qMax(*maxCopying, m.copyingCount());
            if (!QTest::qWaitFor([&]() { return drained.count() == 1; }, 300000)) return false;
            return drained.at(0).at(0).toInt() == targets.size();
        };

        QHash<QString, double> alone, together;
        int copying = 0, copyingTogether = 0;
        QVERIFY(copyTo({d0}, &alone, &copying));
        QVERIFY(copyTo({d1}, &alone, &copying));
        QVERIFY(copyTo({d0, d1}, &together, &copyingTogether));
        const bool intact = readAll(d0.mountPath + folder + "/parallel_test.bin") == data
                         && readAll(d1.mountPath + folder + "/parallel_test.bin") == data;
        cleanup();

        const double mb = data.size() / 1e6;
        for (const UsbUtils::Device& d : {d0, d1})
            qInfo("%s: alone %.1f s (%.1f MB/s), together %.1f s (%.1f MB/s)", qPrintable(d.name),
                  alone[d.name], mb / alone[d.name], together[d.name], mb / together[d.name]);
        const double oneAfterTheOther = alone[d0.name] + alone[d1.name];
        const double bothAtOnce = qMax(together[d0.name], together[d1.name]);
        qInfo("both devices: one after the other %.1f s, at the same time %.1f s", oneAfterTheOther, bothAtOnce);
        QVERIFY(intact);
        QCOMPARE(copyingTogether, 2);                 // both were being written at once
        QVERIFY2(bothAtOnce < oneAfterTheOther, "copying to both at once should beat one after the other");
    }

    void usbChooser_oneDeviceIsNotAsked()
    {
        UsbUtils::Device only;
        only.mountPath = "/media/x/ONLY"; only.name = "ONLY"; only.fileSystem = "exFAT";
        ts::ModalCloser closer;   // would record a dialog if one appeared
        const QList<UsbUtils::Device> chosen = UsbDeviceDialog::choose(nullptr, {only});
        QCOMPARE(chosen.size(), 1);
        QVERIFY(closer.closed.isEmpty());
        QVERIFY(UsbDeviceDialog::choose(nullptr, {}).isEmpty());
    }

    void usbChooser_pickSeveral()
    {
        UsbUtils::Device a, b, c;
        a.mountPath = "/media/x/KINGSTON"; a.name = "KINGSTON"; a.fileSystem = "exFAT"; a.bytesFree = 28100000000LL; a.bytesTotal = 29000000000LL;
        b.mountPath = "/media/x/SANDISK"; b.name = "SANDISK"; b.fileSystem = "FAT32"; b.fat32 = true; b.bytesFree = 7000000000LL; b.bytesTotal = 8000000000LL;
        c.mountPath = "/media/x/SSD"; c.name = "SSD"; c.fileSystem = "NTFS"; c.bytesFree = 400000000000LL; c.bytesTotal = 500000000000LL;
        QCOMPARE(UsbDeviceDialog::describe(a), QString::fromUtf8("KINGSTON — 28.1 GB free of 29.0 GB · exFAT"));

        // Drives the dialog: `tick` = which boxes to leave ticked; records what it showed
        QStringList shown;
        bool downloadEnabledWithNoneTicked = true;
        auto drive = [&](const QList<bool>& tick, bool accept) {
            auto* timer = new QTimer(this);
            timer->setSingleShot(true);
            connect(timer, &QTimer::timeout, this, [&, tick, accept, timer]() {
                auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
                QVERIFY(dialog);
                const QList<QCheckBox*> boxes = dialog->findChildren<QCheckBox*>();
                shown.clear();
                for (QCheckBox* box : boxes) shown << box->text();
                for (QLabel* l : dialog->findChildren<QLabel*>()) shown << l->text();
                const QString shotDir = qEnvironmentVariable("SHOT_DIR");
                if (!shotDir.isEmpty() && accept)
                    dialog->grab().save(shotDir + "/usb_chooser.png");
                for (QCheckBox* box : boxes) box->setChecked(false);
                for (QPushButton* btn : dialog->findChildren<QPushButton*>())
                    if (btn->text() == "Download") downloadEnabledWithNoneTicked = btn->isEnabled();
                for (int i = 0; i < boxes.size(); ++i) boxes[i]->setChecked(tick.value(i));
                accept ? dialog->accept() : dialog->reject();
                timer->deleteLater();
            });
            timer->start(50);
        };

        drive({true, false, true}, true);
        QList<UsbUtils::Device> chosen = UsbDeviceDialog::choose(nullptr, {a, b, c}, 5LL << 30);
        QCOMPARE(chosen.size(), 2);
        QCOMPARE(chosen[0].name, QString("KINGSTON"));
        QCOMPARE(chosen[1].name, QString("SSD"));
        QVERIFY(!downloadEnabledWithNoneTicked);                       // nothing ticked: can't download
        QVERIFY2(shown.join("|").contains("FAT32: files over 4 GB"), qPrintable(shown.join("|")));   // 5 GB file

        drive({true, true, true}, false);                              // Cancel
        QVERIFY(UsbDeviceDialog::choose(nullptr, {a, b, c}, 1000).isEmpty());
        QVERIFY2(!shown.join("|").contains("files over 4 GB"), "no FAT32 note for small files");
    }

    // Two messages at once (two devices finishing together) don't cover each other
    void toast_messagesStack()
    {
        QWidget window;
        window.resize(1280, 720);
        window.show();
        Toast::show(&window, "Copy to KINGSTON finished: 2 file(s) copied. You can remove it.", 2000);
        Toast::show(&window, "Copy to SSD finished: 2 file(s) copied. You can remove it.", 2000);
        const QList<QLabel*> toasts = window.findChildren<QLabel*>("AppToast");
        QCOMPARE(toasts.size(), 2);
        QVERIFY(!toasts[0]->geometry().intersects(toasts[1]->geometry()));
    }

    // ================================================================ Generate Report
    void report_success_opensPdfAndShowsProgress()
    {
        FakeReportService svc;
        if (!svc.listen())
            QSKIP("port 8001 is in use: stop the report service to run this test");
        svc.body = R"({"message":"PDF generated successfully.","pdf_path":"/tmp/test_report.pdf"})";
        svc.delayMs = 300;

        SurgeryRecordingPage page("TEST_NO_SUCH_PATIENT", 424242);
        QSignalSpy opened(&page, &SurgeryRecordingPage::openPdfReport);
        QPushButton* btn = findButton(&page, "Generate Report");
        QVERIFY(btn);
        ts::ModalCloser closer;   // an unexpected error box must fail the test, not hang it
        btn->click();

        QVERIFY(!btn->isEnabled());
        QCOMPARE(btn->text(), QString::fromUtf8("Generating report…"));
        QTRY_VERIFY_WITH_TIMEOUT(opened.count() == 1 || !closer.closed.isEmpty(), 10000);
        QVERIFY2(closer.closed.isEmpty(), qPrintable(closer.closed.join(" | ")));
        QCOMPARE(opened.at(0).at(0).toString(), QString("/tmp/test_report.pdf"));
        QVERIFY(btn->isEnabled());
        QCOMPARE(btn->text(), QString("Generate Report"));

        QCOMPARE(svc.requestLines.size(), 1);
        QVERIFY2(svc.requestLines[0].startsWith("POST /generate-pdf "), qPrintable(svc.requestLines[0]));
        const QJsonObject sent = QJsonDocument::fromJson(svc.requestBodies[0]).object();
        QCOMPARE(sent.value("patient_id").toString(), QString("TEST_NO_SUCH_PATIENT"));
        QCOMPARE(sent.value("surgery_id").toInt(), 424242);
    }

    void report_serverError_showsTheReason()
    {
        FakeReportService svc;
        if (!svc.listen())
            QSKIP("port 8001 is in use: stop the report service to run this test");
        svc.status = 500;
        svc.body = R"({"error":"Patient TEST_NO_SUCH_PATIENT not found"})";

        SurgeryRecordingPage page("TEST_NO_SUCH_PATIENT", 424242);
        QSignalSpy opened(&page, &SurgeryRecordingPage::openPdfReport);
        QPushButton* btn = findButton(&page, "Generate Report");
        ts::ModalCloser closer;
        btn->click();
        QTRY_VERIFY_WITH_TIMEOUT(!closer.closed.isEmpty(), 10000);
        const QString shown = closer.closed.join(" | ");
        QVERIFY2(shown.contains("Failed to generate the PDF report."), qPrintable(shown));
        QVERIFY2(shown.contains("Patient TEST_NO_SUCH_PATIENT not found"), qPrintable(shown));
        QCOMPARE(opened.count(), 0);
        QVERIFY(btn->isEnabled());
        QCOMPARE(btn->text(), QString("Generate Report"));
    }

    void report_noAnswer_stillShowsAMessage()
    {
        FakeReportService svc;
        if (!svc.listen())
            QSKIP("port 8001 is in use: stop the report service to run this test");
        svc.mode = FakeReportService::Mode::Drop;

        SurgeryRecordingPage page("TEST_NO_SUCH_PATIENT", 424242);
        QPushButton* btn = findButton(&page, "Generate Report");
        ts::ModalCloser closer;
        btn->click();
        QTRY_VERIFY_WITH_TIMEOUT(!closer.closed.isEmpty(), 10000);
        const QString shown = closer.closed.join(" | ");
        QVERIFY2(shown.contains("Failed to generate the PDF report.\n\n"), qPrintable(shown));
        QVERIFY2(!shown.endsWith("\n\n"), "the reason must not be empty");
        QVERIFY(btn->isEnabled());
    }

    // ================================================================ Media info on the cards
    void mediaInfo_formatSize()
    {
        QCOMPARE(MediaInfo::formatSize(0), QString("0 B"));
        QCOMPARE(MediaInfo::formatSize(999), QString("999 B"));
        QCOMPARE(MediaInfo::formatSize(566000), QString("566 KB"));
        QCOMPARE(MediaInfo::formatSize(5'700'000), QString("5.7 MB"));
        QCOMPARE(MediaInfo::formatSize(121'000'000), QString("121 MB"));
        QCOMPARE(MediaInfo::formatSize(1'210'000'000), QString("1.21 GB"));
        QCOMPARE(MediaInfo::formatSize(33'830'000'000LL), QString("33.8 GB"));
    }

    void mediaInfo_formatDuration()
    {
        QCOMPARE(MediaInfo::formatDuration(0), QString("0:00"));
        QCOMPARE(MediaInfo::formatDuration(45'400), QString("0:45"));
        QCOMPARE(MediaInfo::formatDuration(754'000), QString("12:34"));
        QCOMPARE(MediaInfo::formatDuration(3'723'000), QString("1:02:03"));
        QCOMPARE(MediaInfo::formatDuration(-1), QString());
    }

    void mediaInfo_describeImageAndMissingFile()
    {
        QTemporaryDir dir;
        const QString jpg = dir.filePath("snapshot.jpg");
        QImage img(1920, 1080, QImage::Format_RGB32);
        img.fill(Qt::darkGray);
        QVERIFY(img.save(jpg, "JPG", 95));
        const QString text = MediaInfo::describe(jpg, false);
        QVERIFY2(text.startsWith(QString::fromUtf8("1920 × 1080 · ")), qPrintable(text));
        QVERIFY2(text.endsWith(MediaInfo::formatSize(QFileInfo(jpg).size())), qPrintable(text));
        QCOMPARE(MediaInfo::describe(dir.filePath("gone.mp4"), true), QString("File not found"));
        // Not a video: the size is still shown
        QVERIFY(writeFile(dir.filePath("broken.mp4"), QByteArray(5000, 'x')));
        QCOMPARE(MediaInfo::describe(dir.filePath("broken.mp4"), true), QString("5 KB"));
    }

    // A fragmented MP4 like the recorder writes (no total length in its header)
    void mediaInfo_videoDuration()
    {
        QTemporaryDir dir;
        const QString mp4 = dir.filePath("recording.mp4");
        QProcess ffmpeg;
        ffmpeg.start("ffmpeg", {"-v", "error", "-f", "lavfi", "-i", "testsrc=duration=7:size=320x240:rate=30",
                                "-c:v", "mpeg4", "-movflags", "frag_keyframe+empty_moov+default_base_moof", mp4});
        if (!ffmpeg.waitForStarted(5000))
            QSKIP("ffmpeg is not installed");
        QVERIFY(ffmpeg.waitForFinished(60000));
        QVERIFY2(ffmpeg.exitCode() == 0, ffmpeg.readAllStandardError().constData());

        const qint64 ms = MediaInfo::videoDurationMs(mp4);
        QVERIFY2(qAbs(ms - 7000) <= 100, qPrintable(QString::number(ms)));
        const QString text = MediaInfo::describe(mp4, true);
        QVERIFY2(text.startsWith(QString::fromUtf8("0:07 · ")), qPrintable(text));
    }

    // The gallery card shows the line (filled in from the worker thread)
    void mediaInfo_shownOnGalleryCard()
    {
        QTemporaryDir dir;
        const QString jpg = dir.filePath("snapshot_1.jpg");
        QImage img(1920, 1080, QImage::Format_RGB32);
        img.fill(Qt::darkGray);
        QVERIFY(img.save(jpg, "JPG", 95));
        QSqlQuery q;
        q.prepare("INSERT INTO snapshots (patient_id, surgery_id, file_path, title) VALUES (?, ?, ?, ?)");
        q.addBindValue("TEST_INFO_PATIENT"); q.addBindValue(31337); q.addBindValue(jpg); q.addBindValue("t");
        QVERIFY(q.exec());

        SurgeryRecordingPage page("TEST_INFO_PATIENT", 31337);
        const QString expected = MediaInfo::describe(jpg, false);
        auto shown = [&]() {
            for (QLabel* l : page.findChildren<QLabel*>())
                if (l->text() == expected) return true;
            return false;
        };
        QTRY_VERIFY_WITH_TIMEOUT(shown(), 5000);
        const QString shotDir = qEnvironmentVariable("SHOT_DIR");
        if (!shotDir.isEmpty()) {
            page.resize(1600, 900);
            page.show();
            QTest::qWait(200);
            page.grab().save(shotDir + "/gallery_info.png");
        }
        QVERIFY(q.exec("DELETE FROM snapshots WHERE patient_id = 'TEST_INFO_PATIENT'"));
    }

    // ================================================================ Rename
    void rename_nameRules()
    {
        QTemporaryDir dir;
        const QString cur = dir.filePath("recording_20260928_120115.mp4");
        QVERIFY(writeFile(cur, "video"));
        QVERIFY(writeFile(dir.filePath("Knee Left.mp4"), "other"));
        QString error;

        QCOMPARE(MediaRename::newPathFor("Hip replacement", cur, &error), dir.filePath("Hip replacement.mp4"));
        QCOMPARE(MediaRename::newPathFor("  Hip replacement.MP4  ", cur, &error), dir.filePath("Hip replacement.mp4"));
        QCOMPARE(MediaRename::newPathFor("Dr. Rao - case 12", cur, &error), dir.filePath("Dr. Rao - case 12.mp4"));
        QCOMPARE(MediaRename::newPathFor("name. ", cur, &error), dir.filePath("name.mp4"));   // trailing dot/space dropped
        QCOMPARE(MediaRename::newPathFor("recording_20260928_120115", cur, &error), cur);      // unchanged

        for (const QString& bad : {QString(""), QString("   "), QString(".hidden"), QString("a/b"), QString("a\\b"),
                                   QString("what?"), QString("a:b"), QString("x*y"), QString("q\"uote"), QString("a<b"),
                                   QString("a|b"), QString("tab\there"), QString(101, 'x')}) {
            error.clear();
            QVERIFY2(MediaRename::newPathFor(bad, cur, &error).isEmpty(), qPrintable(bad));
            QVERIFY2(!error.isEmpty(), qPrintable(bad));
        }
        QVERIFY(!MediaRename::newPathFor(QString(100, 'x'), cur, &error).isEmpty());

        // Taken, also when only the case differs (one file on a USB stick)
        error.clear();
        QVERIFY(MediaRename::newPathFor("knee left", cur, &error).isEmpty());
        QVERIFY2(error.contains("Knee Left.mp4"), qPrintable(error));
        // Changing only the case of its own name is fine
        QCOMPARE(MediaRename::newPathFor("Recording_20260928_120115", cur, &error),
                 dir.filePath("Recording_20260928_120115.mp4"));
    }

    void rename_fileAndDatabaseTogether()
    {
        QTemporaryDir dir;
        const QString oldPath = dir.filePath("recording_1.mp4"), newPath = dir.filePath("Hip.mp4");
        QVERIFY(writeFile(oldPath, "video-bytes"));
        QSqlQuery q;
        q.prepare("INSERT INTO recordings (patient_id, surgery_id, file_path) VALUES (?, ?, ?)");
        q.addBindValue("TEST_RENAME"); q.addBindValue(1); q.addBindValue(oldPath);
        QVERIFY(q.exec());
        const int id = q.lastInsertId().toInt();

        QString error;
        QVERIFY2(MediaRename::rename("recordings", id, oldPath, newPath, &error), qPrintable(error));
        QVERIFY(!QFile::exists(oldPath));
        QCOMPARE(readAll(newPath), QByteArray("video-bytes"));
        QVERIFY(q.exec(QString("SELECT file_path FROM recordings WHERE id = %1").arg(id)) && q.next());
        QCOMPARE(q.value(0).toString(), newPath);

        // Database row missing: the file gets its old name back
        const QString third = dir.filePath("Third.mp4");
        QVERIFY(!MediaRename::rename("recordings", 987654, newPath, third, &error));
        QVERIFY(QFile::exists(newPath));
        QVERIFY(!QFile::exists(third));
        QVERIFY2(error.contains("keeps its old name"), qPrintable(error));

        // Never over another file; never a table other than the two media tables
        QVERIFY(writeFile(third, "other"));
        QVERIFY(!MediaRename::rename("recordings", id, newPath, third, &error));
        QCOMPARE(readAll(third), QByteArray("other"));
        QVERIFY(!MediaRename::rename("patients", id, newPath, dir.filePath("x.mp4"), &error));
        QVERIFY(!MediaRename::rename("recordings", id, dir.filePath("gone.mp4"), dir.filePath("y.mp4"), &error));
        QVERIFY(q.exec("DELETE FROM recordings WHERE patient_id = 'TEST_RENAME'"));
    }

    // The button on a card: type a name, the file and the card change; guards block files in use
    void rename_fromGalleryCard()
    {
        QTemporaryDir dir;
        const QString oldPath = dir.filePath("recording_20260928_120115.mp4");
        QVERIFY(writeFile(oldPath, "video-bytes"));
        QSqlQuery q;
        q.prepare("INSERT INTO recordings (patient_id, surgery_id, file_path) VALUES (?, ?, ?)");
        q.addBindValue("TEST_RENAME_UI"); q.addBindValue(7); q.addBindValue(oldPath);
        QVERIFY(q.exec());

        SurgeryRecordingPage page("TEST_RENAME_UI", 7);
        TransferManager transfers;
        page.setTransferManager(&transfers);
        bool recordingNow = true;
        page.setFileBusyCheck([&](const QString& p) { return recordingNow && p == oldPath; });

        auto renameButton = [&]() -> QPushButton* {
            for (QPushButton* b : page.findChildren<QPushButton*>())
                if (b->toolTip() == "Rename this file") return b;
            return nullptr;
        };
        QVERIFY(renameButton());

        // 1. Being recorded: refused with a message, nothing changes
        {
            ts::ModalCloser closer;
            renameButton()->click();
            QVERIFY2(closer.closed.join("|").contains("still being recorded"), qPrintable(closer.closed.join("|")));
            QVERIFY(QFile::exists(oldPath));
        }
        recordingNow = false;

        // 2. Type a bad name, then a good one
        QStringList typed = {"bad/name", "Hip replacement"};
        QStringList messages;
        QTimer driver;
        driver.setInterval(20);
        connect(&driver, &QTimer::timeout, this, [&]() {
            QWidget* modal = QApplication::activeModalWidget();
            if (auto* input = qobject_cast<QInputDialog*>(modal)) {
                if (typed.isEmpty()) { input->reject(); return; }
                input->setTextValue(typed.takeFirst());
                input->accept();
            } else if (auto* box = qobject_cast<QMessageBox*>(modal)) {
                messages << box->text();
                box->accept();
            }
        });
        driver.start();
        renameButton()->click();
        driver.stop();

        const QString newPath = dir.filePath("Hip replacement.mp4");
        QVERIFY2(messages.join("|").contains("cannot contain"), qPrintable(messages.join("|")));
        QVERIFY(!QFile::exists(oldPath));
        QCOMPARE(readAll(newPath), QByteArray("video-bytes"));
        QVERIFY(q.exec("SELECT file_path FROM recordings WHERE patient_id = 'TEST_RENAME_UI'") && q.next());
        QCOMPARE(q.value(0).toString(), newPath);
        bool cardShowsNewName = false;
        for (QLabel* l : page.findChildren<QLabel*>())
            if (l->text() == "Hip replacement.mp4") cardShowsNewName = true;
        QVERIFY(cardShowsNewName);

        QVERIFY(q.exec("DELETE FROM recordings WHERE patient_id = 'TEST_RENAME_UI'"));
    }

    void rename_blockedWhileCopyingToUsb()
    {
        QTemporaryDir src, dst;
        const QString big = src.filePath("big.mp4");
        QVERIFY(makeSparse(big, 512 << 20));
        TransferManager m;
        QVERIFY(!m.isActiveSource(big));
        QSignalSpy drained(&m, &TransferManager::queueDrained);
        m.enqueue({big}, dst.path());
        QVERIFY(m.isActiveSource(big));
        QVERIFY(!m.isActiveSource(src.filePath("other.mp4")));
        m.cancelAll();
        QTRY_COMPARE_WITH_TIMEOUT(drained.count(), 1, 60000);
        QVERIFY(!m.isActiveSource(big));
    }

    // ================================================================ Headings
    void heading_archiveMatchesDashboard()
    {
        SurgeryRecordingPage archive(QString(), -1);
        archive.resize(1600, 900);
        archive.show();
        QTest::qWait(150);   // updateScaling() runs 50 ms after show

        QLabel* title = nullptr;
        for (QLabel* l : archive.findChildren<QLabel*>())
            if (l->text() == "Archive") title = l;
        QVERIFY(title);
        const QString expected = QString("font-size: %1px").arg(UIScale::pageTitleFontSize(&archive));
        QVERIFY2(title->styleSheet().contains(expected), qPrintable(title->styleSheet()));
        QVERIFY(title->styleSheet().contains("color: red"));

        int visibleLines = 0;
        for (QFrame* f : archive.findChildren<QFrame*>())
            if (f->frameShape() == QFrame::HLine && f->isVisible()) ++visibleLines;
        QCOMPARE(visibleLines, 2);

        const QString shotDir = qEnvironmentVariable("SHOT_DIR");
        if (!shotDir.isEmpty())
            archive.grab().save(shotDir + "/archive_page.png");
    }

    void heading_surgeryPageUnchanged()
    {
        SurgeryRecordingPage page("TEST_NO_SUCH_PATIENT", 424242);
        page.resize(1600, 900);
        page.show();
        QTest::qWait(150);
        int visibleLines = 0;
        for (QFrame* f : page.findChildren<QFrame*>())
            if (f->frameShape() == QFrame::HLine && f->isVisible()) ++visibleLines;
        QCOMPARE(visibleLines, 0);
        bool hasTitle = false;
        for (QLabel* l : page.findChildren<QLabel*>())
            if (l->text() == "Surgery Details") hasTitle = l->styleSheet().contains("#c40000");
        QVERIFY(hasTitle);
    }

    void heading_fontSizeWithinBounds()
    {
        const int px = UIScale::pageTitleFontSize();
        QVERIFY(px >= 20 && px <= 36);
        QCOMPARE(px, qBound(20, int(60 * UIScale::factor()), 36));
    }

    // ================================================================ Icons
    // Every SVG the code refers to is in resources.qrc and renders
    void icons_referencedSvgsExistAndRender()
    {
        const QStringList sources = {"ui/Recording/RecordingPage.cpp", "ui/dashboard/DashboardPage.cpp",
                                     "ui/home/HomePage.cpp"};
        QSet<QString> used;
        const QRegularExpression re(":/?assets/icons/[A-Za-z0-9_-]+\\.svg");
        for (const QString& rel : sources) {
            const QString text = QString::fromUtf8(readAll(ts::appDir() + "/" + rel));
            QVERIFY2(!text.isEmpty(), qPrintable(rel));
            for (auto it = re.globalMatch(text); it.hasNext();)
                used.insert(it.next().captured(0));
        }
        QVERIFY(used.size() >= 12);
        for (QString path : used) {
            if (!path.startsWith(":/")) path.replace(0, 1, ":/");
            QVERIFY2(QFile::exists(path), qPrintable(path + " is not in resources.qrc"));
            QVERIFY2(!QIcon(path).pixmap(32, 32).isNull(), qPrintable(path + " does not render"));
        }
        qInfo("%d SVG icons checked", used.size());
    }

    // No emoji left in what the Recording page shows (labels, buttons, messages)
    void icons_noEmojiInRecordingPageUi()
    {
        const QString text = QString::fromUtf8(readAll(ts::appDir() + "/ui/Recording/RecordingPage.cpp"));
        const QRegularExpression emoji("[\\x{2190}-\\x{2BFF}\\x{1F000}-\\x{1FAFF}]");
        QStringList offending;
        for (const QString& line : text.split('\n')) {
            const QString t = line.trimmed();
            if (t.startsWith("//") || t.contains("qDebug") || t.contains("qWarning") || t.contains("qInfo"))
                continue;
            if (emoji.match(t).hasMatch()) offending << t;
        }
        QVERIFY2(offending.isEmpty(), qPrintable(offending.join("\n")));
    }
};

QTEST_MAIN(TstFeatures)
#include "tst_features.moc"
