#include "RecordingPage.hpp"
#include "../AddCommentDialog/AddCommentDialog.h"
#include "../widgets/VideoWidget.hpp"
#include "../widgets/video_signal_bridge.hpp"
#include "../widgets/CameraZoomAPI.hpp"

#include <QCoreApplication>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QDateTime>
#include <QLabel>
#include <QFrame>
#include <QDebug>
#include <QDir>
#include <QTimer>
#include <QSqlQuery>
#include <QSqlError>
#include <QMessageBox>

// Ensure necessary folders exist
void RecordingPage::ensureFoldersExist() {
    QString recordingsPath = QCoreApplication::applicationDirPath() + "/recordings";
    if (!QDir(recordingsPath).exists()) {
        qDebug() << "📁 Creating recordings directory:" << recordingsPath;
        QDir().mkpath(recordingsPath);
    } else {
        qDebug() << "✅ Recordings directory exists:" << recordingsPath;
    }
}

// Constructor
RecordingPage::RecordingPage(QWidget* parent)
    : QWidget(parent), m_recording(false), currentRecordingId(-1),
      m_videoRecorder(nullptr), m_recordingTimer(nullptr),
      recordingSeconds(0)
{
    this->setStyleSheet(R"(
        QWidget {
            background-color: #121212;
            color: white;
        }
        QFrame#VideoCard {
            background-color: #1e1e1e;
            border-radius: 16px;
            border: 1px solid #333;
        }
        QPushButton {
            background-color: #2e2e2e;
            border-radius: 12px;
            padding: 12px 18px;
            font-size: 20px;
            color: white;
        }
        QPushButton:hover {
            background-color: #3d3d3d;
        }
        QPushButton:pressed {
            background-color: #444;
        }
    )");

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(24);

    // Video card
    QFrame* card = new QFrame(this);
    card->setObjectName("VideoCard");
    card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    QVBoxLayout* cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(16, 16, 16, 16);
    cardLayout->setSpacing(12);

    m_previewView = new DeckLinkOpenGLWidget(card);
    m_previewView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_previewView->setShowLabel(false);  // Never show overlay on recording page
    cardLayout->addWidget(m_previewView);

    // Recording time label
    recordingTimeLabel = new QLabel("⏱ 00:00:00", this);
    recordingTimeLabel->setStyleSheet("font-size: 24px; color: red;");
    recordingTimeLabel->setAlignment(Qt::AlignCenter);
    cardLayout->addWidget(recordingTimeLabel);

    mainLayout->addWidget(card);

    // Control buttons
    QHBoxLayout* controls = new QHBoxLayout();
    controls->setSpacing(16);

    zoomInBtn = new QPushButton("➕ Zoom In");
    zoomOutBtn = new QPushButton("➖ Zoom Out");
    snapshotBtn = new QPushButton("📸 Snapshot");
    recordBtn = new QPushButton("⏺ Start Recording");
    commentBtn = new QPushButton("💬 Comment");
    exitBtn = new QPushButton("❌ Exit");
    rotateBtn = new QPushButton("🔄 Image Rotation");

    QList<QPushButton*> buttons = {
        zoomOutBtn, zoomInBtn, rotateBtn, snapshotBtn, recordBtn, commentBtn, exitBtn
    };

    for (auto* btn : buttons) {
        btn->setMinimumHeight(48);
        btn->setCursor(Qt::PointingHandCursor);
        controls->addWidget(btn);
    }

    mainLayout->addLayout(controls);

    // Zoom API
    zoomAPI = new CameraZoomAPI(this);
    connect(zoomInBtn, &QPushButton::clicked, this, [=]() {
        zoomAPI->moveCamera("zoom_in", "ESP32");
    });
    connect(zoomOutBtn, &QPushButton::clicked, this, [=]() {
        zoomAPI->moveCamera("zoom_out", "ESP32");
    });

    // Connections
    connect(snapshotBtn, &QPushButton::clicked, this, &RecordingPage::onSnapshot);
    connect(recordBtn, &QPushButton::clicked, this, &RecordingPage::onToggleRecording);
    connect(rotateBtn, &QPushButton::clicked, this, &RecordingPage::onRotate);
    connect(commentBtn, &QPushButton::clicked, this, &RecordingPage::onAddComment);
    connect(exitBtn, &QPushButton::clicked, this, [this]() {
        if (m_recording) {
            QMessageBox::warning(this, "Recording in Progress", "Stop the recording first before exiting.");
        } else {
            emit goBackToRecordingPage();
        }
    });

    // Frame capture timer
    m_recordingTimer = new QTimer(this);
    connect(m_recordingTimer, &QTimer::timeout, this, &RecordingPage::captureAndRecordFrame);

    // UI timer for recording time
    uiRecordingTimer = new QTimer(this);
    connect(uiRecordingTimer, &QTimer::timeout, this, [this]() {
        recordingSeconds++;
        int hours = recordingSeconds / 3600;
        int minutes = (recordingSeconds % 3600) / 60;
        int seconds = recordingSeconds % 60;
        recordingTimeLabel->setText(QString("⏱ %1:%2:%3")
                                    .arg(hours, 2, 10, QChar('0'))
                                    .arg(minutes, 2, 10, QChar('0'))
                                    .arg(seconds, 2, 10, QChar('0')));
    });

    recordingTimeLabel->hide();  // hide initially
}

// DeckLinkOpenGLWidget* RecordingPage::sharedGLWidget() const
// {
//     return m_previewView;
// }


// Destructor
RecordingPage::~RecordingPage() {
    if (m_videoRecorder) {
        m_videoRecorder->stopRecording();
    }
}

// Capture and record frame
void RecordingPage::captureAndRecordFrame() {
    if (!m_recording || !m_videoRecorder)
        return;

    QImage frame = m_previewView->grabFramebuffer();
    if (!frame.isNull()) {
        m_videoRecorder->recordFrame(frame);
    }
}

// Toggle recording
void RecordingPage::onToggleRecording() {
    if (!m_recording) {
        ensureFoldersExist();

        QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
        QString outputDir = QCoreApplication::applicationDirPath() + QString("/../recordings/%1/%2")
                                .arg(m_patientId, QString::number(m_surgeryId));
        QDir().mkpath(outputDir);
        QString outputPath = QString("%1/recording_%2.mp4").arg(outputDir, timestamp);

        qDebug() << "🔴 Starting recording to:" << outputPath;

        if (!m_videoRecorder) {
            m_videoRecorder = new VideoRecorder(this);
            qDebug() << "📦 VideoRecorder instance created";
        }

        m_videoRecorder->startRecording(outputPath);
        m_recordingTimer->start(33); // ~30 FPS
        uiRecordingTimer->start(1000);  // UI timer for HH:MM:SS
        recordingTimeLabel->setText("⏱ 00:00:00");
        recordingTimeLabel->show();
        recordingSeconds = 0;
        recordBtn->setText("⏹ Stop Recording");

        // Insert into DB
        QSqlQuery query;
        query.prepare("INSERT INTO recordings (patient_id, surgery_id, file_path) VALUES (?, ?, ?)");
        query.addBindValue(m_patientId);
        query.addBindValue(m_surgeryId);
        query.addBindValue(outputPath);

        if (!query.exec()) {
            qWarning() << "❌ Failed to insert into recordings table:" << query.lastError().text();
            currentRecordingId = -1;
        } else {
            currentRecordingId = query.lastInsertId().toInt();
            qDebug() << "✅ Recording saved to DB with id:" << currentRecordingId;
        }

        m_recording = true;
    } else {
        qDebug() << "⏹️ Stopping recording...";
        if (m_videoRecorder)
            m_videoRecorder->stopRecording();
        if (m_recordingTimer)
            m_recordingTimer->stop();
        if (uiRecordingTimer)
            uiRecordingTimer->stop();

        recordingTimeLabel->hide();
        recordBtn->setText("⏺ Start Recording");
        m_recording = false;
        currentRecordingId = -1;
    }
}

// Other methods stay the same (onSnapshot, onAddComment, onExit, showToast, onRotate, etc.)



void RecordingPage::onSnapshot()
{
    QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
    QString outputDir = QCoreApplication::applicationDirPath() + QString("/../snapshots/%1/%2")
                            .arg(m_patientId, QString::number(m_surgeryId));
    QDir().mkpath(outputDir);

    QString outputPath = QString("%1/snapshot_%2.png").arg(outputDir, timestamp);
    QString cleanPath = QDir::cleanPath(outputPath);
    QString title = QString("Snapshot at %1").arg(timestamp);

    if (m_previewView && m_previewView->saveSnapshot(outputPath)) {
        qDebug() << "Snapshot saved to:" << outputPath;
                    // Save snapshot record to DB
            QSqlQuery query;
            query.prepare("INSERT INTO snapshots (patient_id, surgery_id, file_path, title) VALUES (?, ?, ?, ?)");
            query.addBindValue(m_patientId);
            query.addBindValue(m_surgeryId);
            query.addBindValue(cleanPath);
            query.addBindValue(title);

            if (!query.exec()) {
                qDebug() << "❌ Failed to insert snapshot path into DB:" << query.lastError().text();
            } else {
                qDebug() << "✅ Snapshot saved to DB:" << title;
                showToast("📸 Snapshot taken successfully!");
            }
    } else {
            qWarning() << "❌ Snapshot :";
            showToast("❌ Failed to take snapshot");
    }
}




// void RecordingPage::onToggleRecording() {
//     if (!m_recording) {
//         ensureFoldersExist();

//         QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
//         QString outputDir = QCoreApplication::applicationDirPath() + QString("/../recordings/%1/%2")
//                                 .arg(m_patientId, QString::number(m_surgeryId));
//         QDir().mkpath(outputDir);

//         QString outputPath = QString("%1/recording_%2.mp4").arg(outputDir, timestamp);

//         // QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss");
//         // QString outputPath = QCoreApplication::applicationDirPath() + "../recordings/recording_" + timestamp + ".mp4";

//         qDebug() << "🔴 Starting recording to:" << outputPath;

//         if (!m_videoRecorder) {
//             m_videoRecorder = new VideoRecorder(this);
//             qDebug() << "📦 VideoRecorder instance created";
//         }

//         m_videoRecorder->startRecording(outputPath);
//         m_recordingTimer->start(33); // ~30 FPS
//         recordBtn->setText("Stop Recording");


//         // Insert recording info into the database
//         QSqlQuery query;
//         query.prepare("INSERT INTO recordings (patient_id, surgery_id, file_path) VALUES (?, ?, ?)");
//         query.addBindValue(m_patientId);
//         query.addBindValue(m_surgeryId);
//         query.addBindValue(outputPath);

//         if (!query.exec()) {
//             qWarning() << "❌ Failed to insert into recordings table:" << query.lastError().text();
//             currentRecordingId = -1;
//         } else {
//             currentRecordingId = query.lastInsertId().toInt();
//             qDebug() << "✅ Recording path saved to DB with id:" << currentRecordingId;
//         }

        
//         m_recording = true;
//     } else {
//         qDebug() << "⏹️ Stopping recording...";
//         if (m_videoRecorder) {
//             m_videoRecorder->stopRecording();
//         }
//         if (m_recordingTimer) {
//             m_recordingTimer->stop();
//         }
//         recordBtn->setText("Start Recording");
//         m_recording = false;
//         currentRecordingId = -1;
//     }
// }

void RecordingPage::loadData(const QString& newPatientId, int newSurgeryId) {
    m_patientId = newPatientId;
    m_surgeryId = newSurgeryId;

    ensureFoldersExist();

    qDebug() << "RecordingPage loaded with Patient ID:" << m_patientId << " and Surgery ID:" << m_surgeryId;
}



void RecordingPage::onAddComment() {
    qDebug() << "💬 Add Comment clicked";

    // 1. Check if recording is ongoing and currentRecordingId is valid
    if (!m_recording || currentRecordingId == -1) {
        QMessageBox::warning(this, "Warning", "Please start recording first to add a comment.");
        return;
    }

    AddCommentDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted) {
        QString comment = dialog.getCommentText();
        if (comment.isEmpty()) {
            qDebug() << "⚠️ Empty comment. Not saving.";
            return;
        }

        // Use currentRecordingId directly to link comment
        QSqlQuery insertQuery;
        insertQuery.prepare("INSERT INTO comments_video (video_id, text) VALUES (?, ?)");
        insertQuery.addBindValue(currentRecordingId);
        insertQuery.addBindValue(comment);

        if (!insertQuery.exec()) {
            qWarning() << "❌ Failed to insert comment:" << insertQuery.lastError().text();
            QMessageBox::critical(this, "Error", "Failed to save comment.");
        } else {
            qDebug() << "✅ Comment added for video ID:" << currentRecordingId;
        }
    }
}


void RecordingPage::onExit() {
    qDebug() << "❌ Exit recording page";
    this->close();  // or emit signal to navigate
}


void RecordingPage::showToast(const QString& message, int durationMs) {
    QLabel* toast = new QLabel(message, this);
    toast->setStyleSheet(
        "background-color: rgba(0, 0, 0, 200);"
        "color: white;"
        "padding: 13px 23px;"
        "border-radius: 13px;"
        "font-size: 18px;"
    );
    toast->setAttribute(Qt::WA_TransparentForMouseEvents);
    toast->setWindowFlags(Qt::FramelessWindowHint | Qt::ToolTip);
    toast->adjustSize();

    int x = (width() - toast->width()) / 2;
    int y = height() - toast->height() - 50;
    toast->move(x, y);
    toast->show();

    QTimer::singleShot(durationMs, toast, &QLabel::deleteLater);
}

void RecordingPage::onRotate() {
    m_flipStep = (m_flipStep + 1) % 5;

    qDebug() << "🔄 Flip step:" << m_flipStep;

    if (m_previewView)
        m_previewView->setFlipStep(m_flipStep);
}

void RecordingPage::setSharedDelegate(const com_ptr<DeckLinkOpenGLDelegate>& delegate)
{
    m_previewView->setSharedDelegate(delegate);
}