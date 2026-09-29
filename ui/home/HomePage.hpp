#ifndef HOMEPAGE_HPP
#define HOMEPAGE_HPP

#include "core/ResponsiveWidget.hpp"
#include <QWidget>
#include <QStackedWidget>
#include <QLabel>
#include <QToolButton>
#include <QPushButton>
#include <QVBoxLayout>
#include "decklink/DeckLinkDeviceDiscovery.h"
#include "decklink/DeckLinkInputDevice.h"
#include "decklink/DeckLinkOpenGLWidget.h"

class DashboardPage;
class PatientPage;
class ProfilePage;
class SettingsPage;
class RecordingPage;
class SurgeryDetailsPage;
class SurgeryRecordingPage;
class PdfViewerPage;
class RecordingSession;
class QTimer;

class HomePage : public ResponsiveWidget {
    Q_OBJECT

public:
    explicit HomePage(QWidget *parent = nullptr);
    ~HomePage();

protected:
    void customEvent(QEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;
    void updateScaling() override;

private slots:
    void showDashboard();
    void showPatients();
    void showArchive();
    void showUser();
    void showSettings();
    void restart();
    void shutDown();
    void showRecording(const QString &patientId, int surgeryId);
    void openSurgeryDetails(const QString &patientId);
    void showSurgeryRecordingPage(const QString &patientId, int surgeryId);
    void showPdfReport(const QString &pdfPath);

private:
    void setupMainLayout();
    void setUp();
    void addDevice(com_ptr<IDeckLink>& decklink);
    void removeDevice(const com_ptr<IDeckLink>& decklink);
    void setInputSignalValid(bool valid);
    void setDashboardFullscreen(bool on);
    void reconfigureVideoInput();
    void updateLogo(const QString &logoPath = "");
    void applyStaticStyles();
    void updateRecIndicator();

    // Layout widgets
    QStackedWidget *stackedPages;
    QStackedWidget *layoutSwitcher;
    QWidget *topBar;
    QWidget *sidebar;
    QWidget *topWidget;
    QWidget *bottomWidget;
    QWidget *fullScreenWrapper;
    QWidget *dashboardFullScreenWrapper = nullptr; // dashboard preview, fullscreen in this window
    QPushButton *dashboardExitFullscreenBtn = nullptr;
    QLabel *logoLabel;
    QPushButton *recIndicator = nullptr;  // "● REC" in the top bar while an Archive recording runs
    QTimer *recBlinkTimer = nullptr;
    bool recBlinkOn = true;
    
    // Store buttons for dynamic resizing
    QList<QToolButton*> sidebarButtons;
    
    // Track if styles were applied (only once)
    bool m_stylesApplied = false;

    // Pages
    DashboardPage *dashboardPage;
    PatientPage *patientPage;
    ProfilePage *profilePage;
    SettingsPage *settingPage;
    RecordingPage *recordingPage;
    SurgeryDetailsPage *surgeryDetailsPage;
    SurgeryRecordingPage *surgeryRecordingPage;
    PdfViewerPage *pdfViewerPage;
    SurgeryRecordingPage *archivePage = nullptr;  // media not tied to a patient

    // The one recording at a time: Archive (Dashboard) or patient (RecordingPage)
    RecordingSession *recordingSession = nullptr;

    // DeckLink
    com_ptr<DeckLinkDeviceDiscovery> m_deckLinkDiscovery;
    com_ptr<DeckLinkInputDevice> m_selectedDevice;
    std::map<intptr_t, com_ptr<DeckLinkInputDevice>> m_inputDevices;
    com_ptr<DeckLinkOpenGLDelegate> m_sharedDelegate;
    com_ptr<IDeckLink> m_currentDeckLink;
};

#endif // HOMEPAGE_HPP