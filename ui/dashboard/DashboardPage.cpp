#include "DashboardPage.hpp"
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
    titleLabel->setStyleSheet("color: red; font-weight: bold; font-size: 40px;");
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

    auto *zoomOut = new QPushButton("Zoom Out", zoomBar);
    zoomOut->setObjectName("ZoomButton");
    zoomOut->setAutoRepeat(false);  // We handle repeat manually

    auto *zoomIn = new QPushButton("Zoom In", zoomBar);
    zoomIn->setObjectName("ZoomButton");
    zoomIn->setAutoRepeat(false);

    auto *rotateBtn = new QPushButton("Rotate", zoomBar);
    rotateBtn->setObjectName("ZoomButton");

    auto *fullscreenBtn = new QPushButton("⛶", zoomBar);
    fullscreenBtn->setObjectName("ZoomButton");
    fullscreenBtn->setToolTip("Fullscreen");

    zoomL->addWidget(zoomOut);
    zoomL->addWidget(zoomIn);
    zoomL->addWidget(rotateBtn);
    zoomL->addWidget(fullscreenBtn);
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
    if (!m_isFullscreen)
    {
        // ─── ENTER FULLSCREEN ───
        m_isFullscreen = true;

        // Remove videoBox from normal layout
        m_centerLayout->removeWidget(m_videoBox);

        // Create fullscreen window
        m_fullscreenWindow = new QWidget(nullptr, Qt::Window | Qt::FramelessWindowHint);
        m_fullscreenWindow->setStyleSheet("background-color: black;");
        
        // Get screen size and set window geometry before showing
        QScreen* screen = QGuiApplication::primaryScreen();
        QRect screenGeometry = screen->geometry();
        m_fullscreenWindow->setGeometry(screenGeometry);

        auto *fsLayout = new QVBoxLayout(m_fullscreenWindow);
        fsLayout->setContentsMargins(0, 0, 0, 0);
        fsLayout->addWidget(m_videoBox);

        // Exit button overlay - position before showing
        m_exitBtn = new QPushButton("✕", m_fullscreenWindow);
        m_exitBtn->setFixedSize(50, 50);
        m_exitBtn->setCursor(Qt::PointingHandCursor);
        m_exitBtn->setStyleSheet(
            "QPushButton {"
            "   background-color: rgba(0, 0, 0, 0.5);"
            "   border: none;"
            "   border-radius: 25px;"
            "   color: white;"
            "   font-size: 24px;"
            "}"
            "QPushButton:hover {"
            "   background-color: rgba(255, 0, 0, 0.8);"
            "}"
        );
        m_exitBtn->move(screenGeometry.width() - 70, 20);
        connect(m_exitBtn, &QPushButton::clicked, this, &DashboardPage::toggleFullscreen);

        // Escape key to exit
        QShortcut* esc = new QShortcut(QKeySequence(Qt::Key_Escape), m_fullscreenWindow);
        connect(esc, &QShortcut::activated, this, &DashboardPage::toggleFullscreen);

        // Show fullscreen directly
        m_fullscreenWindow->showFullScreen();
        m_exitBtn->raise();
    }
    else
    {
        // ─── EXIT FULLSCREEN ───
        m_isFullscreen = false;

        // Remove from fullscreen layout first
        if (m_fullscreenWindow && m_fullscreenWindow->layout()) {
            m_fullscreenWindow->layout()->removeWidget(m_videoBox);
        }

        // Return videoBox to normal layout at index 1
        m_videoBox->setParent(this);
        m_centerLayout->insertWidget(1, m_videoBox, 1);

        // Cleanup fullscreen window
        if (m_fullscreenWindow)
        {
            m_fullscreenWindow->hide();
            m_fullscreenWindow->deleteLater();
            m_fullscreenWindow = nullptr;
        }
        m_exitBtn = nullptr;
    }
}

void DashboardPage::setSharedDelegate(const com_ptr<DeckLinkOpenGLDelegate> &delegate)
{
    m_previewView->setSharedDelegate(delegate);
}

com_ptr<DeckLinkOpenGLDelegate> DashboardPage::createSharedDelegate()
{
    return m_previewView->delegate();
}