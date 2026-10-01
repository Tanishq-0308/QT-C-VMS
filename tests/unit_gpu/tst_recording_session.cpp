// tst_recording_session (GPU): the app's shared RecordingSession over the real VideoRecorder
// (NVENC). Synthetic 1080p60 frames are pushed the way the capture card pushes them.
//
// Checks the patient and Archive recordings made from RecordingPage and the Dashboard: states,
// the `recordings` rows, where the files go, one recording at a time, and a stop while starting.
// Uses an EMPTY database built from database/migrations/init.sql. Files are written under
// <test binary>/../recordings and removed at the end.

#include <QtTest>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QProcess>
#include <atomic>
#include <thread>
#include <vector>

#include "tests/unit/common/TestSupport.hpp"
#include "ui/Recording/RecordingSession.hpp"
#include "ui/Recording/VideoRecorder.hpp"

Q_DECLARE_METATYPE(RecordingSession::State)

namespace {

// Pushes grey 1080p60 UYVY frames into the recorder from its own thread, like the capture card
class FrameFeeder {
public:
    explicit FrameFeeder(VideoRecorder* rec) : m_rec(rec), m_frame(1920 * 2 * 1080, 0x80) {}
    ~FrameFeeder() { stop(); }
    void start()
    {
        m_quit = false;
        m_thread = std::thread([this]() {
            int64_t slot = 1000;
            auto next = std::chrono::steady_clock::now();
            while (!m_quit) {
                RawVideoFrame f;
                f.data = m_frame.data();
                f.width = 1920;
                f.height = 1080;
                f.rowBytes = 1920 * 2;
                f.format = RawPixelFormat::UYVY;
                f.timeScale = VideoRecorder::kTimeScale;
                f.frameDuration = VideoRecorder::kTimeScale / 60;
                f.streamTime = slot++ * f.frameDuration;
                m_rec->pushFrame(f);
                next += std::chrono::microseconds(16667);
                std::this_thread::sleep_until(next);
            }
        });
    }
    void stop()
    {
        m_quit = true;
        if (m_thread.joinable())
            m_thread.join();
    }

private:
    VideoRecorder* m_rec;
    std::vector<uint8_t> m_frame;
    std::atomic<bool> m_quit{true};
    std::thread m_thread;
};

// Duration of a video file according to ffprobe, -1 if it can't be read
double probeSeconds(const QString& path)
{
    QProcess p;
    p.start("ffprobe", {"-v", "error", "-show_entries", "format=duration", "-of", "csv=p=0", path});
    if (!p.waitForFinished(10000) || p.exitCode() != 0)
        return -1;
    bool ok = false;
    const double s = QString::fromLatin1(p.readAllStandardOutput()).trimmed().toDouble(&ok);
    return ok ? s : -1;
}

struct Row { QVariant patient, surgery; QString path; };
Row rowFor(int id)
{
    QSqlQuery q;
    q.prepare("SELECT patient_id, surgery_id, file_path FROM recordings WHERE id = ?");
    q.addBindValue(id);
    if (!q.exec() || !q.next())
        return {};
    return {q.value(0), q.value(1), q.value(2).toString()};
}

} // namespace

class TstRecordingSession : public QObject
{
    Q_OBJECT
    QTemporaryDir m_dbDir;
    QSqlDatabase m_db;
    QStringList m_files;

    // Records for `seconds`, stops, and checks the whole lifecycle; returns the DB row
    Row recordAndStop(RecordingSession& s, const QString& patient, int surgery, int seconds)
    {
        QSignalSpy states(&s, &RecordingSession::stateChanged);
        QSignalSpy started(&s, &RecordingSession::recordingStarted);
        QSignalSpy stopped(&s, &RecordingSession::recordingStopped);
        QSignalSpy errors(&s, &RecordingSession::errorOccurred);

        QString error;
        if (!s.start(patient, surgery, 0, &error)) {
            QTest::qFail(qPrintable("start failed: " + error), __FILE__, __LINE__);
            return {};
        }
        if (s.state() != RecordingSession::State::Starting) {
            QTest::qFail("not Starting after start()", __FILE__, __LINE__);
            return {};
        }
        if (!QTest::qWaitFor([&]() { return started.count() == 1; }, 15000)) {
            QTest::qFail(qPrintable("recordingStarted never came: " + errors.value(0).value(0).toString()),
                         __FILE__, __LINE__);
            return {};
        }
        const int id = s.currentRecordingId();
        QTest::qWait(seconds * 1000);
        const qint64 elapsed = s.elapsedSeconds();
        s.stop();
        const bool savingSeen = s.state() == RecordingSession::State::Saving;
        QTest::qWaitFor([&]() { return stopped.count() == 1; }, 15000);

        if (!QTest::qVerify(savingSeen, "Saving after stop()", "", __FILE__, __LINE__)) return {};
        if (!QTest::qVerify(stopped.count() == 1, "recordingStopped", "", __FILE__, __LINE__)) return {};
        if (!QTest::qVerify(stopped.at(0).at(3).toBool(), "wasRecording", "", __FILE__, __LINE__)) return {};
        if (!QTest::qVerify(s.isIdle(), "Idle at the end", "", __FILE__, __LINE__)) return {};
        if (!QTest::qVerify(errors.isEmpty(), qPrintable(errors.value(0).value(0).toString()), "", __FILE__, __LINE__)) return {};
        if (!QTest::qVerify(id > 0, "recording row id", "", __FILE__, __LINE__)) return {};
        if (!QTest::qVerify(elapsed >= seconds - 1, "elapsedSeconds advances", "", __FILE__, __LINE__)) return {};

        QList<RecordingSession::State> seq;
        for (const auto& args : states) seq << args.at(0).value<RecordingSession::State>();
        const QList<RecordingSession::State> expected = {RecordingSession::State::Starting,
            RecordingSession::State::Recording, RecordingSession::State::Saving, RecordingSession::State::Idle};
        if (!QTest::qVerify(seq == expected, "state sequence Starting, Recording, Saving, Idle", "", __FILE__, __LINE__)) return {};

        const QString path = stopped.at(0).at(0).toString();
        const qint64 encoded = stopped.at(0).at(1).toLongLong();
        qInfo("  %s: %lld frames encoded, %lld lost, %lld bytes", qPrintable(QFileInfo(path).fileName()), encoded,
              stopped.at(0).at(2).toLongLong(), QFileInfo(path).size());
        m_files << path;
        QTest::qVerify(encoded >= 60 * (seconds - 1), "frames encoded", "", __FILE__, __LINE__);
        // Grey test frames compress to almost nothing, so check the video's length, not its size
        const double duration = probeSeconds(path);
        qInfo("  playable length %.2f s", duration);
        QTest::qVerify(qAbs(duration - encoded / 60.0) < 0.1, "file plays for as long as was encoded", "", __FILE__, __LINE__);
        const Row row = rowFor(id);
        QTest::qVerify(row.path == path, "DB row points at the file", "", __FILE__, __LINE__);
        return row;
    }

private slots:
    void initTestCase()
    {
        qRegisterMetaType<RecordingSession::State>("RecordingSession::State");
        QVERIFY(m_dbDir.isValid());
        m_db = QSqlDatabase::addDatabase("QSQLITE");
        m_db.setDatabaseName(m_dbDir.filePath("empty.db"));
        QVERIFY(m_db.open());
        QVERIFY(ts::runMigrations(ts::initSqlPath()));
    }

    void cleanupTestCase()
    {
        for (const QString& f : m_files)
            QFile::remove(f);
        QDir(RecordingSession::mediaDir("recordings", "P_TEST", 7)).removeRecursively();
        QDir(QCoreApplication::applicationDirPath() + "/../recordings/P_TEST").removeRecursively();
    }

    void noSignal_refusesToStart()
    {
        RecordingSession s;
        QString error;
        QVERIFY(!s.start("P_TEST", 7, 0, &error));
        QVERIFY2(error.contains("No video signal"), qPrintable(error));
        QVERIFY(s.isIdle());
        QCOMPARE(ts::countRows("recordings"), 0);
    }

    void patientRecording()
    {
        RecordingSession s;
        FrameFeeder feed(s.recorder());
        feed.start();
        QTest::qWait(200);

        // While it runs, a second recording (e.g. the Dashboard's) is refused with a clear reason
        QSignalSpy started(&s, &RecordingSession::recordingStarted);
        QString error;
        QVERIFY(s.start("P_TEST", 7, 0, &error));
        QVERIFY(QTest::qWaitFor([&]() { return started.count() == 1; }, 15000));
        QVERIFY(!s.isArchive());
        QString second;
        QVERIFY(!s.start(QString(), -1, 0, &second));
        QCOMPARE(second, QString("A patient recording is running."));
        s.stop();
        QSignalSpy stopped(&s, &RecordingSession::recordingStopped);
        QVERIFY(QTest::qWaitFor([&]() { return stopped.count() == 1; }, 15000));
        m_files << stopped.at(0).at(0).toString();

        const Row row = recordAndStop(s, "P_TEST", 7, 3);
        QCOMPARE(row.patient.toString(), QString("P_TEST"));
        QCOMPARE(row.surgery.toInt(), 7);
        QVERIFY2(row.path.contains("/recordings/P_TEST/7/recording_"), qPrintable(row.path));
    }

    void archiveRecording()
    {
        RecordingSession s;
        FrameFeeder feed(s.recorder());
        feed.start();
        QTest::qWait(200);

        QSignalSpy started(&s, &RecordingSession::recordingStarted);
        QString error;
        QVERIFY(s.start(QString(), -1, 0, &error));
        QVERIFY(QTest::qWaitFor([&]() { return started.count() == 1; }, 15000));
        QVERIFY(s.isArchive());
        QString second;
        QVERIFY(!s.start("P_TEST", 7, 0, &second));
        QCOMPARE(second, QString("An Archive recording is running. Stop it from the Dashboard first."));
        s.stop();
        QSignalSpy stopped(&s, &RecordingSession::recordingStopped);
        QVERIFY(QTest::qWaitFor([&]() { return stopped.count() == 1; }, 15000));
        m_files << stopped.at(0).at(0).toString();

        const Row row = recordAndStop(s, QString(), -1, 3);
        QVERIFY(row.patient.isNull());   // Archive rows have no patient and no surgery
        QVERIFY(row.surgery.isNull());
        QVERIFY2(row.path.contains("/recordings/general/recording_"), qPrintable(row.path));
    }

    // Stop pressed before the encoder has opened: it still ends cleanly, nothing hangs
    void stopWhileStarting()
    {
        RecordingSession s;
        FrameFeeder feed(s.recorder());
        feed.start();
        QTest::qWait(200);
        QSignalSpy stopped(&s, &RecordingSession::recordingStopped);
        QString error;
        QVERIFY(s.start(QString(), -1, 0, &error));
        s.stop();
        QCOMPARE(s.state(), RecordingSession::State::Saving);
        QVERIFY(QTest::qWaitFor([&]() { return stopped.count() == 1; }, 15000));
        QVERIFY(s.isIdle());
        m_files << stopped.at(0).at(0).toString();

        // And the next recording works
        const Row row = recordAndStop(s, QString(), -1, 2);
        QVERIFY(!row.path.isEmpty());
    }

    // Recording also saved to "USB" (a folder standing in for the device): the copy follows
    // while recording, is identical and plays when the session reports it finished
    void mirroredRecording()
    {
        QTemporaryDir usb;
        UsbUtils::Device device;
        device.mountPath = usb.path();
        device.name = "TESTUSB";
        device.fileSystem = "exFAT";

        RecordingSession s;
        FrameFeeder feed(s.recorder());
        feed.start();
        QTest::qWait(200);
        QSignalSpy started(&s, &RecordingSession::recordingStarted);
        QSignalSpy stopped(&s, &RecordingSession::recordingStopped);
        QSignalSpy mirrorDone(&s, &RecordingSession::mirrorFinished);
        QSignalSpy status(&s, &RecordingSession::mirrorStatusChanged);
        QString error;
        QVERIFY2(s.start("P_TEST", 7, 0, &error, {device}), qPrintable(error));
        QVERIFY(QTest::qWaitFor([&]() { return started.count() == 1; }, 15000));
        QVERIFY(s.isMirroring());
        QVERIFY2(s.mirrorStatus().contains("Also saving to TESTUSB"), qPrintable(s.mirrorStatus()));
        const QString local = started.at(0).at(0).toString();
        const QString copy = usb.path() + "/SurgeryDownloads/" + QFileInfo(local).fileName();

        QTest::qWait(3000);
        QVERIFY2(QFileInfo(copy).size() > 0, "the copy follows while recording");
        s.stop();
        QVERIFY(QTest::qWaitFor([&]() { return stopped.count() == 1; }, 15000));
        QVERIFY(QTest::qWaitFor([&]() { return mirrorDone.count() == 1; }, 15000));
        QCOMPARE(mirrorDone.at(0).at(0).toString(), QString("TESTUSB"));
        QVERIFY2(mirrorDone.at(0).at(1).toBool(), qPrintable(mirrorDone.at(0).at(2).toString()));
        QVERIFY(!s.isMirroring());
        QVERIFY(!status.isEmpty());

        QFile a(local), b(copy);
        QVERIFY(a.open(QIODevice::ReadOnly) && b.open(QIODevice::ReadOnly));
        QVERIFY2(a.readAll() == b.readAll(), "USB copy identical to the local recording");
        const double localLen = probeSeconds(local), copyLen = probeSeconds(copy);
        qInfo("  local %.2f s, USB copy %.2f s, %lld bytes", localLen, copyLen, QFileInfo(copy).size());
        QVERIFY(copyLen > 2.5 && qAbs(copyLen - localLen) < 0.01);
        m_files << local;
    }

    // The same on the real USB devices that are connected, all at once (test files removed after)
    void mirroredRecordingToRealUsb()
    {
        const QList<UsbUtils::Device> devices = UsbUtils::listDevices();
        if (devices.isEmpty())
            QSKIP("no USB device connected");
        RecordingSession s;
        FrameFeeder feed(s.recorder());
        feed.start();
        QTest::qWait(200);
        QSignalSpy started(&s, &RecordingSession::recordingStarted);
        QSignalSpy stopped(&s, &RecordingSession::recordingStopped);
        QSignalSpy mirrorDone(&s, &RecordingSession::mirrorFinished);
        QString error;
        QVERIFY2(s.start(QString(), -1, 0, &error, devices), qPrintable(error));
        QVERIFY(QTest::qWaitFor([&]() { return started.count() == 1; }, 15000));
        const QString local = started.at(0).at(0).toString();
        QTest::qWait(3000);
        s.stop();
        QVERIFY(QTest::qWaitFor([&]() { return stopped.count() == 1; }, 15000));
        QVERIFY(QTest::qWaitFor([&]() { return mirrorDone.count() == devices.size(); }, 60000));

        QFile a(local);
        QVERIFY(a.open(QIODevice::ReadOnly));
        const QByteArray original = a.readAll();
        bool allOk = true, allSame = true, allPlay = true;
        for (const UsbUtils::Device& device : devices) {
            const QString copy = device.mountPath + "/Archive/" + QFileInfo(local).fileName();
            QFile b(copy);
            const bool same = b.open(QIODevice::ReadOnly) && b.readAll() == original;
            b.close();
            const double len = probeSeconds(copy);
            qInfo("  %s (%s): copy %s, plays %.2f s", qPrintable(device.name), qPrintable(device.fileSystem),
                  same ? "identical" : "DIFFERENT", len);
            allSame = allSame && same;
            allPlay = allPlay && len > 2.5;
            QFile::remove(copy);
            QDir(device.mountPath + "/Archive").rmdir(".");   // only if it is empty now
        }
        for (const auto& args : mirrorDone) {
            qInfo("  %s", qPrintable(args.at(2).toString()));
            allOk = allOk && args.at(1).toBool();
        }
        m_files << local;
        QVERIFY(allOk);
        QVERIFY(allSame);
        QVERIFY(allPlay);
    }

    // The file being recorded (and its later parts) is reported as in use, nothing else is
    void isWriting_onlyTheCurrentRecording()
    {
        RecordingSession s;
        FrameFeeder feed(s.recorder());
        feed.start();
        QTest::qWait(200);
        QVERIFY(!s.isWriting("/anything.mp4"));

        QSignalSpy started(&s, &RecordingSession::recordingStarted);
        QSignalSpy stopped(&s, &RecordingSession::recordingStopped);
        QString error;
        QVERIFY2(s.start(QString(), -1, 0, &error), qPrintable(error));
        QVERIFY(QTest::qWaitFor([&]() { return started.count() == 1; }, 15000));
        const QString path = started.at(0).at(0).toString();
        const QFileInfo info(path);
        QVERIFY(s.isWriting(path));
        QVERIFY(s.isWriting(info.path() + "/" + info.completeBaseName() + "_part2.mp4"));
        QVERIFY(!s.isWriting(info.path() + "/some_other_recording.mp4"));
        QVERIFY(!s.isWriting("/elsewhere/" + info.fileName()));

        s.stop();
        QVERIFY(s.isWriting(path));   // still being finalised
        QVERIFY(QTest::qWaitFor([&]() { return stopped.count() == 1; }, 15000));
        QVERIFY(!s.isWriting(path));
        m_files << path;
    }

    // Stop, then start again straight away: the second recording must not overwrite the first
    void sameSecond_distinctFiles()
    {
        RecordingSession s;
        FrameFeeder feed(s.recorder());
        feed.start();
        QTest::qWait(200);
        QStringList paths;
        // Begin just after a second starts, so both recordings fall in the same second
        QVERIFY(QTest::qWaitFor([]() { return QTime::currentTime().msec() < 50; }, 2000));
        for (int i = 0; i < 2; ++i) {
            QSignalSpy started(&s, &RecordingSession::recordingStarted);
            QSignalSpy stopped(&s, &RecordingSession::recordingStopped);
            QString error;
            QVERIFY2(s.start(QString(), -1, 0, &error), qPrintable(error));
            QVERIFY(QTest::qWaitFor([&]() { return started.count() == 1; }, 15000));
            s.stop();
            QVERIFY(QTest::qWaitFor([&]() { return stopped.count() == 1; }, 15000));
            paths << stopped.at(0).at(0).toString();
            m_files << paths.last();
        }
        qInfo("  first: %s  second: %s", qPrintable(QFileInfo(paths[0]).fileName()),
              qPrintable(QFileInfo(paths[1]).fileName()));
        QVERIFY2(paths[0] != paths[1], "second recording overwrote the first (same file name)");
    }
};

QTEST_MAIN(TstRecordingSession)
#include "tst_recording_session.moc"
