#include "TransferManager.hpp"
#include "UsbUtils.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QSaveFile>
#include <QStorageInfo>
#include <QThreadPool>
#include <QTimer>
#include <QDebug>
#include <QtConcurrent/QtConcurrent>
#include <fcntl.h>
#include <unistd.h>

TransferManager::TransferManager(QObject* parent) : QObject(parent) {
    m_pool = new QThreadPool(this);
    m_pool->setMaxThreadCount(1);

    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(200);
    connect(m_pollTimer, &QTimer::timeout, this, &TransferManager::pollProgress);
}

TransferManager::~TransferManager() {
    // Stop the copy in progress (its partial file is discarded) before the pool goes away
    if (m_currentCancel)
        m_currentCancel->store(true);
    m_pool->waitForDone();
}

// Runs on the pool thread. The file is written to a temporary file next to the destination
// and renamed over it at the end, so an existing copy is only replaced by a complete one
// (pulling the stick mid-copy never destroys a previous copy).
//
// Progress counts only data the stick has confirmed (synced), so the progress, speed and time
// left in the drawer are what is really on the stick. Counting bytes handed to the page cache
// instead showed the first second at memory speed (e.g. 42 MB/s for a 23 MB/s stick).
// Syncing every 16 MiB keeps those figures current (under a second of data at USB-stick speeds)
// and costs about 3% of throughput against 64 MiB (measured: 25.2 vs 26.1 MB/s on FAT32; on FAT
// every sync also rewrites the allocation table). Pages already on the stick are dropped from
// the cache so a multi-GB copy doesn't push the rest of the system out of memory.
TransferManager::CopyResult TransferManager::copyFile(const QString& source, const QString& dest,
                                                      std::shared_ptr<std::atomic<qint64>> bytesDone,
                                                      std::shared_ptr<std::atomic<bool>> cancelled)
{
    constexpr qint64 kBufferSize = 8 << 20;
    constexpr qint64 kSyncChunk = 16 << 20;
    CopyResult result;

    QFile in(source);
    if (!in.open(QIODevice::ReadOnly)) {
        result.error = "Cannot read the file: " + in.errorString();
        return result;
    }
    QSaveFile out(dest);
    if (!out.open(QIODevice::WriteOnly)) {
        result.error = "Cannot write to the USB stick: " + out.errorString();
        return result;
    }
    ::posix_fadvise(in.handle(), 0, 0, POSIX_FADV_SEQUENTIAL);

    QByteArray buffer(kBufferSize, Qt::Uninitialized);
    qint64 written = 0;
    qint64 synced = 0;
    bool ok = true;

    while (ok && !in.atEnd()) {
        if (cancelled->load()) {
            out.cancelWriting();
            result.cancelled = true;
            return result;
        }
        const qint64 n = in.read(buffer.data(), buffer.size());
        ok = n >= 0 && out.write(buffer.constData(), n) == n;
        if (!ok)
            break;
        written += n;

        if (written - synced >= kSyncChunk) {
            ok = out.flush() && ::fdatasync(out.handle()) == 0;
            ::posix_fadvise(out.handle(), synced, written - synced, POSIX_FADV_DONTNEED);
            ::posix_fadvise(in.handle(), synced, written - synced, POSIX_FADV_DONTNEED);
            synced = written;
            if (ok)
                bytesDone->store(synced);
        }
    }

    ok = ok && in.error() == QFileDevice::NoError
            && out.flush() && ::fdatasync(out.handle()) == 0 && out.commit();
    if (!ok) {
        result.error = in.error() != QFileDevice::NoError ? in.errorString() : out.errorString();
        out.cancelWriting();
        qWarning() << "USB copy failed:" << source << "->" << dest << result.error;
        return result;
    }
    result.ok = true;
    return result;
}

int TransferManager::enqueue(const QStringList& sources, const QString& destDir) {
    int added = 0;
    for (const QString& source : sources) {
        const QFileInfo info(source);
        bool duplicate = false;
        for (const Job& j : m_jobs) {
            if (j.source == source && j.destDir == destDir &&
                (j.state == State::Queued || j.state == State::Copying)) {
                duplicate = true;
                break;
            }
        }
        if (duplicate)
            continue;

        Job job;
        job.id = m_nextId++;
        job.source = source;
        job.destDir = destDir;
        job.fileName = info.fileName();
        job.size = info.size();
        m_jobs.append(job);
        ++added;
        emit jobAdded(job.id);
    }

    if (added > 0) {
        emit activeCountChanged(activeCount());
        if (m_currentId == 0)
            startNext();
    }
    return added;
}

void TransferManager::startNext() {
    Job* next = nullptr;
    for (Job& j : m_jobs) {
        if (j.state == State::Queued) {
            next = &j;
            break;
        }
    }

    if (!next) {
        m_pollTimer->stop();
        emit activeCountChanged(0);
        if (m_batchSucceeded + m_batchFailed + m_batchCancelled > 0)
            emit queueDrained(m_batchSucceeded, m_batchFailed, m_batchCancelled);
        m_batchSucceeded = m_batchFailed = m_batchCancelled = 0;
        return;
    }

    // Checks that would otherwise fail only after minutes of copying
    if (!QFileInfo::exists(next->source)) {
        finishJob(*next, State::Failed, "The file no longer exists on this system");
        QTimer::singleShot(0, this, &TransferManager::startNext);
        return;
    }
    if (!QDir().mkpath(next->destDir)) {
        finishJob(*next, State::Failed, "USB stick not found or not writable");
        QTimer::singleShot(0, this, &TransferManager::startNext);
        return;
    }
    if (next->size > UsbUtils::kFat32MaxFileSize && UsbUtils::isFat32(next->destDir)) {
        finishJob(*next, State::Failed,
                  "The stick is FAT32, which cannot hold files over 4 GB. Format it as exFAT.");
        QTimer::singleShot(0, this, &TransferManager::startNext);
        return;
    }
    const QStorageInfo storage(next->destDir);
    if (storage.isValid() && storage.bytesAvailable() >= 0 && next->size > storage.bytesAvailable()) {
        finishJob(*next, State::Failed, "Not enough free space on the USB stick");
        QTimer::singleShot(0, this, &TransferManager::startNext);
        return;
    }

    next->state = State::Copying;
    next->bytesDone = 0;
    next->bytesPerSecond = 0;
    m_currentId = next->id;
    m_currentBytes = std::make_shared<std::atomic<qint64>>(0);
    m_currentCancel = std::make_shared<std::atomic<bool>>(false);
    m_speedBytes = 0;
    m_speedClock.start();
    emit jobChanged(next->id);

    if (!m_watcher) {
        m_watcher = new QFutureWatcher<CopyResult>(this);
        connect(m_watcher, &QFutureWatcher<CopyResult>::finished, this, &TransferManager::onCopyFinished);
    }
    const QString dest = QDir(next->destDir).filePath(next->fileName);
    m_watcher->setFuture(QtConcurrent::run(m_pool, &TransferManager::copyFile,
                                           next->source, dest, m_currentBytes, m_currentCancel));
    m_pollTimer->start();
}

void TransferManager::onCopyFinished() {
    const CopyResult result = m_watcher->result();
    const int id = m_currentId;
    m_currentId = 0;
    const qint64 bytes = m_currentBytes ? m_currentBytes->load() : 0;
    m_currentBytes.reset();
    m_currentCancel.reset();

    if (Job* job = findJob(id)) {
        job->bytesDone = bytes;
        if (result.ok)
            finishJob(*job, State::Done);
        else if (result.cancelled)
            finishJob(*job, State::Cancelled);
        else
            finishJob(*job, State::Failed, result.error);
    }
    startNext();
}

void TransferManager::pollProgress() {
    Job* job = findJob(m_currentId);
    if (!job || !m_currentBytes)
        return;

    const qint64 bytes = m_currentBytes->load();
    const qint64 elapsedMs = m_speedClock.elapsed();
    if (elapsedMs >= 1000) {
        const double instant = double(bytes - m_speedBytes) * 1000.0 / double(elapsedMs);
        // Smoothed, so the figure doesn't jump around with every sync
        job->bytesPerSecond = job->bytesPerSecond > 0 ? 0.7 * job->bytesPerSecond + 0.3 * instant : instant;
        m_speedBytes = bytes;
        m_speedClock.restart();
    }
    job->bytesDone = bytes;
    emit jobChanged(job->id);
}

void TransferManager::finishJob(Job& job, State state, const QString& error) {
    job.state = state;
    job.error = error;
    job.bytesPerSecond = 0;
    if (state == State::Done) {
        job.bytesDone = job.size;
        ++m_batchSucceeded;
    } else if (state == State::Failed) {
        ++m_batchFailed;
        if (!m_unseenFailures) {
            m_unseenFailures = true;
            emit failuresChanged(true);
        }
    } else if (state == State::Cancelled) {
        ++m_batchCancelled;
    }
    emit jobChanged(job.id);
    emit activeCountChanged(activeCount());
}

void TransferManager::cancel(int id) {
    Job* job = findJob(id);
    if (!job)
        return;
    if (job->state == State::Queued) {
        finishJob(*job, State::Cancelled);
        if (activeCount() == 0)
            startNext();   // reports the drained queue
    } else if (job->state == State::Copying && m_currentCancel) {
        m_currentCancel->store(true);   // onCopyFinished() marks it once the worker stops
    }
}

void TransferManager::cancelAll() {
    for (Job& job : m_jobs) {
        if (job.state == State::Queued)
            finishJob(job, State::Cancelled);
    }
    if (m_currentCancel)
        m_currentCancel->store(true);
    else if (activeCount() == 0)
        startNext();
}

void TransferManager::clearFinished() {
    const int before = m_jobs.size();
    for (int i = m_jobs.size() - 1; i >= 0; --i) {
        const State s = m_jobs[i].state;
        if (s == State::Done || s == State::Failed || s == State::Cancelled)
            m_jobs.removeAt(i);
    }
    if (m_jobs.size() != before)
        emit jobsRemoved();
    markFailuresSeen();
}

const TransferManager::Job* TransferManager::job(int id) const {
    for (const Job& j : m_jobs) {
        if (j.id == id)
            return &j;
    }
    return nullptr;
}

TransferManager::Job* TransferManager::findJob(int id) {
    for (Job& j : m_jobs) {
        if (j.id == id)
            return &j;
    }
    return nullptr;
}

int TransferManager::activeCount() const {
    int count = 0;
    for (const Job& j : m_jobs) {
        if (j.state == State::Queued || j.state == State::Copying)
            ++count;
    }
    return count;
}

bool TransferManager::isActiveSource(const QString& source) const {
    for (const Job& j : m_jobs) {
        if (j.source == source && (j.state == State::Queued || j.state == State::Copying))
            return true;
    }
    return false;
}

void TransferManager::markFailuresSeen() {
    if (m_unseenFailures) {
        m_unseenFailures = false;
        emit failuresChanged(false);
    }
}
