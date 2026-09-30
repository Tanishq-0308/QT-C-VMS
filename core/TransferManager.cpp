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
    // One copy per device at a time is enforced by the queues; the pool only has to be large
    // enough for every device that can be plugged in at once
    m_pool = new QThreadPool(this);
    m_pool->setMaxThreadCount(32);

    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(200);
    connect(m_pollTimer, &QTimer::timeout, this, &TransferManager::pollProgress);
}

TransferManager::~TransferManager() {
    // Stop the copies in progress (their partial files are discarded) before the pool goes away
    for (const Active& active : m_active)
        active.cancel->store(true);
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

int TransferManager::enqueue(const QStringList& sources, const QString& destDir, const QString& deviceName) {
    const QString device = UsbUtils::volumeRoot(destDir);
    const QString shownName = !deviceName.isEmpty() ? deviceName : QFileInfo(device).fileName().isEmpty()
                                                                       ? device : QFileInfo(device).fileName();
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
        job.device = device;
        job.deviceName = shownName;
        job.size = info.size();
        m_jobs.append(job);
        ++added;
        emit jobAdded(job.id);
    }

    if (added > 0) {
        emit activeCountChanged(activeCount());
        startNext(device);
    }
    return added;
}

// Starts the device's next queued file, unless it is already copying one
void TransferManager::startNext(const QString& device) {
    if (m_active.contains(device))
        return;

    for (;;) {
        Job* next = nullptr;
        for (Job& j : m_jobs) {
            if (j.state == State::Queued && j.device == device) {
                next = &j;
                break;
            }
        }
        if (!next) {
            reportIfIdle(device);
            return;
        }

        // Checks that would otherwise fail only after minutes of copying
        QString problem;
        if (!QFileInfo::exists(next->source)) {
            problem = "The file no longer exists on this system";
        } else if (!QDir().mkpath(next->destDir)) {
            problem = "USB stick not found or not writable";
        } else if (next->size > UsbUtils::kFat32MaxFileSize && UsbUtils::isFat32(next->destDir)) {
            problem = "The stick is FAT32, which cannot hold files over 4 GB. Format it as exFAT.";
        } else {
            const QStorageInfo storage(next->destDir);
            if (storage.isValid() && storage.bytesAvailable() >= 0 && next->size > storage.bytesAvailable())
                problem = "Not enough free space on the USB stick";
        }
        if (!problem.isEmpty()) {
            finishJob(*next, State::Failed, problem);
            continue;   // try the device's next file
        }

        next->state = State::Copying;
        next->bytesDone = 0;
        next->bytesPerSecond = 0;

        Active active;
        active.jobId = next->id;
        active.bytes = std::make_shared<std::atomic<qint64>>(0);
        active.cancel = std::make_shared<std::atomic<bool>>(false);
        active.speedClock.start();
        active.watcher = new QFutureWatcher<CopyResult>(this);
        connect(active.watcher, &QFutureWatcher<CopyResult>::finished, this,
                [this, device]() { onCopyFinished(device); });
        const QString dest = QDir(next->destDir).filePath(next->fileName);
        const int id = next->id;
        active.watcher->setFuture(QtConcurrent::run(m_pool, &TransferManager::copyFile,
                                                    next->source, dest, active.bytes, active.cancel));
        m_active.insert(device, active);
        m_pollTimer->start();
        emit jobChanged(id);
        return;
    }
}

void TransferManager::onCopyFinished(const QString& device) {
    auto it = m_active.find(device);
    if (it == m_active.end())
        return;
    const Active active = it.value();
    m_active.erase(it);
    const CopyResult result = active.watcher->result();
    active.watcher->deleteLater();

    if (Job* job = findJob(active.jobId)) {
        job->bytesDone = active.bytes->load();
        if (result.ok)
            finishJob(*job, State::Done);
        else if (result.cancelled)
            finishJob(*job, State::Cancelled);
        else
            finishJob(*job, State::Failed, result.error);
    }
    if (m_active.isEmpty())
        m_pollTimer->stop();
    startNext(device);
}

void TransferManager::pollProgress() {
    for (auto it = m_active.begin(); it != m_active.end(); ++it) {
        Active& active = it.value();
        Job* job = findJob(active.jobId);
        if (!job)
            continue;

        const qint64 bytes = active.bytes->load();
        const qint64 elapsedMs = active.speedClock.elapsed();
        if (elapsedMs >= 1000) {
            const double instant = double(bytes - active.speedBytes) * 1000.0 / double(elapsedMs);
            // Smoothed, so the figure doesn't jump around with every sync
            job->bytesPerSecond = job->bytesPerSecond > 0 ? 0.7 * job->bytesPerSecond + 0.3 * instant : instant;
            active.speedBytes = bytes;
            active.speedClock.restart();
        }
        job->bytesDone = bytes;
        emit jobChanged(job->id);
    }
}

void TransferManager::finishJob(Job& job, State state, const QString& error) {
    job.state = state;
    job.error = error;
    job.bytesPerSecond = 0;
    Counts& deviceBatch = m_deviceBatch[job.device];
    if (state == State::Done) {
        job.bytesDone = job.size;
        ++m_batch.succeeded;
        ++deviceBatch.succeeded;
    } else if (state == State::Failed) {
        ++m_batch.failed;
        ++deviceBatch.failed;
        if (!m_unseenFailures) {
            m_unseenFailures = true;
            emit failuresChanged(true);
        }
    } else if (state == State::Cancelled) {
        ++m_batch.cancelled;
        ++deviceBatch.cancelled;
    }
    emit jobChanged(job.id);
    emit activeCountChanged(activeCount());
}

// Tells when a device has nothing left to copy, and when no device has
void TransferManager::reportIfIdle(const QString& device) {
    if (m_active.contains(device))
        return;
    for (const Job& j : m_jobs) {
        if (j.device == device && j.state == State::Queued)
            return;
    }

    const Counts counts = m_deviceBatch.take(device);
    if (counts.any()) {
        QString name = device;
        for (const Job& j : m_jobs) {
            if (j.device == device)
                name = j.deviceName;
        }
        emit deviceFinished(name, counts.succeeded, counts.failed, counts.cancelled);
    }

    if (activeCount() == 0) {
        emit activeCountChanged(0);
        const Counts all = m_batch;
        m_batch = Counts();
        if (all.any())
            emit queueDrained(all.succeeded, all.failed, all.cancelled);
    }
}

void TransferManager::cancel(int id) {
    Job* job = findJob(id);
    if (!job)
        return;
    if (job->state == State::Queued) {
        const QString device = job->device;
        finishJob(*job, State::Cancelled);
        reportIfIdle(device);
    } else if (job->state == State::Copying) {
        for (const Active& active : m_active) {
            if (active.jobId == id)
                active.cancel->store(true);   // onCopyFinished() marks it once the worker stops
        }
    }
}

void TransferManager::cancelAll() {
    QStringList devices;
    for (Job& job : m_jobs) {
        if (job.state == State::Queued) {
            if (!devices.contains(job.device))
                devices << job.device;
            finishJob(job, State::Cancelled);
        }
    }
    for (const Active& active : m_active)
        active.cancel->store(true);
    for (const QString& device : devices)
        reportIfIdle(device);   // devices that were only waiting
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

int TransferManager::copyingCount() const {
    return m_active.size();
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
