#ifndef MAINWINDOW_HPP
#define MAINWINDOW_HPP

#include <QMainWindow>
#include <QStackedWidget>

class LoginPage;
class HomePage;
class RecordingPage;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

private slots:
    void showDashboard();  // Called when login succeeds
    void showLoginPage();
    void toggleFullScreen();

private:
    QStackedWidget *stackedWidget;
    LoginPage *loginPage;
    HomePage *homePage;
    RecordingPage* recordingPage;   
    bool isFullScreenMode = false;
};

#endif // MAINWINDOW_HPP
