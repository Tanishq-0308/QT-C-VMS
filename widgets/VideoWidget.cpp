#include "VideoWidget.hpp"
#include <QDebug>
#include <QPainter>
#include <QImage>
#include <QFont>
#include <vector>
#include "video_signal_bridge.hpp"
#include <cmath>


VideoWidget::VideoWidget(QWidget* parent)
    : QOpenGLWidget(parent),
      textureID_(0),
      vao_(0),
      vbo_(0),
      cameraId_("Cam 1"),
      currentFps_(0),
      frameCounter_(0)
{
    qDebug() << "🛠️ VideoWidget constructor called";

    connect(&updateTimer_, &QTimer::timeout, this, QOverload<>::of(&VideoWidget::update));
    updateTimer_.start(60);  // ~30 FPS

    connect(&fpsTimer_, &QTimer::timeout, this, [this]() {
        currentFps_ = frameCounter_;
        frameCounter_ = 0;
    });
    fpsTimer_.start(1000);  // Update FPS every second
}

VideoWidget::~VideoWidget() {
    qDebug() << "🧹 VideoWidget destructor called";

    makeCurrent();
    if (textureID_) glDeleteTextures(1, &textureID_);
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);

    doneCurrent();
}

void VideoWidget::initializeGL() {
    qDebug() << "🟢 initializeGL(): starting";

    initializeOpenGLFunctions();
    initShaders();
    initQuad();

    frameWidth_ = 3840;
    frameHeight_ = 2160;

    makeCurrent();
    glGenTextures(1, &textureID_);
    glBindTexture(GL_TEXTURE_2D, textureID_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, frameWidth_, frameHeight_, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    glBindTexture(GL_TEXTURE_2D, 0);
    doneCurrent();

    emit textureReady(textureID_);
    // videoSignalBridgeInstance()->emitGlobalReady(textureID_, context(), windowHandle());
    videoSignalBridgeInstance()->emitGlobalTextureReady(textureID_, context(), windowHandle());

    qDebug() << "🚀 Emitted textureReady + global bridge signal";

}

void VideoWidget::resizeGL(int w, int h) {
    glViewport(0, 0, w, h);
    qDebug() << "📐 Resized OpenGL viewport to:" << w << "x" << h;
}

void VideoWidget::paintGL() {
    glClear(GL_COLOR_BUFFER_BIT);

    shader_.bind();
    glBindVertexArray(vao_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, textureID_);
    shader_.setUniformValue("u_texture", 0);

        // 👉 NEW: set rotation uniform
    shader_.setUniformValue("u_rotation", rotationAngle);  //rotate in degrees


    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
    shader_.release();

    frameCounter_++;

    QPainter painter(this);
    painter.setPen(Qt::red);
    painter.setFont(QFont("Arial", 20));
    QString overlay = QString("🎥 %1 | %2x%3")
                      .arg(cameraId_)
                      .arg(frameWidth_)
                      .arg(frameHeight_);
                    //   .arg(currentFps_);
    painter.drawText(10, 30, overlay);
    painter.end();
}

void VideoWidget::initShaders() {
    if (!shader_.addShaderFromSourceCode(QOpenGLShader::Vertex, R"(
        #version 330 core
        layout(location = 0) in vec2 pos;
        layout(location = 1) in vec2 tex;
        out vec2 v_tex;
        void main() {
            gl_Position = vec4(pos, 0.0, 1.0);
            v_tex = tex;
        }
    )")) {
        qWarning() << "❌ Vertex shader compile failed:" << shader_.log();
    }

    if (!shader_.addShaderFromSourceCode(QOpenGLShader::Fragment, R"(
        #version 330 core
        in vec2 v_tex;
        out vec4 fragColor;
        uniform sampler2D u_texture;
        void main() {
            fragColor = texture(u_texture, v_tex);
        }
    )")) {
        qWarning() << "❌ Fragment shader compile failed:" << shader_.log();
    }

    if (!shader_.link()) {
        qWarning() << "❌ Shader link failed:" << shader_.log();
    } else {
        qDebug() << "✅ Shaders compiled and linked";
    }
}

void VideoWidget::initQuad() {
    float vertices[] = {
        -1.0f, -1.0f, 0.0f, 0.0f,
         1.0f, -1.0f, 1.0f, 0.0f,
        -1.0f,  1.0f, 0.0f, 1.0f,
         1.0f,  1.0f, 1.0f, 1.0f,
    };

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    qDebug() << "🧱 Fullscreen quad VAO/VBO initialized";
}





void VideoWidget::saveSnapshot(const QString& filePath) {
    makeCurrent();

    QImage image(frameWidth_, frameHeight_, QImage::Format_RGBA8888);
    glReadPixels(0, 0, frameWidth_, frameHeight_, GL_RGBA, GL_UNSIGNED_BYTE, image.bits());
    image = image.mirrored(false, true);

    if (image.save(filePath, "PNG")) {
        qInfo() << "📸 Snapshot saved to" << filePath;
    } else {
        qWarning() << "❌ Failed to save snapshot to" << filePath;
    }

    doneCurrent();
}

void VideoWidget::setCameraInfo(const QString& id, int width, int height) {
    cameraId_ = id;
    frameWidth_ = width;
    frameHeight_ = height;
}


GLuint VideoWidget::getTextureID() const {
    return textureID_;
}


void VideoWidget::toggleRecording(const QString& filePath) {
    if (recording_) {
        stopRecording();
    } else {
        startRecording(filePath);
    }
}

bool VideoWidget::isRecording() const {
    return recording_;
}


void VideoWidget::startRecording(const QString& filePath) {

}

void VideoWidget::stopRecording() {

}

void VideoWidget::setRotationAngle(float angle) {
    rotationAngle = angle;
    rotationMode_ = static_cast<int>(std::round(angle / 90.0f)) % 4;

    qDebug() << "🌀 Set rotationAngle:" << rotationAngle
             << "→ rotationMode:" << rotationMode_;

    update();  // forces paintGL and CUDA update
}


