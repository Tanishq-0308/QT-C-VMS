#ifndef MAINWINDOW_HPP
#define MAINWINDOW_HPP

#include <QMainWindow>
#include <QStackedWidget>

class HomePage;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

private slots:
    void toggleFullScreen();

private:
    QStackedWidget *stackedWidget;
    HomePage *homePage;
    bool isFullScreenMode = false;
};

#endif // MAINWINDOW_HPP
