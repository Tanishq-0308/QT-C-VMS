#include "DashboardPage.hpp"
#include "core/UIScale.hpp"
#include "ui/Recording/RecordingSession.hpp"
#include "widgets/Toast.hpp"
#include <QIcon>
#include <QMessageBox>
#include "../widgets/VideoWidget.hpp"
#include "../widgets/video_signal_bridge.hpp"
#include "../widgets/CameraZoomAPI.hpp"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QToolButton>
#include <QFrame>
#include <QPushButton>
#include <QApplication>
#include <QFile>
#include <QSpacerItem>
#include <QOpenGLContext>
#include <QShortcut>
#include <QTimer>
#include <QScreen>
#include <QGuiApplication>
#include <QDebug>

static QToolButton *makeSidebarButton(const QString &iconPath,
                                      const QString &fallbackText,
                                      QWidget *parent)
{
    QToolButton *btn = new QToolButton(parent);
    btn->setObjectName("SideBarButton");
    QIcon ico(iconPath);
    if (!ico.isNull())
    {
        btn->setIcon(ico);
        btn->setIconSize(QSize(24, 24));
        btn->setToolButtonStyle(Qt::ToolButtonIconOnly);
    }
    else
    {
        btn->setText(fallbackText);
        btn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    }
    btn->setCursor(Qt::PointingHandCursor);
    return btn;
}

DashboardPage::DashboardPage(QWidget *parent) : QWidget(parent)
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    auto *bodyH = new QHBoxLayout();
    bodyH->setContentsMargins(0, 0, 0, 0);

    m_centerLayout = new QVBoxLayout();
    auto *titleRow = new QWidget();
    auto *titleLayout = new QHBoxLayout(titleRow);

    // Left line
    QFrame *leftLine = new QFrame;
    leftLine->setFrameShape(QFrame::HLine);
    leftLine->setFrameShadow(QFrame::Sunken);
    leftLine->setStyleSheet("color: red; background-color: red;");
    leftLine->setFixedHeight(3);
    titleLayout->addWidget(leftLine);

    // Title label
    QLabel *titleLabel = new QLabel("Dashboard");
    titleLabel->setStyleSheet(QString("color: red; font-weight: bold; font-size: %1px;")
                                  .arg(UIScale::pageTitleFontSize()));
    titleLabel->setAlignment(Qt::AlignCenter);
    titleLayout->addWidget(titleLabel);

    // Right line
    QFrame *rightLine = new QFrame;
    rightLine->setFrameShape(QFrame::HLine);
    rightLine->setFrameShadow(QFrame::Sunken);
    rightLine->setStyleSheet("color: red; background-color: red;");
    rightLine->setFixedHeight(3);
    titleLayout->addWidget(rightLine);

    titleLayout->setStretch(0, 1);
    titleLayout->setStretch(1, 0);
    titleLayout->setStretch(2, 1);
    m_centerLayout->addWidget(titleRow);  // Index 0

    // ─── Video Box ───────────────────────────────
    m_videoBox = new QFrame();
    m_videoBox->setObjectName("VideoWidget");
    m_videoBox->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_videoBox->setMinimumSize(640, 360);
    m_videoBox->setMaximumSize(3840, 2160);
    m_videoBox->resize(3572, 1844);

    auto *videoLayout = new QVBoxLayout(m_videoBox);
    videoLayout->setContentsMargins(0, 0, 0, 0);
    videoLayout->setSpacing(0);

    m_previewView = new DeckLinkOpenGLWidget(m_videoBox);
    m_previewView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    videoLayout->addWidget(m_previewView);

    m_centerLayout->addWidget(m_videoBox, 1);  // Index 1

    // ─── Zoom Controls ───────────────────────────
    QWidget *zoomBar = new QWidget(this);
    zoomBar->setObjectName("BottomControls");
    auto *zoomL = new QHBoxLayout(zoomBar);
    zoomL->addStretch();

    // Icon-only controls; the tooltip names each one
    auto makeIconButton = [zoomBar](const QString &icon, const QString &tip) {
        auto *btn = new QPushButton(zoomBar);
        btn->setObjectName("ZoomButton");
        btn->setIcon(QIcon(icon));
        btn->setIconSize(QSize(UIScale::scaled(64, 28, 64), UIScale::scaled(64, 28, 64)));
        btn->setToolTip(tip);
        btn->setCursor(Qt::PointingHandCursor);
        return btn;
    };

    auto *zoomOut = makeIconButton(":/assets/icons/zoom-out.svg", "Zoom out (hold)");
    zoomOut->setAutoRepeat(false);  // We handle repeat manually

    auto *zoomIn = makeIconButton(":/assets/icons/zoom-in.svg", "Zoom in (hold)");
    zoomIn->setAutoRepeat(false);

    auto *rotateBtn = makeIconButton(":/assets/icons/rotate.svg", "Rotate");
    auto *fullscreenBtn = makeIconButton(":/assets/icons/fullscreen.svg", "Fullscreen");
    m_recordBtn = makeIconButton(":/assets/icons/record.svg", "Start recording to the Archive");

    zoomL->addWidget(zoomOut);
    zoomL->addWidget(zoomIn);
    zoomL->addWidget(rotateBtn);
    zoomL->addWidget(fullscreenBtn);
    zoomL->addWidget(m_recordBtn);
    zoomL->addStretch();
    m_centerLayout->addWidget(zoomBar, 0, Qt::AlignCenter);  // Index 2

    bodyH->addLayout(m_centerLayout, 1);
    mainLayout->addLayout(bodyH);

    // ─── Zoom API ───────────────────────────────
    zoomAPI = new CameraZoomAPI(this);
    
    // ─── Long Press Zoom Timer ───────────────────
    m_zoomTimer = new QTimer(this);
    m_zoomTimer->setInterval(100);  // Call every 100ms while holding
    connect(m_zoomTimer, &QTimer::timeout, this, [this]() {
        if (!m_currentZoomDirection.isEmpty() && zoomAPI) {
            // qDebug() << "Timer tick:" << m_currentZoomDirection;
            zoomAPI->moveCamera(m_currentZoomDirection, "ESP32");
        }
    });
    
    // ─── Zoom In: Press & Release ───────────────
    connect(zoomIn, &QPushButton::pressed, this, &DashboardPage::startZoomIn);
    connect(zoomIn, &QPushButton::released, this, &DashboardPage::stopZoom);
    
    // ─── Zoom Out: Press & Release ──────────────
    connect(zoomOut, &QPushButton::pressed, this, &DashboardPage::startZoomOut);
    connect(zoomOut, &QPushButton::released, this, &DashboardPage::stopZoom);
    
    connect(rotateBtn, &QPushButton::clicked, this, &DashboardPage::onRotate);
    connect(fullscreenBtn, &QPushButton::clicked, this, &DashboardPage::toggleFullscreen);
    connect(m_recordBtn, &QPushButton::clicked, this, &DashboardPage::onRecordClicked);

    // Blinks the Record button's dot while recording (the REC timer is in HomePage's top bar)
    m_blinkTimer = new QTimer(this);
    m_blinkTimer->setInterval(500);
    connect(m_blinkTimer, &QTimer::timeout, this, [this]() {
        m_blinkOn = !m_blinkOn;
        updateRecordingUi();
    });
}

void DashboardPage::setRecordingSession(RecordingSession *session)
{
    m_session = session;
    connect(session, &RecordingSession::stateChanged, this, &DashboardPage::updateRecordingUi);
    connect(session, &RecordingSession::errorOccurred, this, [this](const QString &message) {
        if (m_session->isArchive())
            QMessageBox::critical(this, "Recording problem", message);
    });
    connect(session, &RecordingSession::recordingStopped, this,
            [this](const QString &, qint64, qint64 dropped, bool wasRecording) {
        if (!m_session->isArchive() || !wasRecording)
            return;
        if (dropped > 0)
            Toast::show(window(), QString("Recording saved to the Archive, %1 frames were lost").arg(dropped),
                        5000, "#c40000");
        else
            Toast::show(window(), "Recording saved to the Archive");
    });
    updateRecordingUi();
}

void DashboardPage::onRecordClicked()
{
    if (!m_session)
        return;
    switch (m_session->state()) {
    case RecordingSession::State::Idle: {
        QString error;
        if (!m_session->start(QString(), -1, m_flipStep, &error))
            QMessageBox::critical(this, "Recording could not start", error);
        break;
    }
    case RecordingSession::State::Starting:
    case RecordingSession::State::Recording:
        if (m_session->isArchive())
            m_session->stop();
        break;
    case RecordingSession::State::Saving:
        Toast::show(window(), "The previous recording is still being saved…", 3000, "#555555");
        break;
    }
}

void DashboardPage::updateRecordingUi()
{
    if (!m_recordBtn)
        return;
    const auto state = m_session ? m_session->state() : RecordingSession::State::Idle;
    const bool mine = m_session && m_session->isArchive();
    const bool recording = mine && (state == RecordingSession::State::Starting ||
                                    state == RecordingSession::State::Recording);
    const bool saving = mine && state == RecordingSession::State::Saving;

    if (recording && !m_blinkTimer->isActive()) {
        m_blinkOn = true;
        m_blinkTimer->start();
    } else if (!recording && m_blinkTimer->isActive()) {
        m_blinkTimer->stop();
        m_blinkOn = true;
    }

    // Blinking red dot while recording; the button then stops the recording
    m_recordBtn->setIcon(QIcon(recording && !m_blinkOn ? ":/assets/icons/record-blink.svg"
                                                       : ":/assets/icons/record.svg"));
    m_recordBtn->setToolTip(recording ? "Stop recording" : "Start recording to the Archive");
    m_recordBtn->setEnabled(!saving);
}

DeckLinkOpenGLWidget *DashboardPage::sharedGLWidget() const
{
    return m_previewView;
}

DashboardPage::~DashboardPage()
{
    if (m_zoomTimer) {
        m_zoomTimer->stop();
    }
}

void DashboardPage::onRotate()
{
    m_flipStep = (m_flipStep + 1) % 5;

    if (m_previewView)
        m_previewView->setFlipStep(m_flipStep);
    // Keep an Archive recording oriented like the preview
    if (m_session && m_session->isArchive() && !m_session->isIdle())
        m_session->setFlipStep(m_flipStep);
}

// ─── Long Press Zoom Functions ───────────────────

void DashboardPage::startZoomIn()
{
    m_currentZoomDirection = "zoom_in";
    // Immediate first call
    if (zoomAPI) {
        zoomAPI->moveCamera("zoom_in", "ESP32");
    }
    // Start repeated calls
    m_zoomTimer->start();
}

void DashboardPage::startZoomOut()
{
    m_currentZoomDirection = "zoom_out";
    // Immediate first call
    if (zoomAPI) {
        zoomAPI->moveCamera("zoom_out", "ESP32");
    }
    // Start repeated calls
    m_zoomTimer->start();
}

void DashboardPage::stopZoom()
{
    qDebug() << "Zoom stop";
    m_zoomTimer->stop();
    m_currentZoomDirection.clear();
}

// ─── Fullscreen Functions ───────────────────────

void DashboardPage::toggleFullscreen()
{
    m_isFullscreen = !m_isFullscreen;
    emit fullscreenRequested(m_isFullscreen);
}

QFrame* DashboardPage::detachVideoBox()
{
    m_centerLayout->removeWidget(m_videoBox);
    // dashboard.qss gives #VideoWidget a 20px side margin for the normal layout; fullscreen
    // must use the whole screen, so the margin is dropped while detached.
    m_videoBox->setStyleSheet("#VideoWidget { margin: 0; border: none; }");
    return m_videoBox;
}

void DashboardPage::reattachVideoBox()
{
    m_videoBox->setStyleSheet(QString()); // back to the stylesheet's normal margins
    m_videoBox->setParent(this);
    m_centerLayout->insertWidget(1, m_videoBox, 1); // back below the title row
    m_videoBox->show();
}

void DashboardPage::setSharedDelegate(const com_ptr<DeckLinkOpenGLDelegate> &delegate)
{
    m_previewView->setSharedDelegate(delegate);
}

com_ptr<DeckLinkOpenGLDelegate> DashboardPage::createSharedDelegate()
{
    return m_previewView->delegate();
}