#include "HomePage.hpp"
#include "DashboardPage.hpp"
#include "PatientPage.hpp"
#include "../SurgeryDetails/SurgeryDetailsPage.hpp"
#include "../SurgeryRecordPage/SurgeryRecordingPage.hpp"
#include "../Profile/ProfilePage.hpp"
#include "../Settings/SettingsPage.hpp"
#include "../Recording/RecordingPage.hpp"
#include "../PdfViewerPage/PdfViewerPage.hpp"
#include "core/UIScale.hpp"
#include "core/TransferManager.hpp"
#include "../Recording/RecordingSession.hpp"
#include "../Recording/VideoRecorder.hpp"
#include "widgets/TransferDrawer.hpp"
#include "widgets/Toast.hpp"
#include <QProcess>
#include <QMessageBox>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QShortcut>
#include <QTimer>
#include <QPushButton>
#include <QToolButton>
#include <QDebug>
#include <QFile>
#include <QCoreApplication>
#include <QEvent>
#include <QSqlQuery>
#include <QMainWindow>
#include <QScreen>
#include <QGuiApplication>


HomePage::HomePage(QWidget *parent) : ResponsiveWidget(parent) {
    stackedPages = new QStackedWidget;
    dashboardPage = new DashboardPage;
    connect(dashboardPage, &DashboardPage::fullscreenRequested, this, &HomePage::setDashboardFullscreen);
    patientPage = new PatientPage;
    surgeryDetailsPage = new SurgeryDetailsPage;
    profilePage = new ProfilePage;
    settingPage = new SettingsPage;
    recordingPage = new RecordingPage;
    recordingSession = new RecordingSession(this);
    recordingPage->setRecordingSession(recordingSession);
    dashboardPage->setRecordingSession(recordingSession);
    pdfViewerPage = new PdfViewerPage;
    surgeryRecordingPage = nullptr;
    transferManager = new TransferManager(this);
    recordingPage->setTransferManager(transferManager);
    dashboardPage->setTransferManager(transferManager);
    archivePage = new SurgeryRecordingPage(QString(), -1);
    archivePage->setTransferManager(transferManager);
    archivePage->setFileBusyCheck([this](const QString& path) { return recordingSession->isWriting(path); });

    stackedPages->addWidget(dashboardPage);
    stackedPages->addWidget(patientPage);
    stackedPages->addWidget(surgeryDetailsPage);
    stackedPages->addWidget(profilePage);
    stackedPages->addWidget(settingPage);
    stackedPages->addWidget(pdfViewerPage);
    stackedPages->addWidget(archivePage);

    setupMainLayout();
    
    // Apply stylesheet ONCE here - never again
    applyStaticStyles();

    // USB transfers: drawer over the right edge, toggled from the top bar
    transferDrawer = new TransferDrawer(transferManager, this);
    transferDrawer->setToggleWidget(transfersBtn);
    connect(transfersBtn, &QPushButton::clicked, this, [this]() {
        updateDrawerOffset();
        transferDrawer->toggleDrawer();
    });
    connect(transferManager, &TransferManager::activeCountChanged, this, &HomePage::updateTransfersButton);
    connect(transferManager, &TransferManager::failuresChanged, this, &HomePage::updateTransfersButton);
    // One message per device, when that device has nothing left to copy (others may still be busy)
    connect(transferManager, &TransferManager::deviceFinished, this,
            [this](const QString& device, int succeeded, int failed, int cancelled) {
        if (failed > 0) {
            Toast::show(this, QString("Copy to %1 finished: %2 copied, %3 failed — see Transfers")
                                  .arg(device).arg(succeeded).arg(failed), 7000, "#c40000");
        } else if (succeeded > 0) {
            QString text = QString("Copy to %1 finished: %2 file(s) copied").arg(device).arg(succeeded);
            if (cancelled > 0)
                text += QString(", %1 cancelled").arg(cancelled);
            Toast::show(this, text + ". You can remove it.", 6000);
        }
    });
    updateTransfersButton();

    // Archive: recordings made from the Dashboard's Record button, not tied to a patient
    connect(archivePage, &SurgeryRecordingPage::downloadsQueued,
            this, &HomePage::onDownloadsQueued);
    recBlinkTimer = new QTimer(this);
    recBlinkTimer->setInterval(500);
    connect(recBlinkTimer, &QTimer::timeout, this, [this]() {
        recBlinkOn = !recBlinkOn;
        updateRecIndicator();
    });
    connect(recordingSession, &RecordingSession::stateChanged, this, &HomePage::updateRecIndicator);
    connect(recordingSession, &RecordingSession::mirrorStatusChanged, this, &HomePage::updateRecIndicator);
    // USB copy of a recording: limit reached, finished (safe to remove) or stopped
    connect(recordingSession, &RecordingSession::mirrorNotice, this, [this](const QString& text) {
        Toast::show(this, text, 7000, "#b36b00");
    });
    connect(recordingSession, &RecordingSession::mirrorFinished, this,
            [this](const QString&, bool ok, const QString& message) {
        Toast::show(this, message, ok ? 6000 : 8000, ok ? "#1a7f37" : "#c40000");
    });
    connect(recordingSession, &RecordingSession::recordingStopped, this, [this]() {
        if (recordingSession->isArchive() && stackedPages->currentWidget() == archivePage)
            archivePage->refreshRecordings();   // show what was just recorded
    });
    connect(recIndicator, &QPushButton::clicked, this, &HomePage::showDashboard);
    updateRecIndicator();

    connect(surgeryDetailsPage, &SurgeryDetailsPage::openSurgeryRecordingPage,
            this, &HomePage::showSurgeryRecordingPage);
    connect(patientPage, &PatientPage::viewSurgeryDetailsRequested,
            this, &HomePage::openSurgeryDetails);
    connect(surgeryDetailsPage, &SurgeryDetailsPage::goBackRequested,
            this, [=]() { stackedPages->setCurrentWidget(patientPage); });
    connect(recordingPage, &RecordingPage::goBackToRecordingPage,
            this, [=]() {
                layoutSwitcher->setCurrentIndex(0);
                stackedPages->setCurrentWidget(surgeryDetailsPage);
                topBar->show();
                sidebar->show();
    });

    connect(settingPage, &SettingsPage::videoInputChanged, this, [this](const QString &) {
        reconfigureVideoInput();
    });
        
    // In constructor or where you connect settings signals:
    connect(settingPage, &SettingsPage::videoLabelVisibilityChanged, this, [this](bool visible) {
    if (dashboardPage) {
        dashboardPage->sharedGLWidget()->setShowLabel(visible);
    }
    });

    connect(settingPage, &SettingsPage::logoChanged, this, [=](const QString &newLogoPath) {
        updateLogo(newLogoPath);
    });
}

void HomePage::applyStaticStyles() {
    // ONLY style sidebar and header - nothing related to video/dashboard
    QString style = R"(
        #TopBar {
            background: #ffffff;
            border-bottom: 1px solid #ced0d5;
        }

        #SideBar {
            background: #ffffff;
            border-right: 1px solid #ced0d5;
        }

        QToolButton#SideBarButton {
            border-radius: 14px;
            border: 1px solid black;
            font-weight: 600;
        }

        QToolButton#SideBarButton:pressed,
        QToolButton#SideBarButton[active="true"] {
            background: #c40000;
            color: #ffffff;
            border-left: 4px solid #ffffff;
        }
    )";

    setStyleSheet(style);
    m_stylesApplied = true;
}

void HomePage::updateScaling() {
    // Only update WIDGET SIZES here - NO setStyleSheet()!
    if (width() < 100 || height() < 100) return;

    // Sidebar: 6% of width, min 80, max 150
    int sidebarW = percentWidth(6, 80, 150);
    sidebar->setFixedWidth(sidebarW);

    // Top bar margins
    int topMarginH = percentWidth(1, 10, 20);
    int topMarginV = percentHeight(1, 8, 15);
    if (auto* layout = qobject_cast<QHBoxLayout*>(topBar->layout())) {
        layout->setContentsMargins(topMarginH, topMarginV, topMarginH, topMarginV);
    }

    // Button size: 55% of sidebar width, with bounds
    int btnSize = qBound(45, sidebarW * 55 / 100, 80);
    int iconSize = btnSize * 60 / 100;

    for (QToolButton* btn : sidebarButtons) {
        btn->setFixedSize(btnSize, btnSize);
        btn->setIconSize(QSize(iconSize, iconSize));
    }

    // Update logo size
    updateLogo();

    if (transferDrawer) {
        updateTransfersButton();
        updateDrawerOffset();
    }
}

void HomePage::setupMainLayout() {
    QVBoxLayout *mainLayout = new QVBoxLayout;
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Top bar
    topBar = new QWidget(this);
    topBar->setObjectName("TopBar");
    QHBoxLayout *topLayout = new QHBoxLayout(topBar);
    topLayout->setContentsMargins(15, 10, 15, 10);
    
    logoLabel = new QLabel;
    logoLabel->setObjectName("LogoLabel");
    updateLogo();
    topLayout->addWidget(logoLabel);
    topLayout->addStretch();
    transfersBtn = new QPushButton;
    transfersBtn->setObjectName("TransfersButton");
    transfersBtn->setCursor(Qt::PointingHandCursor);
    transfersBtn->setIcon(QIcon(":/assets/icons/usb-transfer.svg"));
    transfersBadge = new QLabel(transfersBtn);
    transfersBadge->setAlignment(Qt::AlignCenter);
    transfersBadge->setAttribute(Qt::WA_TransparentForMouseEvents);
    transfersBadge->hide();
    recIndicator = new QPushButton;
    recIndicator->setObjectName("RecIndicator");
    recIndicator->setCursor(Qt::PointingHandCursor);
    recIndicator->setToolTip("An Archive recording is running. Tap to go to the Dashboard to stop it.");
    recIndicator->hide();
    topLayout->addWidget(recIndicator);
    topLayout->addSpacing(12);
    topLayout->addWidget(transfersBtn);
    mainLayout->addWidget(topBar);

    // Sidebar
    sidebar = new QWidget(this);
    sidebar->setObjectName("SideBar");
    sidebar->setFixedWidth(120);
    
    QVBoxLayout *sidebarLayout = new QVBoxLayout(sidebar);
    sidebarLayout->setAlignment(Qt::AlignTop);

    topWidget = new QWidget(sidebar);
    QVBoxLayout *topbarLayout = new QVBoxLayout(topWidget);
    topbarLayout->setAlignment(Qt::AlignTop);

    bottomWidget = new QWidget(sidebar);
    QVBoxLayout *bottombarLayout = new QVBoxLayout(bottomWidget);
    bottombarLayout->setAlignment(Qt::AlignBottom);

    struct Item { QString icon; QString tooltip; void (HomePage::*handler)(); };
    QList<Item> items = {
        {":assets/icons/apps.png", "Dashboard", &HomePage::showDashboard},
        {":assets/icons/person-simple.png", "Patients", &HomePage::showPatients},
        {":assets/icons/archive.svg", "Archive", &HomePage::showArchive}
    };
    
    for (auto &item : items) {
        QToolButton *btn = new QToolButton;
        btn->setIcon(QIcon(item.icon));
        btn->setObjectName("SideBarButton");
        btn->setToolTip(item.tooltip);
        connect(btn, &QToolButton::clicked, this, item.handler);
        topbarLayout->addWidget(btn);
        sidebarButtons.append(btn);
    }

    QList<Item> bottomItems = {
        {":assets/icons/rotate-right.png", "Restart", &HomePage::restart},
        {":assets/icons/power.png", "Shut Down", &HomePage::shutDown},
        {":assets/icons/settings.png", "Settings", &HomePage::showSettings}
    };
    
    for (auto &item : bottomItems) {
        QToolButton *btn = new QToolButton;
        btn->setIcon(QIcon(item.icon));
        btn->setObjectName("SideBarButton");
        btn->setToolTip(item.tooltip);
        connect(btn, &QToolButton::clicked, this, item.handler);
        bottombarLayout->addWidget(btn);
        sidebarButtons.append(btn);
    }

    // Body layout
    QHBoxLayout *bodyLayout = new QHBoxLayout;
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);
    sidebarLayout->addWidget(topWidget);
    sidebarLayout->addStretch();
    sidebarLayout->addWidget(bottomWidget);
    bodyLayout->addWidget(sidebar);
    bodyLayout->addWidget(stackedPages, 1);
    mainLayout->addLayout(bodyLayout, 1);

    QWidget *normalWidget = new QWidget;
    normalWidget->setLayout(mainLayout);

    // Full-screen wrapper
    fullScreenWrapper = new QWidget;
    fullScreenWrapper->setStyleSheet("background-color: black;");

    layoutSwitcher = new QStackedWidget(this);
    layoutSwitcher->addWidget(normalWidget);
    layoutSwitcher->addWidget(fullScreenWrapper);

    // Separate page for the dashboard's fullscreen preview (index 2). Keeping it inside this
    // window preserves the preview's OpenGL context; a separate window would destroy it.
    dashboardFullScreenWrapper = new QWidget;
    dashboardFullScreenWrapper->setStyleSheet("background-color: black;");
    auto *dashboardFsLayout = new QVBoxLayout(dashboardFullScreenWrapper);
    dashboardFsLayout->setContentsMargins(0, 0, 0, 0);
    layoutSwitcher->addWidget(dashboardFullScreenWrapper);

    QVBoxLayout *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->addWidget(layoutSwitcher);
    
    setUp();
}

void HomePage::updateLogo(const QString &logoPath) {
    static QPixmap cachedPixmap;
    static QString cachedPath;
    
    QString pathToLoad = logoPath;

    if (logoPath.isEmpty()) {
        QString runtimeLogo = QCoreApplication::applicationDirPath() + "/logo.png";
        if (QFile::exists(runtimeLogo)) {
            pathToLoad = runtimeLogo;
        } else {
            pathToLoad = ":/assets/logo.png";
        }
    }

    // Only reload pixmap if path changed
    if (cachedPath != pathToLoad) {
        if (!cachedPixmap.load(pathToLoad)) {
            cachedPixmap.load(":/assets/logo.png");
        }
        cachedPath = pathToLoad;
    }

    // Scale based on current widget size
    int logoW = percentWidth(10, 150, 400);
    int logoH = percentHeight(5, 40, 100);
    
    logoLabel->setPixmap(cachedPixmap.scaled(logoW, logoH, Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void HomePage::showDashboard() {
    stackedPages->setCurrentWidget(dashboardPage);
}

void HomePage::showPatients() {
    stackedPages->setCurrentWidget(patientPage);
}

void HomePage::showUser() {
    QWidget *window = this->window();
    if (!window) return;

    auto mainWindow = qobject_cast<QMainWindow*>(window);
    if (mainWindow) {
        QMetaObject::invokeMethod(mainWindow, "toggleFullScreen", Qt::QueuedConnection);
    }
}

void HomePage::showArchive() {
    archivePage->refreshRecordings();
    stackedPages->setCurrentWidget(archivePage);
}

void HomePage::showSettings() {
    stackedPages->setCurrentWidget(settingPage);
}

void HomePage::onDownloadsQueued(int count) {
    if (count > 0) {
        // Show the queue once so it is clear where the copy went; a tap anywhere hides it
        updateDrawerOffset();
        transferDrawer->openDrawer();
    } else {
        Toast::show(this, "Those files are already being copied", 3000, "#555555");
    }
}

void HomePage::updateDrawerOffset() {
    const bool topBarShown = topBar->isVisible();
    transferDrawer->setTopOffset(topBarShown ? topBar->mapTo(this, QPoint(0, topBar->height())).y() : 0);
}

void HomePage::updateTransfersButton() {
    const int active = transferManager->activeCount();
    const bool failed = transferManager->hasUnseenFailures();

    // Icon-only square button: navy when idle, blue while copying, red after a failure
    const int size = UIScale::scaled(104, 44, 104, this);
    const int iconSize = size * 55 / 100;
    transfersBtn->setFixedSize(size, size);
    transfersBtn->setIconSize(QSize(iconSize, iconSize));

    QString background = "#003366";
    QString hover = "#004488";
    QString tip = "USB transfers";
    if (active > 0) {
        background = "#0055aa";
        hover = "#0066cc";
        tip = QString("USB transfers: copying %1 file(s)").arg(active);
    } else if (failed) {
        background = "#c40000";
        hover = "#e00000";
        tip = "USB transfers: a copy failed, tap to see why";
    }
    transfersBtn->setToolTip(tip);
    transfersBtn->setStyleSheet(QString("QPushButton#TransfersButton { background: %1; border: none; "
                                        "border-radius: %3px; }"
                                        "QPushButton#TransfersButton:hover { background: %2; }")
                                    .arg(background, hover).arg(size / 4));

    // Count badge on the top-right corner while files are queued or copying
    if (active > 0) {
        const int badge = qMax(18, size * 42 / 100);
        transfersBadge->setText(active > 99 ? "99+" : QString::number(active));
        transfersBadge->setStyleSheet(QString("background: #ff3b30; color: white; font-weight: bold; "
                                              "font-size: %1px; border-radius: %2px; border: 2px solid white;")
                                          .arg(badge * 55 / 100).arg(badge / 2));
        transfersBadge->setFixedSize(qMax(badge, transfersBadge->sizeHint().width()), badge);
        transfersBadge->move(size - transfersBadge->width(), 0);
        transfersBadge->show();
        transfersBadge->raise();
    } else {
        transfersBadge->hide();
    }
}

void HomePage::updateRecIndicator() {
    // Only for Archive recordings: a patient recording has its own full-screen page
    const auto state = recordingSession->state();
    const bool archive = recordingSession->isArchive();
    const bool recording = archive && (state == RecordingSession::State::Starting ||
                                       state == RecordingSession::State::Recording);
    const bool saving = archive && state == RecordingSession::State::Saving;

    if (recording && !recBlinkTimer->isActive()) {
        recBlinkOn = true;
        recBlinkTimer->start();
    } else if (!recording && recBlinkTimer->isActive()) {
        recBlinkTimer->stop();
    }
    // An Archive recording's USB copy can still be finishing after Stop: keep showing that
    const bool mirroring = archive && recordingSession->isMirroring();
    if (!recording && !saving && !mirroring) {
        recIndicator->hide();
        return;
    }

    const qint64 secs = recordingSession->elapsedSeconds();
    if (!recording && !saving) {
        recIndicator->setText(recordingSession->mirrorStatus());   // "Finishing copy to …"
        recIndicator->show();
        return;
    }
    QString text = saving ? QString("Saving recording…")
                          : QString("%1 REC  %2:%3:%4")
                                .arg(recBlinkOn ? "●" : "○")
                                .arg(secs / 3600, 2, 10, QChar('0'))
                                .arg((secs % 3600) / 60, 2, 10, QChar('0'))
                                .arg(secs % 60, 2, 10, QChar('0'));
    // Also saved to USB: say so, and whether that copy keeps up
    int mirrorLevel = 0;
    if (!recordingSession->mirrorStatus(&mirrorLevel).isEmpty())
        text += mirrorLevel == 2 ? "  + USB stopped" : mirrorLevel == 1 ? "  + USB behind" : "  + USB";
    recIndicator->setText(text);
    recIndicator->setToolTip(recordingSession->mirrorStatus().isEmpty()
                                 ? QString("An Archive recording is running. Tap to go to the Dashboard to stop it.")
                                 : recordingSession->mirrorStatus());
    const int fontPx = UIScale::scaled(40, 16, 40, this);
    recIndicator->setStyleSheet(QString("QPushButton#RecIndicator { background: #fff0f0; color: #c40000; "
                                        "border: 2px solid #c40000; border-radius: 8px; font-weight: bold; "
                                        "font-size: %1px; padding: %2px %3px; }")
                                    .arg(fontPx).arg(fontPx / 3).arg(fontPx / 2));
    recIndicator->show();
}

bool HomePage::confirmNoActiveTransfers(const QString& action) {
    if (!recordingSession->isIdle()) {
        const auto reply = QMessageBox::warning(
            this, "Recording in progress",
            QString("A recording is still running.\nStop it first so the file is saved completely.\n\n"
                    "%1 anyway?").arg(action),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (reply != QMessageBox::Yes)
            return false;
    }
    if (recordingSession->isMirroring()) {
        const auto reply = QMessageBox::warning(
            this, "Copying to USB",
            QString("A recording is still being copied to the USB device.\n"
                    "If you %1 now, the copy on the USB device will be incomplete.\n\n%2 anyway?")
                .arg(action.toLower(), action),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (reply != QMessageBox::Yes)
            return false;
    }
    const int active = transferManager->activeCount();
    if (active == 0)
        return true;
    const auto reply = QMessageBox::warning(
        this, "USB copy in progress",
        QString("%1 file(s) are still being copied to the USB stick.\n"
                "If you %2 now, those copies will be incomplete.\n\n%3 anyway?")
            .arg(active).arg(action.toLower(), action),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    return reply == QMessageBox::Yes;
}

void HomePage::restart() {
    QMessageBox::StandardButton reply;
    reply = QMessageBox::question(this,
                                  "Restart Confirmation",
                                  "Are you sure you want to restart the system?",
                                  QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::Yes && confirmNoActiveTransfers("Restart")) {
        QProcess::startDetached("systemctl", QStringList() << "reboot");
    }
}

void HomePage::shutDown() {
    QMessageBox::StandardButton reply;
    reply = QMessageBox::question(this,
                                  "Shutdown Confirmation",
                                  "Are you sure you want to shut down the system?",
                                  QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::Yes && confirmNoActiveTransfers("Shut down")) {
        QProcess::startDetached("systemctl", QStringList() << "poweroff");
    }
}

void HomePage::openSurgeryDetails(const QString &patientId) {
    surgeryDetailsPage->loadPatientData(patientId);
    stackedPages->setCurrentWidget(surgeryDetailsPage);
}

void HomePage::setDashboardFullscreen(bool on) {
    if (!dashboardPage || !dashboardFullScreenWrapper)
        return;

    QLayout *layout = dashboardFullScreenWrapper->layout();
    if (on) {
        layout->addWidget(dashboardPage->detachVideoBox());

        if (!dashboardExitFullscreenBtn) {
            dashboardExitFullscreenBtn = new QPushButton("✕", dashboardFullScreenWrapper);
            dashboardExitFullscreenBtn->setFixedSize(50, 50);
            dashboardExitFullscreenBtn->setCursor(Qt::PointingHandCursor);
            dashboardExitFullscreenBtn->setToolTip("Exit fullscreen (Esc)");
            dashboardExitFullscreenBtn->setStyleSheet(
                "QPushButton { background-color: rgba(0,0,0,0.5); border: none; border-radius: 25px;"
                "              color: white; font-size: 24px; }"
                "QPushButton:hover { background-color: rgba(255,0,0,0.8); }");
            connect(dashboardExitFullscreenBtn, &QPushButton::clicked, dashboardPage, &DashboardPage::toggleFullscreen);
            auto *esc = new QShortcut(QKeySequence(Qt::Key_Escape), dashboardFullScreenWrapper);
            connect(esc, &QShortcut::activated, dashboardPage, &DashboardPage::toggleFullscreen);
        }
        dashboardFullScreenWrapper->installEventFilter(this); // keeps the button in the corner
        dashboardExitFullscreenBtn->show();
        dashboardExitFullscreenBtn->raise();
        // The page gets its final size only after the switch, so place the button then too
        QTimer::singleShot(0, this, [this]() {
            if (dashboardExitFullscreenBtn && dashboardFullScreenWrapper) {
                dashboardExitFullscreenBtn->move(dashboardFullScreenWrapper->width() - 70, 20);
                dashboardExitFullscreenBtn->raise();
            }
        });

        transferDrawer->closeDrawer();
        topBar->hide();
        sidebar->hide();
        layoutSwitcher->setCurrentIndex(2);
    } else {
        layout->removeWidget(dashboardPage->detachVideoBox());
        dashboardPage->reattachVideoBox();
        if (dashboardExitFullscreenBtn)
            dashboardExitFullscreenBtn->hide();

        topBar->show();
        sidebar->show();
        layoutSwitcher->setCurrentIndex(0);
    }
}

// Keeps the fullscreen exit button pinned to the top-right corner
bool HomePage::eventFilter(QObject* watched, QEvent* event) {
    if (watched == dashboardFullScreenWrapper && event->type() == QEvent::Resize && dashboardExitFullscreenBtn) {
        dashboardExitFullscreenBtn->move(dashboardFullScreenWrapper->width() - 70, 20);
        dashboardExitFullscreenBtn->raise();
    }
    return QWidget::eventFilter(watched, event);
}

void HomePage::showRecording(const QString &patientId, int surgeryId) {
    // The recording page takes over the screen; leave the dashboard's fullscreen first
    if (dashboardPage && dashboardPage->isFullscreen())
        dashboardPage->toggleFullscreen();
    transferDrawer->closeDrawer();

    if (!recordingPage->parent()) {
        recordingPage->setParent(fullScreenWrapper);
    }

    recordingPage->loadData(patientId, surgeryId);

    QLayout *layout = fullScreenWrapper->layout();
    if (!layout) {
        layout = new QVBoxLayout(fullScreenWrapper);
        layout->setContentsMargins(0, 0, 0, 0);
    } else {
        QLayoutItem *item;
        while ((item = layout->takeAt(0)) != nullptr) {
            QWidget* w = item->widget();
            if (w && w != recordingPage) {
                w->deleteLater();
            }
            delete item;
        }
    }

    layout->addWidget(recordingPage);
    topBar->hide();
    sidebar->hide();
    layoutSwitcher->setCurrentIndex(1);
}

void HomePage::showSurgeryRecordingPage(const QString &patientId, int surgeryId) {
    if (surgeryRecordingPage) {
        stackedPages->removeWidget(surgeryRecordingPage);
        delete surgeryRecordingPage;
    }
    surgeryRecordingPage = new SurgeryRecordingPage(patientId, surgeryId);
    surgeryRecordingPage->setTransferManager(transferManager);
    surgeryRecordingPage->setFileBusyCheck([this](const QString& path) { return recordingSession->isWriting(path); });
    stackedPages->addWidget(surgeryRecordingPage);
    stackedPages->setCurrentWidget(surgeryRecordingPage);

    connect(surgeryRecordingPage, &SurgeryRecordingPage::goBackRequested, this, [=]() {
        stackedPages->setCurrentWidget(surgeryDetailsPage);
    });

    connect(surgeryRecordingPage, &SurgeryRecordingPage::goToRecordingPage, this, [=]() {
        this->showRecording(patientId, surgeryId);
    });

    connect(surgeryRecordingPage, &SurgeryRecordingPage::openPdfReport,
            this, &HomePage::showPdfReport);
    connect(surgeryRecordingPage, &SurgeryRecordingPage::downloadsQueued,
            this, &HomePage::onDownloadsQueued);
}

void HomePage::showPdfReport(const QString &pdfPath) {
    if (!pdfViewerPage) {
        pdfViewerPage = new PdfViewerPage(this);
        stackedPages->addWidget(pdfViewerPage);
    }

    pdfViewerPage->loadPdf(pdfPath);
    stackedPages->setCurrentWidget(pdfViewerPage);
}

void HomePage::setUp() {
    m_deckLinkDiscovery = make_com_ptr<DeckLinkDeviceDiscovery>(this);
    if (m_deckLinkDiscovery) {
        if (!m_deckLinkDiscovery->enable()) {
            QMessageBox::critical(this, "Missing Drivers", "DeckLink drivers not found.");
        }
    }
}

void HomePage::customEvent(QEvent* event) {
    if (event->type() == kAddDeviceEvent) {
        auto* discoveryEvent = dynamic_cast<DeckLinkDeviceDiscoveryEvent*>(event);
        com_ptr<IDeckLink> decklink(discoveryEvent->deckLink());
        addDevice(decklink);
    }
    else if (event->type() == kRemoveDeviceEvent) {
        auto* discoveryEvent = dynamic_cast<DeckLinkDeviceDiscoveryEvent*>(event);
        if (discoveryEvent)
            removeDevice(discoveryEvent->deckLink());
    }
    else if (event->type() == kVideoFrameArrivedEvent) {
        // Sent only when the input signal is gained or lost
        auto* frameEvent = dynamic_cast<DeckLinkInputFrameArrivedEvent*>(event);
        if (!frameEvent)
            return;
        if (frameEvent->SignalValid())
            qInfo() << "DeckLink: input signal present";
        else
            qWarning() << "DeckLink: input signal lost";
        setInputSignalValid(frameEvent->SignalValid());
    }
    else if (event->type() == kVideoFormatChangedEvent) {
        auto* formatEvent = dynamic_cast<DeckLinkInputFormatChangedEvent*>(event);
        if (!formatEvent)
            return;
        const QString modeText = formatEvent->Width() > 0
            ? QString("%1x%2 @ %3").arg(formatEvent->Width()).arg(formatEvent->Height())
                  .arg(formatEvent->Fps(), 0, 'g', 4)
            : QString();
        if (dashboardPage && dashboardPage->sharedGLWidget())
            dashboardPage->sharedGLWidget()->setModeText(modeText);
        if (recordingPage)
            recordingPage->setModeText(modeText);
    }
}

// Shows/hides the NO SIGNAL warning on every live view
void HomePage::setInputSignalValid(bool valid) {
    if (dashboardPage && dashboardPage->sharedGLWidget())
        dashboardPage->sharedGLWidget()->setSignalValid(valid);
    if (recordingPage)
        recordingPage->setSignalValid(valid);
}

// Initial capture mode. 1080p60 8-bit YUV is the highest mode the capture card's PCIe x1 link
// carries reliably; format detection still follows the source if it sends something else.
static const BMDDisplayMode kCaptureDisplayMode = bmdModeHD1080p6000;

void HomePage::addDevice(com_ptr<IDeckLink>& deckLink) {
    const intptr_t key = (intptr_t)deckLink.get();

    // A device that is re-announced must not leave its previous capture running
    auto existing = m_inputDevices.find(key);
    if (existing != m_inputDevices.end()) {
        if (existing->second) {
            existing->second->stopCapture();
            existing->second->setVideoFrameSink(nullptr);
        }
        if (existing->second == m_selectedDevice) {
            m_selectedDevice = nullptr;
            m_currentDeckLink = nullptr;
        }
        m_inputDevices.erase(existing);
    }

    auto inputDevice = make_com_ptr<DeckLinkInputDevice>(this, deckLink);
    if (!inputDevice->Init())
        return;
    m_inputDevices[key] = inputDevice;

    if (!m_sharedDelegate) {
        m_sharedDelegate = dashboardPage->createSharedDelegate();
        dashboardPage->setSharedDelegate(m_sharedDelegate);
        recordingPage->setSharedDelegate(m_sharedDelegate);

        bool showLabel = true;
        QSqlQuery labelQuery("SELECT show_video_label FROM settings ORDER BY id LIMIT 1");
        if (labelQuery.next())
            showLabel = labelQuery.value(0).toInt() == 1;
        dashboardPage->sharedGLWidget()->setShowLabel(showLabel);
    }

    // Only one input feeds the live view (starting every card into the same preview would
    // interleave sources). Keep the current card unless this one has the selected input and it
    // doesn't, e.g. HDMI selected while capturing from an SDI-only card.
    const QString selectedInput = selectedVideoInput();
    const com_ptr<DeckLinkInputDevice> wanted = deviceForInput(selectedInput);
    if (m_selectedDevice && wanted == m_selectedDevice) {
        qInfo() << "Additional DeckLink device detected; not capturing from it:" << inputDevice->getDeviceName();
        return;
    }
    startCaptureOn(wanted, selectedInput);
}

// "SDI" -> SDI; HDMI and AHD (through an HDMI converter) -> HDMI
QString HomePage::selectedVideoInput() const {
    QSqlQuery q("SELECT video_input FROM settings ORDER BY id LIMIT 1");
    return q.next() ? q.value(0).toString() : QStringLiteral("SDI");
}

static int64_t connectionForInput(const QString& input) {
    return input.compare("SDI", Qt::CaseInsensitive) == 0 ? bmdVideoConnectionSDI : bmdVideoConnectionHDMI;
}

static bool hasInputConnection(com_ptr<DeckLinkInputDevice> device, int64_t connection) {
    if (!device)
        return false;
    com_ptr<IDeckLinkProfileAttributes> attributes;
    device->getDeckLinkInstance()->QueryInterface(IID_IDeckLinkProfileAttributes,
                                                  reinterpret_cast<void**>(attributes.releaseAndGetAddressOf()));
    int64_t connections = 0;
    return attributes && attributes->GetInt(BMDDeckLinkVideoInputConnections, &connections) == S_OK
           && (connections & connection);
}

// The card to capture from for this input: the current one if it has the input, otherwise the
// first card that does; if none has it, the current (or first) card
com_ptr<DeckLinkInputDevice> HomePage::deviceForInput(const QString& input) const {
    const int64_t connection = connectionForInput(input);
    if (hasInputConnection(m_selectedDevice, connection))
        return m_selectedDevice;
    for (const auto& pair : m_inputDevices)
        if (hasInputConnection(pair.second, connection))
            return pair.second;
    if (m_selectedDevice)
        return m_selectedDevice;
    return m_inputDevices.empty() ? com_ptr<DeckLinkInputDevice>() : m_inputDevices.begin()->second;
}

// Captures `input` on `device`, moving the live view and the recorder off the previous card
void HomePage::startCaptureOn(com_ptr<DeckLinkInputDevice> device, const QString& input) {
    if (!device)
        return;
    if (m_selectedDevice && !(m_selectedDevice == device)) {
        m_selectedDevice->stopCapture();
        m_selectedDevice->setVideoFrameSink(nullptr);
    }
    device->stopCapture();

    const int64_t connection = connectionForInput(input);
    com_ptr<IDeckLinkConfiguration> config = device->getDeckLinkConfiguration();
    if (!hasInputConnection(device, connection))
        qWarning() << device->getDeviceName() << "has no" << input << "input and no card found so far has one;"
                   << "capturing its current input";
    else if (!config || config->SetInt(bmdDeckLinkConfigVideoInputConnection, connection) != S_OK)
        qWarning() << "Could not switch" << device->getDeviceName() << "to" << input;
    else
        qInfo() << "Capturing" << input << "from" << device->getDeviceName();

    dashboardPage->sharedGLWidget()->setInputSource(input);
    // Frames go to the recorder straight from the capture thread
    device->setVideoFrameSink(recordingSession->recorder());
    setInputSignalValid(false);
    device->startCapture(kCaptureDisplayMode, m_sharedDelegate.get(), true);
    m_selectedDevice = device;
    m_currentDeckLink = device->getDeckLinkInstance();
}

void HomePage::removeDevice(const com_ptr<IDeckLink>& deckLink) {
    auto it = m_inputDevices.find((intptr_t)deckLink.get());
    if (it == m_inputDevices.end())
        return;

    com_ptr<DeckLinkInputDevice> device = it->second;
    m_inputDevices.erase(it);
    if (device) {
        device->stopCapture();
        device->setVideoFrameSink(nullptr);
    }

    if (!(device == m_selectedDevice))
        return;

    qWarning() << "Selected DeckLink device removed";
    m_selectedDevice = nullptr;
    m_currentDeckLink = nullptr;

    // Fall back to another connected device, if any
    if (!m_inputDevices.empty()) {
        com_ptr<IDeckLink> next = m_inputDevices.begin()->second->getDeckLinkInstance();
        m_inputDevices.erase(m_inputDevices.begin());
        addDevice(next);
    }
}

void HomePage::reconfigureVideoInput() {
    if (m_inputDevices.empty())
        return;
    const QString input = selectedVideoInput();
    startCaptureOn(deviceForInput(input), input);
}

HomePage::~HomePage() {
    // Stop capture before child pages (and the recorder the capture thread feeds) are destroyed
    for (auto& pair : m_inputDevices) {
        if (pair.second) {
            pair.second->stopCapture();
            pair.second->setVideoFrameSink(nullptr);
        }
    }
    m_inputDevices.clear();
}