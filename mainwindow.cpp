#include "mainwindow.hpp"
#include "ui/home/HomePage.hpp"
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
    if (query.exec("SELECT rtsp_link FROM settings ORDER BY id LIMIT 1") && query.next()) {
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

    // No login: the device opens straight on the dashboard. The login page is kept, unused, in
    // ui/login (LoginPage, assets/styles/login.qss) in case it is needed again. To re-enable it,
    // add it to stackedWidget, show it first and connect LoginPage::loginSuccessful to showing
    // homePage (its pre-filled admin/123456 credentials must be replaced before that).
    homePage = new HomePage();
    stackedWidget->addWidget(homePage);
    stackedWidget->setCurrentWidget(homePage);

    // ✅ Listen globally for any VideoWidget textureReady
    connect(videoSignalBridgeInstance(), &VideoSignalBridge::textureReadyGlobal,
            this, [=](GLuint texID, QOpenGLContext* ctx, QWindow* win) {
        static bool started = false;
        if (!started) {

        }
    });

    // connect(homePage->toggleButton, &QPushButton::clicked, this, &MainWindow::toggleFullScreen);

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