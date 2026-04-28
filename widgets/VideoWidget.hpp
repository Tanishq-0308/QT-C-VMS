#pragma once

#include <QOpenGLWidget>
#include <QOpenGLFunctions_3_3_Core>
#include <QTimer>
#include <QOpenGLShaderProgram>

class VideoWidget : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core {
    Q_OBJECT

public:
    explicit VideoWidget(QWidget* parent = nullptr);
    ~VideoWidget();

    GLuint getTextureID() const;

    void saveSnapshot(const QString& filePath);              // 📸 Save snapshot as PNG
    void setCameraInfo(const QString& id, int width, int height);  // 🧾 Set camera label + resolution

    void startRecording(const QString& filePath);
    void stopRecording();
    void toggleRecording(const QString& filePath);
    bool isRecording() const;

    void setRotationAngle(float angle); //rotate
signals:
    void textureReady(GLuint textureID);  // ✅ Emitted when texture is ready

protected:
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int w, int h) override;

private:
    void initShaders();  // 🔧 Compile and link GLSL shaders
    void initQuad();     // 🔧 Set up fullscreen textured quad

    bool recording_;

    GLuint textureID_ = 0;
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    QOpenGLShaderProgram shader_;
    QTimer updateTimer_;
    QTimer fpsTimer_;

    int frameWidth_ = 1920;
    int frameHeight_ = 1088;
    int frameCounter_ = 0;
    int currentFps_ = 0;

    float rotationAngle = 0.0f; //rotate
    int rotationMode_ = 0;  // 0 = 0°, 1 = 90°, 2 = 180°, 3 = 270°

    QString cameraId_ = "Cam 1";
};