// #pragma once

// #include <QObject>
// #include <QImage>
// #include <atomic>

// extern "C" {
// #include <libavformat/avformat.h>
// #include <libavcodec/avcodec.h>
// #include <libswscale/swscale.h>
// }

// class VideoRecorder : public QObject {
//     Q_OBJECT

// public:
//     explicit VideoRecorder(QObject* parent = nullptr);
//     ~VideoRecorder();

//     void startRecording(const QString& outputPath);
//     void stopRecording();
//     void recordFrame(const QImage& image);

//     bool isRecording() const { return m_recording.load(); }

// signals:
//     void recordingStarted();
//     void recordingStopped();
//     void errorOccurred(const QString& message);

// private:
//     bool initializeFFmpeg();
//     void finalizeFFmpeg();
//     void flushEncoder(); 

//     std::atomic<bool> m_recording;
//     std::chrono::steady_clock::time_point m_startTime;

//     QString m_outputPath;
//     int m_width;
//     int m_height;
//     int m_fps;
//     int64_t m_frameCounter;

//     AVFormatContext* m_formatCtx;
//     AVCodecContext* m_codecCtx;
//     AVStream* m_videoStream;
//     SwsContext* m_swsCtx;
// };



#pragma once
#include <QObject>
#include <QImage>
#include <QMutex>
#include <QWaitCondition>
#include <queue>
#include <atomic>
#include <thread>
#include <chrono>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_cuda.h>
#include <libavutil/buffer.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

#include <cuda_runtime.h>

class VideoRecorder : public QObject {
    Q_OBJECT

public:
    explicit VideoRecorder(QObject* parent = nullptr);
    ~VideoRecorder();
    
    void startRecording(const QString& outputPath);
    void stopRecording();
    void recordFrame(const QImage& image);
    bool isRecording() const { return m_recording.load(); }
    
    void setResolution(int width, int height) { m_width = width; m_height = height; }
    void setFrameRate(int fps) { m_fps = fps; }

signals:
    void recordingStarted();
    void recordingStopped();
    void errorOccurred(const QString& message);

private:
    void recordingThreadFunc();
    bool initializeGPU();
    bool initializeFFmpegHardware();
    void finalizeGPU();
    void finalizeFFmpeg();
    void processFrame(const QImage& image);
    void flushEncoder();
    
    std::atomic<bool> m_recording;
    std::thread m_recordingThread;
    
    QString m_outputPath;
    int m_width = 1920;
    int m_height = 1080;
    int m_fps = 20;
    int64_t m_frameCounter;
    
    // FFmpeg contexts
    AVFormatContext* m_formatCtx;
    AVCodecContext* m_codecCtx;
    AVStream* m_videoStream;
    AVBufferRef* m_hwDeviceCtx;
    AVBufferRef* m_hwFramesCtx;
    SwsContext* m_swsCtx;
    
    // CUDA resources
    cudaStream_t m_cudaStream;
    uint8_t* d_yuvBuffer;      // GPU YUV buffer
    size_t m_yuvBufferSize;
    
    // Frame queue
    QMutex m_queueMutex;
    QWaitCondition m_queueCond;
    std::queue<QImage> m_frameQueue;
};