#include "mainwindow.hpp"
#include "ui/login/LoginPage.hpp"
#include "ui/home/HomePage.hpp"
#include "ui/Recording/RecordingPage.hpp"
#include "widgets/VideoWidget.hpp"
#include "widgets/video_signal_bridge.hpp"
#include <QMessageBox>
#include <QSqlQuery>      // ✅ Needed!
#include <QSqlError>      // ✅ For error handling
#include <QDebug>    
#include <QGuiApplication>
#include <QScreen>


// CuvidDecoderWrapper* sharedDecoder = nullptr;  // ✅ Global shared instance

QString getRtspLinkFromDatabase() {
    QSqlQuery query;
    if (query.exec("SELECT rtsp_link FROM settings LIMIT 1") && query.next()) {
        return query.value(0).toString();
    }
    qWarning() << "❌ Failed to fetch RTSP link:" << query.lastError().text();
    return "";
}



MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    stackedWidget = new QStackedWidget(this);
    setCentralWidget(stackedWidget);
    setWindowTitle("Medical Video System");

    loginPage = new LoginPage();
    homePage = new HomePage();
    recordingPage = new RecordingPage();

    stackedWidget->addWidget(loginPage);
    stackedWidget->addWidget(homePage);
    stackedWidget->addWidget(recordingPage);

    // ✅ Switch to any initial page here
    // stackedWidget->setCurrentWidget(recordingPage);
    stackedWidget->setCurrentWidget(loginPage);

    // ✅ Listen globally for any VideoWidget textureReady
    connect(videoSignalBridgeInstance(), &VideoSignalBridge::textureReadyGlobal,
            this, [=](GLuint texID, QOpenGLContext* ctx, QWindow* win) {
        static bool started = false;
        if (!started) {

        }
    });

    // Optional: hook up login success
    connect(loginPage, &LoginPage::loginSuccessful, this, &MainWindow::showDashboard);
    connect(homePage, &HomePage::logoutClicked, this, &MainWindow::showLoginPage);
    // connect(homePage->toggleButton, &QPushButton::clicked, this, &MainWindow::toggleFullScreen);

}

void MainWindow::showDashboard() {
    stackedWidget->setCurrentWidget(homePage);
}

void MainWindow::showLoginPage() {
    stackedWidget->setCurrentWidget(loginPage);
}

void MainWindow::toggleFullScreen()
{
    if (isFullScreenMode) {
        // Switch to 1920x1080 windowed mode
        // this->showNormal();
        // this->setWindowFlags(Qt::FramelessWindowHint);
        // this->setFixedSize(1920, 1080);

        // // Optional: center the window
        // QRect screenGeometry = QGuiApplication::primaryScreen()->geometry();
        // int x = (screenGeometry.width() - 1920) / 2;
        // int y = (screenGeometry.height() - 1080) / 2;
        // this->move(x, y);

        // this->show(); // Re-apply after changing flags

        // isFullScreenMode = false;
        // qInfo() << "🪟 Switched to windowed mode (1920x1080)";
    } else {
        // Switch to fullscreen
        // this->showFullScreen();
        // isFullScreenMode = true;
        // qInfo() << "🖥️ Switched to fullscreen mode";
    }
}