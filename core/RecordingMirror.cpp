#include "RecordingMirror.hpp"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QStringList>
#include <QTimer>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <fcntl.h>
#include <mutex>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

// State shared with the worker thread. The thread owns the file descriptors; everything else
// is guarded by `m`. Held by a shared_ptr so a worker stuck in a hanging USB write can be
// detached without touching a deleted object.
struct RecordingMirror::Shared {
    std::mutex m;
    std::condition_variable cv;

    QString destDir;
    QString deviceName;
    qint64 maxFileBytes = 0;

    QStringList pending;        // files not yet picked up by the worker
    bool finishing = false;
    std::atomic<bool> stop{false};   // also read by the worker while copying

    // Reported by the worker
    qint64 lagBytes = 0;        // on the local disk but not yet on the device
    bool done = false;
    bool failed = false;
    QString error;
    QStringList notices;        // not yet passed on
    bool limitReached = false;  // some file was cut at maxFileBytes
    bool exited = false;
};

namespace {

constexpr qint64 kChunk = qint64(8) << 20;
constexpr qint64 kSyncEvery = qint64(16) << 20;

struct FileCopy {
    QString src, dst;
    int srcFd = -1, dstFd = -1;
    qint64 copied = 0, synced = 0;
    bool closed = false;          // finished with (limit reached or, at the end, complete)
};

QString writeError(int err, const QString& device) {
    if (err == ENOSPC)
        return QString("%1 is full").arg(device);
    return QString("%1 was removed or can't be written (%2)").arg(device, QString::fromLocal8Bit(strerror(err)));
}

bool syncAndClose(FileCopy& f, const QString& device, QString* error) {
    bool ok = true;
    if (f.dstFd >= 0) {
        if (::fdatasync(f.dstFd) != 0) {
            *error = writeError(errno, device);
            ok = false;
        }
        ::close(f.dstFd);
        f.dstFd = -1;
    }
    if (f.srcFd >= 0) {
        ::close(f.srcFd);
        f.srcFd = -1;
    }
    f.closed = true;
    return ok;
}

} // namespace

RecordingMirror::RecordingMirror(const QString& firstFile, const QString& destDir, const QString& deviceName,
                                 qint64 maxFileBytes, QObject* parent)
    : QObject(parent), m_shared(std::make_shared<Shared>()), m_deviceName(deviceName) {
    m_shared->destDir = destDir;
    m_shared->deviceName = deviceName;
    m_shared->maxFileBytes = maxFileBytes;
    m_shared->pending << firstFile;
    m_text = QString("Also saving to %1").arg(deviceName);

    m_thread = std::thread(&RecordingMirror::run, m_shared);

    m_poll = new QTimer(this);
    m_poll->setInterval(500);
    connect(m_poll, &QTimer::timeout, this, &RecordingMirror::poll);
    m_poll->start();
}

RecordingMirror::~RecordingMirror() {
    {
        std::lock_guard<std::mutex> lock(m_shared->m);
        m_shared->stop = true;
    }
    m_shared->cv.notify_all();
    // A USB device that hangs can keep the thread in a write for a long time: don't let that
    // hang the app. The thread only touches the shared state, which it keeps alive itself.
    // System-clock deadlines (pthread_cond_timedwait) rather than wait_for: ThreadSanitizer can
    // follow those, so the locking here stays checkable. Every wait is also woken by a notify.
    std::unique_lock<std::mutex> lock(m_shared->m);
    const bool exited = m_shared->cv.wait_until(lock, std::chrono::system_clock::now() + std::chrono::seconds(3),
                                                [this] { return m_shared->exited; });
    lock.unlock();
    if (exited)
        m_thread.join();
    else
        m_thread.detach();
}

void RecordingMirror::addFile(const QString& path) {
    {
        std::lock_guard<std::mutex> lock(m_shared->m);
        m_shared->pending << path;
    }
    m_shared->cv.notify_all();
}

void RecordingMirror::finish() {
    {
        std::lock_guard<std::mutex> lock(m_shared->m);
        m_shared->finishing = true;
    }
    m_shared->cv.notify_all();
    poll();
}

void RecordingMirror::run(std::shared_ptr<Shared> shared) {
    std::vector<FileCopy> files;
    std::vector<char> buffer(kChunk);
    QString destDir, device;
    qint64 maxBytes = 0;
    {
        std::lock_guard<std::mutex> lock(shared->m);
        destDir = shared->destDir;
        device = shared->deviceName;
        maxBytes = shared->maxFileBytes;
    }

    QString error;
    QStringList notices;
    bool limitReached = false;

    // Copies what is on the local disk now. False on a read or write error.
    auto pump = [&](FileCopy& f, const std::atomic<bool>& stop) -> bool {
        if (f.closed)
            return true;
        if (f.srcFd < 0) {
            f.srcFd = ::open(f.src.toLocal8Bit().constData(), O_RDONLY | O_CLOEXEC);
            if (f.srcFd < 0) {
                error = QString("cannot read the recording %1").arg(QFileInfo(f.src).fileName());
                return false;
            }
        }
        if (f.dstFd < 0) {
            if (!QDir().mkpath(destDir)) {
                error = writeError(errno ? errno : EIO, device);
                return false;
            }
            f.dstFd = ::open(f.dst.toLocal8Bit().constData(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
            if (f.dstFd < 0) {
                error = writeError(errno, device);
                return false;
            }
        }
        struct stat st {};
        if (::fstat(f.srcFd, &st) != 0) {
            error = QString("cannot read the recording %1").arg(QFileInfo(f.src).fileName());
            return false;
        }
        const qint64 size = st.st_size;
        const qint64 target = maxBytes > 0 ? qMin(size, maxBytes) : size;

        while (f.copied < target && !stop.load()) {
            const qint64 want = qMin<qint64>(kChunk, target - f.copied);
            const ssize_t got = ::pread(f.srcFd, buffer.data(), size_t(want), f.copied);
            if (got <= 0)
                break;   // nothing more on disk right now
            ssize_t written = 0;
            while (written < got) {
                const ssize_t w = ::write(f.dstFd, buffer.data() + written, size_t(got - written));
                if (w < 0) {
                    if (errno == EINTR)
                        continue;
                    error = writeError(errno, device);
                    return false;
                }
                written += w;
            }
            f.copied += got;
            if (f.copied - f.synced >= kSyncEvery) {
                if (::fdatasync(f.dstFd) != 0) {
                    error = writeError(errno, device);
                    return false;
                }
                ::posix_fadvise(f.dstFd, f.synced, f.copied - f.synced, POSIX_FADV_DONTNEED);
                f.synced = f.copied;
            }
        }

        // FAT32: the copy of this file ends here; the recording continues locally
        if (maxBytes > 0 && f.copied >= maxBytes && size > maxBytes) {
            notices << QString("%1 reached the 4 GB limit (FAT32): the rest of %2 is saved on this system only")
                           .arg(device, QFileInfo(f.src).fileName());
            limitReached = true;
            if (!syncAndClose(f, device, &error))
                return false;
        }
        return true;
    };

    for (;;) {
        bool finishing = false, stop = false;
        {
            std::lock_guard<std::mutex> lock(shared->m);
            for (const QString& src : shared->pending) {
                FileCopy f;
                f.src = src;
                f.dst = QDir(destDir).filePath(QFileInfo(src).fileName());
                files.push_back(f);
            }
            shared->pending.clear();
            finishing = shared->finishing;
            stop = shared->stop.load();
        }
        if (stop)
            break;

        bool ok = true;
        for (FileCopy& f : files) {
            if (!pump(f, shared->stop)) {
                ok = false;
                break;
            }
        }

        qint64 lag = 0;
        for (const FileCopy& f : files) {
            if (f.closed || f.srcFd < 0)
                continue;
            struct stat st {};
            if (::fstat(f.srcFd, &st) == 0) {
                const qint64 target = maxBytes > 0 ? qMin<qint64>(st.st_size, maxBytes) : st.st_size;
                lag += qMax<qint64>(0, target - f.copied);
            }
        }

        bool done = false;
        if (ok && finishing && lag == 0) {
            // Everything recorded is on the device: flush it and check the sizes
            for (FileCopy& f : files) {
                const QString src = f.src;
                const qint64 copied = f.copied;
                const bool cut = f.closed;
                if (!f.closed && !syncAndClose(f, device, &error)) {
                    ok = false;
                    break;
                }
                const qint64 local = QFileInfo(src).size();
                if (!cut && copied != local) {
                    error = QString("the copy of %1 on %2 is incomplete").arg(QFileInfo(src).fileName(), device);
                    ok = false;
                    break;
                }
            }
            done = ok;
        }

        {
            std::lock_guard<std::mutex> lock(shared->m);
            shared->lagBytes = lag;
            shared->notices << notices;
            notices.clear();
            shared->limitReached = shared->limitReached || limitReached;
            if (!ok) {
                shared->failed = true;
                shared->error = error;
            }
            shared->done = done;
        }
        if (!ok || done)
            break;

        std::unique_lock<std::mutex> lock(shared->m);
        shared->cv.wait_until(lock, std::chrono::system_clock::now() + std::chrono::seconds(1), [&] {
            return shared->stop || !shared->pending.isEmpty() || (shared->finishing && !finishing);
        });
    }

    for (FileCopy& f : files) {
        if (f.dstFd >= 0)
            ::close(f.dstFd);
        if (f.srcFd >= 0)
            ::close(f.srcFd);
    }
    {
        std::lock_guard<std::mutex> lock(shared->m);
        shared->exited = true;
    }
    shared->cv.notify_all();
}

void RecordingMirror::poll() {
    qint64 lag = 0;
    bool done = false, failed = false, finishing = false, limitReached = false;
    QString error;
    QStringList notices;
    {
        std::lock_guard<std::mutex> lock(m_shared->m);
        lag = m_shared->lagBytes;
        done = m_shared->done;
        failed = m_shared->failed;
        finishing = m_shared->finishing;
        limitReached = m_shared->limitReached;
        error = m_shared->error;
        notices = m_shared->notices;
        m_shared->notices.clear();
    }
    for (const QString& n : notices)
        emit notice(n);

    State state = State::Copying;
    QString text = QString("Also saving to %1").arg(m_deviceName);
    const QString left = QString("%1 MB").arg(qMax<qint64>(1, lag / 1000000));
    if (failed) {
        state = State::Failed;
        text = QString("Saving to USB stopped: %1. The recording continues on this system.").arg(error);
    } else if (done) {
        state = State::Done;
        text = limitReached
            ? QString("Copy to %1 finished, cut at 4 GB (FAT32). The full recording is on this system.").arg(m_deviceName)
            : QString("Copy to %1 finished. You can remove it.").arg(m_deviceName);
    } else if (finishing) {
        state = State::Finishing;
        text = QString("Finishing copy to %1… %2 left").arg(m_deviceName, left);
    } else if (lag > kBehindBytes) {
        state = State::Behind;
        text = QString("Saving to %1 is falling behind (%2 to copy)").arg(m_deviceName, left);
    }

    if (state != m_state || text != m_text) {
        m_state = state;
        m_text = text;
        emit stateChanged(state, text);
    }
    if ((done || failed) && !m_finishedEmitted) {
        m_finishedEmitted = true;
        m_poll->stop();
        if (failed)
            qWarning() << "Recording mirror to" << m_deviceName << "failed:" << error;
        emit finished(done, text);
    }
}
