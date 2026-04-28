#pragma once

#include <QWidget>
#include <QBoxLayout>
#include <QPushButton>
#include <QFrame>
#include <QTimer>
#include "decklink/DeckLinkOpenGLWidget.h"

class CameraZoomAPI;

class DashboardPage : public QWidget {
    Q_OBJECT

public:
    explicit DashboardPage(QWidget *parent = nullptr);
    ~DashboardPage();

    void setSharedDelegate(const com_ptr<DeckLinkOpenGLDelegate>& delegate);
    com_ptr<DeckLinkOpenGLDelegate> createSharedDelegate();
    DeckLinkOpenGLWidget* sharedGLWidget() const;

private slots:
    void onRotate();
    void toggleFullscreen();

    // Long press zoom
    void startZoomIn();
    void startZoomOut();
    void stopZoom();
private:
    int m_flipStep = 0;
    DeckLinkOpenGLWidget* m_previewView;
    CameraZoomAPI* zoomAPI = nullptr;

    // Fullscreen
    bool m_isFullscreen = false;
    QFrame* m_videoBox = nullptr;
    QVBoxLayout* m_centerLayout = nullptr;
    QWidget* m_fullscreenWindow = nullptr;
    QPushButton* m_exitBtn = nullptr;

    // Long press zoom
    QTimer* m_zoomTimer = nullptr;
    QString m_currentZoomDirection;
};