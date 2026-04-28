#include "video_signal_bridge.hpp"
#include <QMetaObject>
#include <QDebug>
#include <mutex>

static std::vector<VideoWidget*> widgetList;
static std::mutex widgetMutex;

// ───────────────────────────────────────────────
// 🧠 Global Signal Bridge Singleton Implementation
// ───────────────────────────────────────────────
VideoSignalBridge* VideoSignalBridge::instance() {
    static VideoSignalBridge bridge;
    return &bridge;
}

void VideoSignalBridge::emitGlobalTextureReady(GLuint texID, QOpenGLContext* ctx, QWindow* win) {
    emit textureReadyGlobal(texID, ctx, win);
}

// ───────────────────────────────────────────────
// 🎥 Register VideoWidgets for upload dispatching
// ───────────────────────────────────────────────
void registerVideoWidget(VideoWidget* widget) {
    std::lock_guard<std::mutex> lock(widgetMutex);
    widgetList.push_back(widget);

    // 🪝 Connect per-widget textureReady → global bridge once
    QObject::connect(widget, &VideoWidget::textureReady, VideoSignalBridge::instance(),
                     [=](GLuint texID) {
                         VideoSignalBridge::instance()->emitGlobalTextureReady(
                             texID, widget->context(), widget->windowHandle());
                     });
}

void unregisterVideoWidget(VideoWidget* widget) {
    std::lock_guard<std::mutex> lock(widgetMutex);
    widgetList.erase(std::remove(widgetList.begin(), widgetList.end(), widget), widgetList.end());
}

// ───────────────────────────────────────────────
// 🖼️ Dispatch decoded frames to all registered UIs
// ───────────────────────────────────────────────
void emitSignalToUploadFrameOnGUI(
    uint8_t* yPlane,
    uint8_t* uvPlane,
    int pitch,
    int width,
    int height
) {
    std::lock_guard<std::mutex> lock(widgetMutex);
    for (auto* w : widgetList) {
        if (!w) continue;
        QMetaObject::invokeMethod(w, "uploadFrameToCuda",
            Qt::QueuedConnection,
            Q_ARG(void*, yPlane),
            Q_ARG(void*, uvPlane),
            Q_ARG(int, pitch),
            Q_ARG(int, width),
            Q_ARG(int, height));
    }
}
