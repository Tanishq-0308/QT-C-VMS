#include "RecordingSession.hpp"
#include "VideoRecorder.hpp"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

RecordingSession::RecordingSession(QObject* parent) : QObject(parent) {
    m_recorder = new VideoRecorder(this);
    connect(m_recorder, &VideoRecorder::recordingStarted, this, &RecordingSession::onRecordingStarted);
    connect(m_recorder, &VideoRecorder::recordingStopped, this, &RecordingSession::onRecordingStopped);
    connect(m_recorder, &VideoRecorder::segmentStarted, this, &RecordingSession::onSegmentStarted);
    connect(m_recorder, &VideoRecorder::errorOccurred, this, &RecordingSession::errorOccurred);
    connect(m_recorder, &VideoRecorder::framesDropped, this, [this](qint64 total) {
        m_droppedFrames = total;
        emit framesDropped(total);
    });
}

RecordingSession::~RecordingSession() {
    // The recorder (a child) finalises the file in its own destructor
    m_recorder->stopRecording();
}

QString RecordingSession::mediaDir(const QString& kind, const QString& patientId, int surgeryId) {
    const QString base = QCoreApplication::applicationDirPath() + "/../" + kind;
    if (patientId.isEmpty())
        return QDir::cleanPath(base + "/general");
    return QDir::cleanPath(QString("%1/%2/%3").arg(base, patientId, QString::number(surgeryId)));
}

bool RecordingSession::start(const QString& patientId, int surgeryId, int flipStep, QString* errorMessage) {
    if (m_state != State::Idle || m_recorder->isRecording()) {
        if (errorMessage) {
            *errorMessage = m_state == State::Saving || (m_state == State::Idle && m_recorder->isRecording())
                ? "The previous recording is still being saved."
                : (isArchive() ? "An Archive recording is running. Stop it from the Dashboard first."
                               : "A patient recording is running.");
        }
        return false;
    }

    m_patientId = patientId;
    m_surgeryId = surgeryId;
    m_recordingId = -1;
    m_droppedFrames = 0;

    const QString outputDir = mediaDir("recordings", patientId, surgeryId);
    QDir().mkpath(outputDir);
    // Milliseconds, like snapshots: a stop and a new start within the same second must not
    // reuse (and overwrite) the previous recording's file
    const QString timestamp = QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_zzz");
    QString outputPath = QDir::cleanPath(QString("%1/recording_%2.mp4").arg(outputDir, timestamp));
    for (int n = 2; QFileInfo::exists(outputPath); ++n)
        outputPath = QDir::cleanPath(QString("%1/recording_%2_%3.mp4").arg(outputDir, timestamp).arg(n));
    qDebug() << "🔴 Starting recording to:" << outputPath;

    m_recorder->setFlipStep(flipStep);
    if (!m_recorder->startRecording(outputPath, errorMessage))
        return false;   // nothing was started: no DB row, no state change
    m_outputPath = outputPath;

    // The encoder opens on the recorder thread; onRecordingStarted() confirms it
    setState(State::Starting);
    return true;
}

void RecordingSession::stop() {
    if (m_state != State::Starting && m_state != State::Recording)
        return;
    qDebug() << "⏹️ Stopping recording...";
    setState(State::Saving);
    m_recorder->stopRecording();
}

void RecordingSession::setFlipStep(int flipStep) {
    m_recorder->setFlipStep(flipStep);
}

bool RecordingSession::isWriting(const QString& path) const {
    if (m_state == State::Idle || m_outputPath.isEmpty())
        return false;
    const QFileInfo recording(m_outputPath), asked(path);
    if (recording.absolutePath() != asked.absolutePath())
        return false;
    // Later parts (input format changed mid-recording) are named <base>_part<N>.<ext>
    return asked.fileName() == recording.fileName()
           || asked.fileName().startsWith(recording.completeBaseName() + "_part");
}

qint64 RecordingSession::elapsedSeconds() const {
    return m_state == State::Recording && m_clock.isValid() ? m_clock.elapsed() / 1000 : 0;
}

int RecordingSession::insertRecordingRow(const QString& path) {
    // Archive recordings are stored with no patient and no surgery (NULL)
    QSqlQuery query;
    query.prepare("INSERT INTO recordings (patient_id, surgery_id, file_path) VALUES (?, ?, ?)");
    query.addBindValue(isArchive() ? QVariant(QVariant::String) : QVariant(m_patientId));
    query.addBindValue(isArchive() ? QVariant(QVariant::Int) : QVariant(m_surgeryId));
    query.addBindValue(path);

    if (!query.exec()) {
        qWarning() << "❌ Failed to insert into recordings table:" << query.lastError().text();
        return -1;
    }
    const int id = query.lastInsertId().toInt();
    qDebug() << "✅ Recording saved to DB with id:" << id;
    return id;
}

void RecordingSession::onRecordingStarted(const QString& path) {
    m_recordingId = insertRecordingRow(path);
    m_clock.start();
    // A stop pressed while starting stays a stop
    if (m_state == State::Starting)
        setState(State::Recording);
    emit recordingStarted(path);
}

void RecordingSession::onRecordingStopped(const QString& path, qint64 framesEncoded, qint64 framesDropped) {
    qDebug() << "✅ Recording finalised:" << path << "frames" << framesEncoded << "lost" << framesDropped;
    // A failed start never reached onRecordingStarted()
    const bool wasRecording = m_recordingId != -1 || m_clock.isValid();
    m_clock.invalidate();
    m_recordingId = -1;
    setState(State::Idle);
    emit recordingStopped(path, framesEncoded, framesDropped, wasRecording);
}

void RecordingSession::onSegmentStarted(const QString& path) {
    // Comments stay linked to the first file (currentRecordingId() is unchanged)
    insertRecordingRow(path);
    emit segmentStarted(path);
}

void RecordingSession::setState(State state) {
    if (m_state == state)
        return;
    m_state = state;
    emit stateChanged(state);
}
