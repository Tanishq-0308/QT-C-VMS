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
#include "core/TransferManager.hpp"
#include "core/UIScale.hpp"
#include "core/UsbUtils.hpp"
#include "ui/SurgeryRecordPage/SurgeryRecordingPage.hpp"

namespace {

using State = TransferManager::State;

QByteArray patternBytes(qint64 size, int seed)
{
    QByteArray b(int(size), Qt::Uninitialized);
    for (int i = 0; i < b.size(); ++i)
        b[i] = char((i * 31 + seed * 7 + (i >> 11)) & 0xff);
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
