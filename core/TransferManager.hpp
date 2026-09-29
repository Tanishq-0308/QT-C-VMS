#pragma once

#include <QObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <QElapsedTimer>
#include <atomic>
#include <memory>

class QTimer;
class QThreadPool;
template <typename T> class QFutureWatcher;

// Copies files to a USB stick in the background, one file at a time, for the whole app.
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
        qint64 size = 0;
        qint64 bytesDone = 0;
        double bytesPerSecond = 0;   // only while Copying
        State state = State::Queued;
        QString error;
    };

    explicit TransferManager(QObject* parent = nullptr);
    ~TransferManager() override;

    // Queues one job per file. Files already queued or copying to the same folder are skipped.
    // Returns the number of jobs added.
    int enqueue(const QStringList& sources, const QString& destDir);

    void cancel(int id);
    void cancelAll();
    void clearFinished();

    QList<Job> jobs() const { return m_jobs; }
    const Job* job(int id) const;
    int activeCount() const;          // Queued + Copying
    bool hasUnseenFailures() const { return m_unseenFailures; }
    void markFailuresSeen();

signals:
    void jobAdded(int id);
    void jobChanged(int id);
    void jobsRemoved();
    void activeCountChanged(int count);
    void failuresChanged(bool unseen);
    // The queue has emptied; counts cover every job finished since it was last empty
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

    Job* findJob(int id);
    void startNext();
    void onCopyFinished();
    void pollProgress();
    void finishJob(Job& job, State state, const QString& error = QString());

    QList<Job> m_jobs;
    int m_nextId = 1;

    // The job being copied
    int m_currentId = 0;
    std::shared_ptr<std::atomic<qint64>> m_currentBytes;
    std::shared_ptr<std::atomic<bool>> m_currentCancel;
    QFutureWatcher<CopyResult>* m_watcher = nullptr;
    QElapsedTimer m_speedClock;
    qint64 m_speedBytes = 0;

    QThreadPool* m_pool = nullptr;    // one thread: parallel writes to one stick are slower
    QTimer* m_pollTimer = nullptr;

    int m_batchSucceeded = 0;
    int m_batchFailed = 0;
    int m_batchCancelled = 0;
    bool m_unseenFailures = false;
};
