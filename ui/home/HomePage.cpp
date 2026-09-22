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
#include <QProcess>
#include <QMessageBox>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QPixmap>
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
    patientPage = new PatientPage;
    surgeryDetailsPage = new SurgeryDetailsPage;
    profilePage = new ProfilePage;
    settingPage = new SettingsPage;
    recordingPage = new RecordingPage;
    pdfViewerPage = new PdfViewerPage;
    surgeryRecordingPage = nullptr;

    stackedPages->addWidget(dashboardPage);
    stackedPages->addWidget(patientPage);
    stackedPages->addWidget(surgeryDetailsPage);
    stackedPages->addWidget(profilePage);
    stackedPages->addWidget(settingPage);
    stackedPages->addWidget(pdfViewerPage);

    setupMainLayout();
    
    // Apply stylesheet ONCE here - never again
    applyStaticStyles();

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
        {":assets/icons/sign-out-alt.png", "Sign Out", &HomePage::signOut}
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

void HomePage::showSettings() {
    stackedPages->setCurrentWidget(settingPage);
}

void HomePage::signOut() {
    emit logoutClicked();
}

void HomePage::restart() {
    QMessageBox::StandardButton reply;
    reply = QMessageBox::question(this,
                                  "Restart Confirmation",
                                  "Are you sure you want to restart the system?",
                                  QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        QProcess::startDetached("systemctl", QStringList() << "reboot");
    }
}

void HomePage::shutDown() {
    QMessageBox::StandardButton reply;
    reply = QMessageBox::question(this,
                                  "Shutdown Confirmation",
                                  "Are you sure you want to shut down the system?",
                                  QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        QProcess::startDetached("systemctl", QStringList() << "poweroff");
    }
}

void HomePage::openSurgeryDetails(const QString &patientId) {
    surgeryDetailsPage->loadPatientData(patientId);
    stackedPages->setCurrentWidget(surgeryDetailsPage);
}

void HomePage::showRecording(const QString &patientId, int surgeryId) {
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
        auto* frameEvent = dynamic_cast<DeckLinkInputFrameArrivedEvent*>(event);
        if (!frameEvent || !frameEvent->SignalValid())
            return;

        auto* senderDevice = dynamic_cast<DeckLinkInputDevice*>(m_selectedDevice.get());
        if (!senderDevice)
            return;
    }
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

    QString selectedInput = "SDI";
    {
        QSqlQuery q("SELECT video_input FROM settings ORDER BY id LIMIT 1");
        if (q.next())
            selectedInput = q.value(0).toString();
    }

    int64_t inputConnection = (selectedInput.compare("SDI", Qt::CaseInsensitive) == 0)
                                ? bmdVideoConnectionSDI
                                : bmdVideoConnectionHDMI;

    com_ptr<IDeckLinkConfiguration> config;
    deckLink->QueryInterface(IID_IDeckLinkConfiguration, reinterpret_cast<void**>(config.releaseAndGetAddressOf()));
    if (config) {
        if (config->SetInt(bmdDeckLinkConfigVideoInputConnection, inputConnection) != S_OK) {
            qWarning() << "Failed to set DeckLink input connection to" << selectedInput;
        } else {
            qDebug() << "DeckLink input connection set to" << selectedInput;
        }
    }

    m_inputDevices[key] = inputDevice;

    // Only one input feeds the live view. Starting every discovered device into the same
    // preview would interleave different sources and multiply the frame rate.
    if (m_selectedDevice) {
        qWarning() << "Additional DeckLink device detected; not capturing from it:" << inputDevice->getDeviceName();
        return;
    }
    m_currentDeckLink = deckLink;

    if (!m_sharedDelegate) {
        m_sharedDelegate = dashboardPage->createSharedDelegate();
        dashboardPage->setSharedDelegate(m_sharedDelegate);
        recordingPage->setSharedDelegate(m_sharedDelegate);
    }

    // ✅ ADD THIS LINE - Set input source on startup
    dashboardPage->sharedGLWidget()->setInputSource(selectedInput);

    // After setInputSource line, add:
    bool showLabel = true;
    QSqlQuery labelQuery("SELECT show_video_label FROM settings ORDER BY id LIMIT 1");
    if (labelQuery.next()) {
        showLabel = labelQuery.value(0).toInt() == 1;
    }
    dashboardPage->sharedGLWidget()->setShowLabel(showLabel);

    // Frames go to the recorder straight from the capture thread
    inputDevice->setVideoFrameSink(recordingPage->recorder());
    inputDevice->startCapture(kCaptureDisplayMode, m_sharedDelegate.get(), true);
    m_selectedDevice = inputDevice;
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
    if (!m_selectedDevice || !m_currentDeckLink)
        return;

    m_selectedDevice->stopCapture();

    QString selectedInput = "SDI";
    QSqlQuery q("SELECT video_input FROM settings ORDER BY id LIMIT 1");
    if (q.next())
        selectedInput = q.value(0).toString();
    
    qDebug() << selectedInput;
    
    if (dashboardPage)
        dashboardPage->sharedGLWidget()->setInputSource(selectedInput);

    int64_t inputConnection = (selectedInput.compare("SDI", Qt::CaseInsensitive) == 0)
                                ? bmdVideoConnectionSDI
                                : bmdVideoConnectionHDMI;

    com_ptr<IDeckLinkConfiguration> config;
    m_currentDeckLink->QueryInterface(IID_IDeckLinkConfiguration, reinterpret_cast<void**>(config.releaseAndGetAddressOf()));
    if (config)
        config->SetInt(bmdDeckLinkConfigVideoInputConnection, inputConnection);

    m_selectedDevice->startCapture(kCaptureDisplayMode, m_sharedDelegate.get(), true);
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