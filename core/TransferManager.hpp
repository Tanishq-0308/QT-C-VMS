#pragma once

#include <QObject>
#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <atomic>
#include <memory>

class QTimer;
class QThreadPool;
template <typename T> class QFutureWatcher;

// Copies files to USB devices in the background, for the whole app.
//
// Each device has its own queue: one file at a time per device (parallel writes to one stick
// are slower), different devices at the same time.
//
// Owned by HomePage, so a copy keeps running (and keeps reporting) when the page that
// started it is closed or rebuilt. TransferDrawer shows the queue; pages only enqueue.
class TransferManager : public QObject {
    Q_OBJECT

public:
    enum class State { Queued, Copying, Done, Failed, Cancelled };

    struct Job {
        int id = 0;
        QString source;
        QString destDir;
        QString fileName;
        QString device;       // mount root of the device destDir is on: its queue
        QString deviceName;   // shown to the user
        qint64 size = 0;
        qint64 bytesDone = 0;
        double bytesPerSecond = 0;   // only while Copying
        State state = State::Queued;
        QString error;
    };

    explicit TransferManager(QObject* parent = nullptr);
    ~TransferManager() override;

    // Queues one job per file. Files already queued or copying to the same folder are skipped.
    // `deviceName` is what the user sees for that device (its label); the folder's volume root
    // is used when it is empty. Returns the number of jobs added.
    int enqueue(const QStringList& sources, const QString& destDir, const QString& deviceName = QString());

    void cancel(int id);
    void cancelAll();
    void clearFinished();

    QList<Job> jobs() const { return m_jobs; }
    const Job* job(int id) const;
    int activeCount() const;          // Queued + Copying
    int copyingCount() const;         // files being copied right now (one per busy device)
    bool isActiveSource(const QString& source) const;   // queued or being copied right now
    bool hasUnseenFailures() const { return m_unseenFailures; }
    void markFailuresSeen();

signals:
    void jobAdded(int id);
    void jobChanged(int id);
    void jobsRemoved();
    void activeCountChanged(int count);
    void failuresChanged(bool unseen);
    // One device has nothing left to copy; counts cover its jobs since it was last idle.
    // The device can be removed once this has been emitted.
    void deviceFinished(const QString& deviceName, int succeeded, int failed, int cancelled);
    // Every queue has emptied; counts cover every job finished since they were last all empty
    void queueDrained(int succeeded, int failed, int cancelled);

private:
    struct CopyResult {
        bool ok = false;
        bool cancelled = false;
        QString error;
    };
    static CopyResult copyFile(const QString& source, const QString& dest,
                               std::shared_ptr<std::atomic<qint64>> bytesDone,
                               std::shared_ptr<std::atomic<bool>> cancelled);

    // The copy running on one device
    struct Active {
        int jobId = 0;
        std::shared_ptr<std::atomic<qint64>> bytes;
        std::shared_ptr<std::atomic<bool>> cancel;
        QFutureWatcher<CopyResult>* watcher = nullptr;
        QElapsedTimer speedClock;
        qint64 speedBytes = 0;
    };
    struct Counts {
        int succeeded = 0, failed = 0, cancelled = 0;
        bool any() const { return succeeded + failed + cancelled > 0; }
    };

    Job* findJob(int id);
    void startNext(const QString& device);
    void onCopyFinished(const QString& device);
    void pollProgress();
    void finishJob(Job& job, State state, const QString& error = QString());
    void reportIfIdle(const QString& device);

    QList<Job> m_jobs;
    int m_nextId = 1;

    QHash<QString, Active> m_active;      // by device
    QHash<QString, Counts> m_deviceBatch; // by device, since that device was last idle
    Counts m_batch;                       // all devices, since everything was last idle

    QThreadPool* m_pool = nullptr;    // one thread per busy device
    QTimer* m_pollTimer = nullptr;

    bool m_unseenFailures = false;
};
