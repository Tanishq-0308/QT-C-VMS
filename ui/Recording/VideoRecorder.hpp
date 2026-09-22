#pragma once

#include <QObject>
#include <QString>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

#include "decklink/VideoFrameSink.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/buffer.h>
}

#include <cuda.h>

// Pixel layouts the recorder accepts (values match FrameConvertFormat in cuda/frame_convert.h)
enum class RawPixelFormat : int { UYVY = 0, ARGB = 1, BGRA = 2, Unsupported = -1 };

// One captured frame, independent of the DeckLink SDK (lets tests feed synthetic frames)
struct RawVideoFrame
{
    const uint8_t* data = nullptr;
    int rowBytes = 0;
    int width = 0;
    int height = 0;
    RawPixelFormat format = RawPixelFormat::Unsupported;
    int64_t streamTime = 0;    // capture clock, in timeScale units
    int64_t frameDuration = 0; // in timeScale units
    int64_t timeScale = 0;
    bool hasSignal = true;     // false for DeckLink "no input source" frames
};

// Records the capture stream to H.264 (NVENC) in a fragmented MP4.
//
// Frames come straight from the DeckLink capture thread (onVideoFrame / pushFrame), are copied
// into a bounded buffer pool, uploaded to the GPU once, converted to NV12 by a CUDA kernel and
// encoded on a worker thread. Only start/stop run on the GUI thread.
// Timestamps come from the capture clock, so file duration matches wall-clock time.
// Frames are never dropped silently: every drop is counted and reported via framesDropped().
class VideoRecorder : public QObject, public IVideoFrameSink
{
    Q_OBJECT

public:
    static constexpr int kPoolFrames = 30;                    // capture->encoder buffer (0.5 s at 60 fps)
    static constexpr int kMinPoolFrames = 8;
    static constexpr size_t kMaxPoolBytes = 256u << 20;       // cap for large frames (RAM is limited)
    static constexpr int64_t kTimeScale = 120000;             // exact for 23.976 ... 60 fps
    static constexpr int64_t kMinFreeBytesStart = 2LL << 30;  // refuse to start below 2 GB free
    static constexpr int64_t kMinFreeBytesRecord = 1LL << 30; // stop cleanly below 1 GB free

    explicit VideoRecorder(QObject* parent = nullptr);
    ~VideoRecorder() override;

    // GUI thread, fast (no encoder work). Checks signal, format, folder and disk space, then
    // opens the encoder and file on the worker thread. Returns false with a readable reason if
    // a check fails. On true, exactly one of these follows:
    //   recordingStarted(path)                     - frames are being recorded
    //   errorOccurred(msg) + recordingStopped(...)  - the encoder or file could not be opened
    bool startRecording(const QString& outputPath, QString* errorMessage = nullptr);

    // GUI thread. Returns immediately; the worker drains the pool, finalises the file and then
    // emits recordingStopped().
    void stopRecording();

    // True from a successful startRecording() call until the file has been finalised
    bool isRecording() const { return m_active.load(); }

    // Flip steps as used by the preview: 1 horizontal, 2 both, 3 vertical, other = none
    void setFlipStep(int step) { m_flipStep = step; }

    // IVideoFrameSink: DeckLink capture thread
    void onVideoFrame(IDeckLinkVideoInputFrame* frame) override;

    // Any thread. Copies the frame into the pool (drops and counts it if the pool is full).
    void pushFrame(const RawVideoFrame& frame);

signals:
    // Emitted from the worker thread (queued to receivers in other threads)
    void recordingStarted(const QString& path);
    void recordingStopped(const QString& path, qint64 framesEncoded, qint64 framesDropped);
    void segmentStarted(const QString& path); // new file after an input format change
    void errorOccurred(const QString& message);
    void framesDropped(qint64 totalDropped);

private:
    struct Format
    {
        int width = 0, height = 0, rowBytes = 0;
        RawPixelFormat pixelFormat = RawPixelFormat::Unsupported;
        int64_t frameDuration = 0, timeScale = 0;
        bool sameLayout(const Format& o) const
        {
            return width == o.width && height == o.height && rowBytes == o.rowBytes && pixelFormat == o.pixelFormat;
        }
    };

    struct PoolBuffer
    {
        std::vector<uint8_t> bytes;
        Format format;
        int64_t streamTime = 0;
    };

    bool initializeRecording(QString* errorMessage);
    bool openSegment(const QString& path, const Format& format, QString* errorMessage);
    void closeSegment(bool writeTrailer);
    bool encodeBuffer(const PoolBuffer& buffer, QString* errorMessage);
    bool sendAndWrite(AVFrame* frame, QString* errorMessage);
    void releaseDevice();
    void workerLoop();
    bool housekeeping(QString* errorMessage);
    QString segmentPath(int index) const;

    // Capture-side state
    std::atomic<bool> m_accepting{false};
    std::atomic<int> m_pushesInFlight{0}; // capture-thread copies into the pool still running
    std::atomic<int64_t> m_lastCaptureTime{-1}; // stream time of the previous accepted frame
    std::mutex m_formatMutex;
    Format m_lastFormat;
    std::chrono::steady_clock::time_point m_lastFrameTime;

    // Buffer pool (index-based; a buffer is owned by exactly one side at a time)
    std::mutex m_poolMutex;
    std::condition_variable m_poolCond;
    std::vector<PoolBuffer> m_pool;
    std::deque<int> m_free;
    std::deque<int> m_ready;
    bool m_stopRequested = false;

    // Worker / encoder state (owned by the worker thread while it runs)
    std::thread m_worker;
    std::atomic<bool> m_active{false};
    std::atomic<int> m_flipStep{0};

    QString m_basePath;
    QString m_currentPath;
    int m_segmentIndex = 0;
    Format m_startFormat;
    Format m_segmentFormat;
    int64_t m_segmentBaseTime = -1;
    int64_t m_lastPts = -1;
    int m_syncFd = -1;
    bool m_headerWritten = false;
    std::chrono::steady_clock::time_point m_lastHousekeeping;

    AVBufferRef* m_hwDeviceCtx = nullptr;
    AVBufferRef* m_hwFramesCtx = nullptr;
    AVCodecContext* m_codecCtx = nullptr;
    AVFormatContext* m_formatCtx = nullptr;
    AVStream* m_stream = nullptr;
    AVPacket* m_packet = nullptr;
    AVFrame* m_hwFrame = nullptr;
    CUcontext m_cudaCtx = nullptr;
    CUdeviceptr m_devSrc = 0;
    size_t m_devSrcPitch = 0;
    size_t m_devSrcBytes = 0;

    // Statistics
    std::atomic<qint64> m_framesDropped{0};
    std::atomic<qint64> m_framesEncoded{0};
    qint64 m_lastReportedDrops = 0;
};
