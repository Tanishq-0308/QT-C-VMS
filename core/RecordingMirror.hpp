#pragma once

#include <QObject>
#include <QString>
#include <memory>
#include <thread>

class QTimer;

// Copies a recording to a USB device while it is being recorded.
//
// The recorder writes the local file as always; this only reads what is already on the local
// disk and appends it to the copy, on its own thread. A slow, full or removed USB device
// therefore never holds up the recording: the copy falls behind or stops, the local recording
// carries on. The recorder writes fragmented MP4, so a copy that stops early still plays up to
// that point.
class RecordingMirror : public QObject {
    Q_OBJECT

public:
    enum class State { Copying, Behind, Finishing, Done, Failed };

    // `maxFileBytes` > 0 caps each copied file (FAT32: UsbUtils::kFat32MaxFileSize); the copy of
    // a file stops there and the recording itself continues locally.
    RecordingMirror(const QString& firstFile, const QString& destDir, const QString& deviceName,
                    qint64 maxFileBytes, QObject* parent = nullptr);
    ~RecordingMirror() override;

    void addFile(const QString& path);   // a later part of the recording (input format changed)
    void finish();                       // the recording stopped: copy the rest, then finished()

    State state() const { return m_state; }
    QString deviceName() const { return m_deviceName; }
    QString statusText() const { return m_text; }

    // More than this still to copy counts as falling behind (about 30 s of 1080p60)
    static constexpr qint64 kBehindBytes = qint64(64) << 20;

signals:
    void stateChanged(RecordingMirror::State state, const QString& text);
    void notice(const QString& text);                  // e.g. a file reached the FAT32 limit
    void finished(bool ok, const QString& message);    // once: Done or Failed

private:
    struct Shared;
    static void run(std::shared_ptr<Shared> shared);
    void poll();

    std::shared_ptr<Shared> m_shared;
    std::thread m_thread;
    QTimer* m_poll = nullptr;
    QString m_deviceName;
    State m_state = State::Copying;
    QString m_text;
    bool m_finishedEmitted = false;
};
