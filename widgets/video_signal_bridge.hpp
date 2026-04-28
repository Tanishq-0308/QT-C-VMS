#pragma once

#include <QObject>
#include <vector>
#include <QOpenGLContext>
#include <QWindow>
#include "VideoWidget.hpp"

class VideoSignalBridge : public QObject {
    Q_OBJECT
public:
    static VideoSignalBridge* instance();

    // 🔁 Emit global texture ready signal
    void emitGlobalTextureReady(GLuint texID, QOpenGLContext* ctx, QWindow* win);

signals:
    void textureReadyGlobal(GLuint texID, QOpenGLContext* ctx, QWindow* win);
};

// 🌐 Global widget registration (for frame delivery)
void registerVideoWidget(VideoWidget* widget);
void unregisterVideoWidget(VideoWidget* widget);

// 📤 Deliver decoded frame to all registered widgets
void emitSignalToUploadFrameOnGUI(
    uint8_t* yPlane,
    uint8_t* uvPlane,
    int pitch,
    int width,
    int height
);

// ✅ Singleton accessor
inline VideoSignalBridge* videoSignalBridgeInstance() {
    return VideoSignalBridge::instance();
}
