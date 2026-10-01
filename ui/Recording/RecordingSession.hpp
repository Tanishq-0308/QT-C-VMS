#pragma once

#include <QObject>
#include <QElapsedTimer>
#include <QList>
#include <QString>
#include "core/UsbUtils.hpp"

class VideoRecorder;
class RecordingMirror;

// The app's one recording at a time. Owns the VideoRecorder (the capture device feeds it
// directly) and does the bookkeeping around it: file paths, the `recordings` rows and state.
//
// Owned by HomePage and shared by the Dashboard (Archive recordings: no patient, started and
// stopped in place, running in the background while the user goes anywhere) and RecordingPage
// (patient recordings).
class RecordingSession : public QObject {
    Q_OBJECT

public:
    enum class State { Idle, Starting, Recording, Saving };

    explicit RecordingSession(QObject* parent = nullptr);
    ~RecordingSession() override;

    VideoRecorder* recorder() const { return m_recorder; }

    // Starts recording for a patient/surgery, or an Archive recording when patientId is empty.
    // Returns false (with the reason) when nothing was started. `mirrorTo`: USB devices that also
    // get the recording, copied while it is recorded (RecordingMirror) into the folder downloads
    // use (SurgeryDownloads, or Archive for Archive recordings).
    bool start(const QString& patientId, int surgeryId, int flipStep, QString* errorMessage = nullptr,
               const QList<UsbUtils::Device>& mirrorTo = {});
    // Returns immediately; recordingStopped() follows once the file is finalised
    void stop();
    void setFlipStep(int flipStep);

    State state() const { return m_state; }
    bool isIdle() const { return m_state == State::Idle; }
    bool isRecording() const { return m_state == State::Recording; }
    // Whether the current (or last) recording is an Archive recording
    bool isArchive() const { return m_patientId.isEmpty(); }
    int currentRecordingId() const { return m_recordingId; }
    qint64 elapsedSeconds() const;
    qint64 droppedFrames() const { return m_droppedFrames; }
    // Whether `path` is the file being recorded now (or a later part of it), so it must not be
    // renamed or deleted
    bool isWriting(const QString& path) const;

    // Bytes per second a recording started now would take (for USB space estimates)
    qint64 expectedBytesPerSecond() const;
    // Copies to USB still running (also after Stop, until they have caught up)
    bool isMirroring() const { return !m_mirrors.isEmpty(); }
    // One line about the USB copies and how serious it is: 0 fine, 1 behind/finishing, 2 problem
    QString mirrorStatus(int* level = nullptr) const;

    // <app>/../<kind>/<patient>/<surgery>, or <app>/../<kind>/general for the Archive
    static QString mediaDir(const QString& kind, const QString& patientId, int surgeryId);

signals:
    void stateChanged(RecordingSession::State state);
    void recordingStarted(const QString& path);
    // wasRecording is false when the start failed (errorOccurred() has the reason)
    void recordingStopped(const QString& path, qint64 framesEncoded, qint64 framesDropped, bool wasRecording);
    void segmentStarted(const QString& path);   // input format changed, continued in a new file
    void framesDropped(qint64 total);
    void errorOccurred(const QString& message);
    void mirrorStatusChanged(const QString& text, int level);   // empty text: no USB copy
    void mirrorNotice(const QString& text);                      // e.g. FAT32 limit reached
    void mirrorFinished(const QString& deviceName, bool ok, const QString& message);

private:
    void setState(State state);
    int insertRecordingRow(const QString& path);
    void onRecordingStarted(const QString& path);
    void onRecordingStopped(const QString& path, qint64 framesEncoded, qint64 framesDropped);
    void onSegmentStarted(const QString& path);

    VideoRecorder* m_recorder = nullptr;
    State m_state = State::Idle;
    QString m_patientId;
    QString m_outputPath;   // first file of the current recording
    QList<UsbUtils::Device> m_pendingMirrors;   // chosen at start, begin with the first file
    QList<RecordingMirror*> m_mirrors;
    QString m_lastMirrorStatus;
    int m_lastMirrorLevel = -1;
    void startMirrors(const QString& firstFile);
    void emitMirrorStatus();
    int m_surgeryId = -1;
    int m_recordingId = -1;
    qint64 m_droppedFrames = 0;
    QElapsedTimer m_clock;
};
