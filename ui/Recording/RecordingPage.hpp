#pragma once

#include <QWidget>
#include <QString>
#include <QTimer>  // ✅ Required for frame timer
#include "decklink/DeckLinkOpenGLWidget.h"
#include "RecordingSession.hpp"
class TransferManager;
#include <QElapsedTimer>
#include <QLabel>

class VideoWidget;
class CuvidDecoderWrapper;
class QPushButton;
class CameraZoomAPI;

namespace Ui {
class RecordingPage;
}

class RecordingPage : public QWidget {
    Q_OBJECT

public:
    explicit RecordingPage(QWidget *parent = nullptr);
    ~RecordingPage();

    // Set the shared video delegate (DeckLink frame source)
    void setSharedDelegate(const com_ptr<DeckLinkOpenGLDelegate>& delegate);

    // Load metadata
    void loadData(const QString& patientId, int surgeryId);

    // Show toast-like messages
    void showToast(const QString& message, int durationMs = 2000);

    // The app's shared recording (HomePage owns it; the Dashboard uses it for the Archive)
    void setRecordingSession(RecordingSession* session);
    // Asked before recording: is a USB device busy with a download
    void setTransferManager(TransferManager* manager) { m_transferManager = manager; }

    // Live input state for the preview overlay
    void setSignalValid(bool valid);
    void setModeText(const QString& modeText);

        // DeckLinkOpenGLWidget* sharedGLWidget() const;

signals:
    void goBackToRecordingPage();

private slots:
    void onSnapshot();            // 📸 Capture and save a snapshot
    void onToggleRecording();     // ⏺️ Start/stop recording
    void onAddComment();          // 💬 Add comment to video
    void onExit();                // ❌ Exit recording page
    void onRotate();              // 🔄 Rotate view
    void onRecordingStarted(const QString& path);
    void onRecorderError(const QString& message);
    void onRecordingStopped(const QString& path, qint64 framesEncoded, qint64 framesDropped, bool wasRecording);
    void onSegmentStarted(const QString& path);
    void onFramesDropped(qint64 totalDropped);

private:
    // void ensureAppFoldersExist(); // Create folders for patient/surgery recording & snapshot
    void ensureFoldersExist();
    void updateRecordingLabel();
    void resetRecordingUi();
    // Qt Designer UI (optional; currently unused)
    Ui::RecordingPage* ui = nullptr;
    // std::chrono::time_point<std::chrono::steady_clock> m_startTime;

    // Widgets
    VideoWidget* videoWidget = nullptr;
    DeckLinkOpenGLWidget* m_previewView = nullptr;

    QPushButton* snapshotBtn = nullptr;
    QPushButton* recordBtn = nullptr;
    QPushButton* zoomInBtn = nullptr;
    QPushButton* zoomOutBtn = nullptr;
    QPushButton* commentBtn = nullptr;
    QPushButton* exitBtn = nullptr;
    QPushButton* rotateBtn = nullptr;

    // Recording (shared). m_ownsRecording: the session's current recording was started here,
    // so this page shows it; an Archive recording started from the Dashboard is not
    RecordingSession* m_session = nullptr;
    TransferManager* m_transferManager = nullptr;
    QLabel* m_mirrorLabel = nullptr;   // "Also saving to KINGSTON", under the timer
    bool m_ownsRecording = false;
    bool m_recording = false;
    int currentRecordingId = -1;

    // Metadata
    QString m_patientId;
    int m_surgeryId = -1;

    // View settings
    int m_flipStep = 0;

    // Zoom control API
    CameraZoomAPI* zoomAPI = nullptr;

    qint64 m_droppedFrames = 0;

    QLabel* recordingTimeLabel;
    QTimer* uiRecordingTimer;
    int recordingSeconds;

};
