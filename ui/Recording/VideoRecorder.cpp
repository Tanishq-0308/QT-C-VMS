
#include "VideoRecorder.hpp"

#include "cuda/frame_convert.h"
#include "decklink/com_ptr.h"
#include "diag/DiagProbe.h"

#include <QDebug>
#include <QFileInfo>
#include <QStorageInfo>

#include <cstring>
#include <fcntl.h>
#include <unistd.h>

extern "C" {
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_cuda.h>
#include <libavutil/opt.h>
}

namespace {

QString avError(int err)
{
    char buf[AV_ERROR_MAX_STRING_SIZE] = {0};
    av_strerror(err, buf, sizeof(buf));
    return QString::fromUtf8(buf);
}

RawPixelFormat toRawFormat(BMDPixelFormat format)
{
    switch (format)
    {
    case bmdFormat8BitYUV:  return RawPixelFormat::UYVY;
    case bmdFormat8BitARGB: return RawPixelFormat::ARGB;
    case bmdFormat8BitBGRA: return RawPixelFormat::BGRA;
    default:                return RawPixelFormat::Unsupported;
    }
}

// Scoped push/pop of the encoder's CUDA context on the calling thread
struct CudaContextScope
{
    explicit CudaContextScope(CUcontext ctx) : ok(ctx && cuCtxPushCurrent(ctx) == CUDA_SUCCESS) {}
    ~CudaContextScope() { if (ok) { CUcontext dummy; cuCtxPopCurrent(&dummy); } }
    bool ok;
};

int64_t freeBytes(const QString& path)
{
    QStorageInfo storage(QFileInfo(path).absolutePath());
    return storage.isValid() ? storage.bytesAvailable() : -1;
}

} // namespace

VideoRecorder::VideoRecorder(QObject* parent) : QObject(parent)
{
    av_log_set_level(AV_LOG_ERROR);
}

VideoRecorder::~VideoRecorder()
{
    stopRecording();
    if (m_worker.joinable())
        m_worker.join();
}

/// Capture side (DeckLink thread)

void VideoRecorder::onVideoFrame(IDeckLinkVideoInputFrame* frame)
{
    if (!frame)
        return;

    RawVideoFrame raw;
    raw.width = static_cast<int>(frame->GetWidth());
    raw.height = static_cast<int>(frame->GetHeight());
    raw.rowBytes = static_cast<int>(frame->GetRowBytes());
    raw.format = toRawFormat(frame->GetPixelFormat());
    raw.timeScale = kTimeScale;

    BMDTimeValue streamTime = 0, duration = 0;
    if (frame->GetStreamTime(&streamTime, &duration, kTimeScale) != S_OK)
        return;
    raw.streamTime = streamTime;
    raw.frameDuration = duration;
    raw.hasSignal = (frame->GetFlags() & bmdFrameHasNoInputSource) == 0;

    // Only touch the pixel data while recording; format bookkeeping is always kept current
    if (!m_accepting.load())
    {
        pushFrame(raw);
        return;
    }

    com_ptr<IDeckLinkVideoBuffer> buffer;
    if (frame->QueryInterface(IID_IDeckLinkVideoBuffer, reinterpret_cast<void**>(buffer.releaseAndGetAddressOf())) != S_OK || !buffer)
    {
        m_framesDropped++;
        return;
    }
    if (buffer->StartAccess(bmdBufferAccessRead) != S_OK)
    {
        m_framesDropped++;
        return;
    }

    void* bytes = nullptr;
    if (buffer->GetBytes(&bytes) == S_OK && bytes)
    {
        raw.data = static_cast<const uint8_t*>(bytes);
        pushFrame(raw);
    }
    else
    {
        m_framesDropped++;
    }
    buffer->EndAccess(bmdBufferAccessRead);
}

void VideoRecorder::pushFrame(const RawVideoFrame& frame)
{
    Format format;
    format.width = frame.width;
    format.height = frame.height;
    format.rowBytes = frame.rowBytes;
    format.pixelFormat = frame.format;
    format.frameDuration = frame.frameDuration;
    format.timeScale = frame.timeScale;

    if (frame.hasSignal)
    {
        std::lock_guard<std::mutex> lock(m_formatMutex);
        m_lastFormat = format;
        m_lastFrameTime = std::chrono::steady_clock::now();
    }

    if (!frame.data)
        return;

    // Registered before checking m_accepting so startRecording() can wait for stragglers
    // before it reallocates the pool
    struct InFlight {
        std::atomic<int>& n;
        explicit InFlight(std::atomic<int>& c) : n(c) { ++n; }
        ~InFlight() { --n; }
    } inFlight(m_pushesInFlight);

    if (!m_accepting.load())
        return;

    // Frames the driver never delivered show up as gaps in the capture clock. Counted here, on
    // the capture side, so they never overlap with frames dropped below because the pool is full.
    const int64_t previous = m_lastCaptureTime.exchange(frame.streamTime);
    if (previous >= 0 && frame.frameDuration > 0 && frame.streamTime > previous)
    {
        const int64_t missing = (frame.streamTime - previous + frame.frameDuration / 2) / frame.frameDuration - 1;
        if (missing > 0)
            m_framesDropped += missing;
    }

    int index = -1;
    {
        std::lock_guard<std::mutex> lock(m_poolMutex);
        DIAG_REC_QUEUE(static_cast<int>(m_ready.size()));
        if (!m_free.empty())
        {
            index = m_free.front();
            m_free.pop_front();
        }
    }

    if (index < 0)
    {
        // Encoder is behind: never block the capture thread. Count it; the UI is told.
        m_framesDropped++;
        DIAG_REC_DROP();
        return;
    }

    // The buffer at `index` belongs to this thread until it is queued as ready
    PoolBuffer& buf = m_pool[index];
    const size_t bytes = static_cast<size_t>(frame.rowBytes) * frame.height;
    if (buf.bytes.size() < bytes)
        buf.bytes.resize(bytes);
    std::memcpy(buf.bytes.data(), frame.data, bytes);
    buf.format = format;
    buf.streamTime = frame.streamTime;

    {
        std::lock_guard<std::mutex> lock(m_poolMutex);
        m_ready.push_back(index);
    }
    m_poolCond.notify_one();
}

/// Start / stop (GUI thread)

bool VideoRecorder::startRecording(const QString& outputPath, QString* errorMessage)
{
    // Only fast checks here: this runs on the GUI thread and must not stall the live view.
    // The encoder and file are opened on the worker thread, which emits recordingStarted().
    auto fail = [&](const QString& msg) {
        qWarning() << "[VideoRecorder]" << msg;
        if (errorMessage)
            *errorMessage = msg;
        return false;
    };

    // Must not touch any encoder state while a recording (or its finalisation) is running
    if (m_active.load())
        return fail(tr("A recording is still in progress or being saved."));

    // The previous worker has finished (m_active is false); reap the thread
    if (m_worker.joinable())
        m_worker.join();

    Format format;
    std::chrono::steady_clock::time_point lastFrame;
    {
        std::lock_guard<std::mutex> lock(m_formatMutex);
        format = m_lastFormat;
        lastFrame = m_lastFrameTime;
    }

    if (format.width == 0 || std::chrono::steady_clock::now() - lastFrame > std::chrono::milliseconds(500))
        return fail(tr("No video signal from the capture card. Check the camera connection."));
    if (format.pixelFormat == RawPixelFormat::Unsupported)
        return fail(tr("The input pixel format is not supported for recording (8-bit YUV or RGB required)."));
    if ((format.width & 1) || (format.height & 1))
        return fail(tr("Unsupported input resolution %1x%2.").arg(format.width).arg(format.height));

    const QFileInfo dirInfo(QFileInfo(outputPath).absolutePath());
    if (!dirInfo.isDir() || !dirInfo.isWritable())
        return fail(tr("The recording folder is missing or not writable:\n%1").arg(dirInfo.absoluteFilePath()));

    const int64_t free = freeBytes(outputPath);
    if (free >= 0 && free < kMinFreeBytesStart)
        return fail(tr("Not enough free disk space to record (%1 MB free, at least %2 MB required).")
                        .arg(free >> 20).arg(kMinFreeBytesStart >> 20));

    m_basePath = outputPath;
    m_currentPath = outputPath;
    m_startFormat = format;
    m_segmentIndex = 0;
    m_framesDropped = 0;
    m_framesEncoded = 0;
    m_lastReportedDrops = 0;
    {
        std::lock_guard<std::mutex> lock(m_poolMutex);
        m_stopRequested = false;
    }

    m_active = true;
    m_worker = std::thread(&VideoRecorder::workerLoop, this);
    return true;
}

bool VideoRecorder::initializeRecording(QString* errorMessage)
{
    const Format& format = m_startFormat;

    // CUDA device + NVENC. The conversion kernel runs in the same context (pushed around each
    // launch). Not using the primary context: it fails if something else initialised CUDA first.
    int ret = av_hwdevice_ctx_create(&m_hwDeviceCtx, AV_HWDEVICE_TYPE_CUDA, "0", nullptr, 0);
    if (ret < 0)
    {
        *errorMessage = tr("Could not initialise the NVIDIA GPU for recording (%1). "
                           "Check that the NVIDIA driver is working.").arg(avError(ret));
        return false;
    }
    m_cudaCtx = reinterpret_cast<AVCUDADeviceContext*>(
                    reinterpret_cast<AVHWDeviceContext*>(m_hwDeviceCtx->data)->hwctx)->cuda_ctx;

    if (!openSegment(segmentPath(0), format, errorMessage))
        return false;

    // A capture-thread copy from the previous recording may still be finishing
    while (m_pushesInFlight.load() > 0)
        std::this_thread::yield();

    // Pool: pre-size every buffer so the capture thread never allocates while recording.
    // Bounded in memory as well as frames: 30 x 4 MB at 1080p YUV, ~8 x 33 MB at 2160p RGB.
    const size_t bytes = static_cast<size_t>(format.rowBytes) * format.height;
    const int frames = static_cast<int>(qBound<size_t>(kMinPoolFrames, kMaxPoolBytes / qMax<size_t>(bytes, 1), kPoolFrames));
    std::vector<PoolBuffer> pool(frames);
    for (auto& buffer : pool)
        buffer.bytes.resize(bytes);
    {
        std::lock_guard<std::mutex> lock(m_poolMutex);
        m_pool.swap(pool);
        m_free.clear();
        m_ready.clear();
        for (int i = 0; i < frames; ++i)
            m_free.push_back(i);
    }
    qInfo() << "[VideoRecorder] Frame pool:" << frames << "x" << (bytes >> 10) << "KB";
    return true;
}

void VideoRecorder::stopRecording()
{
    if (!m_active.load())
        return;

    m_accepting = false;
    {
        std::lock_guard<std::mutex> lock(m_poolMutex);
        m_stopRequested = true;
    }
    m_poolCond.notify_one();
}

/// Worker thread

void VideoRecorder::workerLoop()
{
    QString error;

    if (!initializeRecording(&error))
    {
        qWarning() << "[VideoRecorder]" << error;
        closeSegment(false);
        releaseDevice();
        m_active = false;
        emit errorOccurred(error);
        emit recordingStopped(m_basePath, 0, 0);
        return;
    }

    m_lastHousekeeping = std::chrono::steady_clock::now();
    m_lastCaptureTime = -1;
    m_accepting = true;
    qInfo() << "[VideoRecorder] Recording started:" << m_currentPath << m_startFormat.width << "x"
            << m_startFormat.height << "@"
            << (m_startFormat.frameDuration ? double(m_startFormat.timeScale) / m_startFormat.frameDuration : 0.0)
            << "fps";
    emit recordingStarted(m_currentPath);

    for (;;)
    {
        int index = -1;
        {
            std::unique_lock<std::mutex> lock(m_poolMutex);
            m_poolCond.wait_for(lock, std::chrono::milliseconds(200),
                                [this] { return !m_ready.empty() || m_stopRequested; });
            if (!m_ready.empty())
            {
                index = m_ready.front();
                m_ready.pop_front();
            }
            else if (m_stopRequested)
            {
                break; // everything queued before stop has been encoded
            }
        }

        if (index >= 0)
        {
            const bool ok = encodeBuffer(m_pool[index], &error);
            {
                std::lock_guard<std::mutex> lock(m_poolMutex);
                m_free.push_back(index);
            }
            if (!ok)
                break;
        }

        if (!housekeeping(&error))
            break;
    }

    m_accepting = false;
    const bool failed = !error.isEmpty();

    // Flush the encoder and finalise the file even after an error, so what was recorded plays
    if (m_codecCtx && m_headerWritten)
    {
        QString flushError;
        sendAndWrite(nullptr, &flushError);
        if (!failed && !flushError.isEmpty())
            error = flushError;
    }
    const QString lastPath = m_currentPath;
    closeSegment(true);
    releaseDevice();

    // Give the frame pool (~124 MB at 1080p) back while not recording
    while (m_pushesInFlight.load() > 0)
        std::this_thread::yield();
    {
        std::lock_guard<std::mutex> lock(m_poolMutex);
        std::vector<PoolBuffer>().swap(m_pool);
        m_free.clear();
        m_ready.clear();
    }

    const qint64 encoded = m_framesEncoded.load();
    const qint64 dropped = m_framesDropped.load();
    qInfo() << "[VideoRecorder] Recording finished:" << lastPath << "encoded" << encoded << "dropped" << dropped;

    m_active = false;
    if (!error.isEmpty())
        emit errorOccurred(error);
    emit recordingStopped(lastPath, encoded, dropped);
}

bool VideoRecorder::housekeeping(QString* errorMessage)
{
    // Report drops (at most once per housekeeping interval)
    const qint64 dropped = m_framesDropped.load();

    const auto now = std::chrono::steady_clock::now();
    if (now - m_lastHousekeeping < std::chrono::seconds(1))
        return true;
    m_lastHousekeeping = now;

    if (dropped != m_lastReportedDrops)
    {
        m_lastReportedDrops = dropped;
        qWarning() << "[VideoRecorder] Frames dropped so far:" << dropped;
        emit framesDropped(dropped);
    }

    // Push written data to the disk so a power cut loses at most about a second
    if (m_formatCtx && m_formatCtx->pb)
        avio_flush(m_formatCtx->pb);
    if (m_syncFd >= 0)
        ::fdatasync(m_syncFd);

    const int64_t free = freeBytes(m_currentPath);
    if (free >= 0 && free < kMinFreeBytesRecord)
    {
        *errorMessage = tr("Recording stopped: the disk is almost full (%1 MB free). "
                           "The file recorded so far has been saved.").arg(free >> 20);
        return false;
    }
    return true;
}

bool VideoRecorder::encodeBuffer(const PoolBuffer& buffer, QString* errorMessage)
{
    const Format& format = buffer.format;

    // Input format changed mid-recording (e.g. camera switched mode): continue in a new file
    if (!format.sameLayout(m_segmentFormat))
    {
        if (format.pixelFormat == RawPixelFormat::Unsupported || (format.width & 1) || (format.height & 1))
        {
            m_framesDropped++;
            return true;
        }

        if (!sendAndWrite(nullptr, errorMessage))
            return false;
        closeSegment(true);
        if (!openSegment(segmentPath(++m_segmentIndex), format, errorMessage))
            return false;
        emit segmentStarted(m_currentPath);
    }

    if (m_segmentBaseTime < 0)
        m_segmentBaseTime = buffer.streamTime;
    int64_t pts = buffer.streamTime - m_segmentBaseTime;
    if (pts <= m_lastPts)
    {
        // The capture clock restarted (DeckLink streams restarted with the same format):
        // continue the timeline right after the last frame instead of discarding frames
        const int64_t step = format.frameDuration > 0 ? format.frameDuration : 1;
        m_segmentBaseTime = buffer.streamTime - (m_lastPts + step);
        pts = m_lastPts + step;
        qWarning() << "[VideoRecorder] Capture clock restarted; timeline continued";
    }

    CudaContextScope ctxScope(m_cudaCtx);
    if (!ctxScope.ok)
    {
        *errorMessage = tr("Recording stopped: lost the GPU context.");
        return false;
    }

    av_frame_unref(m_hwFrame);
    int ret = av_hwframe_get_buffer(m_hwFramesCtx, m_hwFrame, 0);
    if (ret < 0)
    {
        *errorMessage = tr("Recording stopped: GPU frame allocation failed (%1).").arg(avError(ret));
        return false;
    }

    // Single upload of the captured frame, then convert to NV12 straight into the encoder surface
    CUDA_MEMCPY2D copy = {};
    copy.srcMemoryType = CU_MEMORYTYPE_HOST;
    copy.srcHost = buffer.bytes.data();
    copy.srcPitch = static_cast<size_t>(format.rowBytes);
    copy.dstMemoryType = CU_MEMORYTYPE_DEVICE;
    copy.dstDevice = m_devSrc;
    copy.dstPitch = m_devSrcPitch;
    copy.WidthInBytes = static_cast<size_t>(format.rowBytes);
    copy.Height = static_cast<size_t>(format.height);
    if (cuMemcpy2D(&copy) != CUDA_SUCCESS)
    {
        *errorMessage = tr("Recording stopped: upload to the GPU failed.");
        return false;
    }

    const int flip = m_flipStep.load();
    const int cudaErr = launchToNv12(static_cast<FrameConvertFormat>(format.pixelFormat),
                                     reinterpret_cast<const uint8_t*>(m_devSrc), static_cast<int>(m_devSrcPitch),
                                     m_hwFrame->data[0], m_hwFrame->linesize[0],
                                     m_hwFrame->data[1], m_hwFrame->linesize[1],
                                     format.width, format.height,
                                     flip == 1 || flip == 2, flip == 2 || flip == 3, nullptr);
    if (cudaErr != 0 || cuCtxSynchronize() != CUDA_SUCCESS)
    {
        *errorMessage = tr("Recording stopped: GPU colour conversion failed (CUDA error %1).").arg(cudaErr);
        return false;
    }

    m_hwFrame->pts = pts;
    #if LIBAVUTIL_VERSION_INT >= AV_VERSION_INT(57, 30, 100)
        m_hwFrame->duration = format.frameDuration;
    #else
        m_hwFrame->pkt_duration = format.frameDuration;
    #endif
    m_lastPts = pts;

    if (!sendAndWrite(m_hwFrame, errorMessage))
        return false;

    m_framesEncoded++;
    return true;
}

bool VideoRecorder::sendAndWrite(AVFrame* frame, QString* errorMessage)
{
    int ret = avcodec_send_frame(m_codecCtx, frame);
    if (ret < 0 && ret != AVERROR_EOF)
    {
        *errorMessage = tr("Recording stopped: the video encoder failed (%1).").arg(avError(ret));
        return false;
    }

    for (;;)
    {
        ret = avcodec_receive_packet(m_codecCtx, m_packet);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            return true;
        if (ret < 0)
        {
            *errorMessage = tr("Recording stopped: the video encoder failed (%1).").arg(avError(ret));
            return false;
        }

        m_packet->stream_index = m_stream->index;
        av_packet_rescale_ts(m_packet, m_codecCtx->time_base, m_stream->time_base);
        ret = av_interleaved_write_frame(m_formatCtx, m_packet);
        av_packet_unref(m_packet);
        if (ret < 0)
        {
            *errorMessage = tr("Recording stopped: writing the file failed (%1). "
                               "Check the disk.").arg(avError(ret));
            return false;
        }
    }
}

/// Encoder / file setup

QString VideoRecorder::segmentPath(int index) const
{
    if (index == 0)
        return m_basePath;
    QFileInfo info(m_basePath);
    return QString("%1/%2_part%3.%4").arg(info.path(), info.completeBaseName()).arg(index + 1).arg(info.suffix());
}

bool VideoRecorder::openSegment(const QString& path, const Format& format, QString* errorMessage)
{
    const QByteArray pathUtf8 = path.toUtf8();
    const int64_t duration = format.frameDuration > 0 ? format.frameDuration : kTimeScale / 60;
    const double fps = double(kTimeScale) / duration;

    const AVCodec* codec = avcodec_find_encoder_by_name("h264_nvenc");
    if (!codec)
    {
        *errorMessage = tr("The NVIDIA H.264 encoder (NVENC) is not available.");
        return false;
    }

    // GPU frames the conversion kernel writes into
    m_hwFramesCtx = av_hwframe_ctx_alloc(m_hwDeviceCtx);
    if (!m_hwFramesCtx)
    {
        *errorMessage = tr("Could not allocate GPU frames.");
        return false;
    }
    auto* frames = reinterpret_cast<AVHWFramesContext*>(m_hwFramesCtx->data);
    frames->format = AV_PIX_FMT_CUDA;
    frames->sw_format = AV_PIX_FMT_NV12;
    frames->width = format.width;
    frames->height = format.height;
    frames->initial_pool_size = 8;
    int ret = av_hwframe_ctx_init(m_hwFramesCtx);
    if (ret < 0)
    {
        *errorMessage = tr("Could not initialise GPU frames (%1).").arg(avError(ret));
        return false;
    }

    m_codecCtx = avcodec_alloc_context3(codec);
    if (!m_codecCtx)
    {
        *errorMessage = tr("Could not allocate the video encoder.");
        return false;
    }

    // ~0.13 bits per pixel: 1080p60 -> ~16 Mbps, 1080p30 -> ~8 Mbps, 2160p30 -> ~32 Mbps
    const int64_t bitRate = qBound<int64_t>(4000000, static_cast<int64_t>(double(format.width) * format.height * fps * 0.13),
                                            60000000);

    m_codecCtx->width = format.width;
    m_codecCtx->height = format.height;
    m_codecCtx->time_base = AVRational{1, static_cast<int>(kTimeScale)};
    m_codecCtx->framerate = AVRational{static_cast<int>(kTimeScale), static_cast<int>(duration)};
    m_codecCtx->pix_fmt = AV_PIX_FMT_CUDA;
    m_codecCtx->hw_frames_ctx = av_buffer_ref(m_hwFramesCtx);
    m_codecCtx->gop_size = qMax(1, qRound(fps / 2)); // keyframe (and MP4 fragment) every 0.5 s
    m_codecCtx->max_b_frames = 0;
    m_codecCtx->bit_rate = bitRate;
    m_codecCtx->rc_max_rate = bitRate * 5 / 4;
    m_codecCtx->rc_buffer_size = static_cast<int>(bitRate * 2);
    m_codecCtx->color_range = AVCOL_RANGE_MPEG;
    m_codecCtx->colorspace = AVCOL_SPC_BT709;
    m_codecCtx->color_primaries = AVCOL_PRI_BT709;
    m_codecCtx->color_trc = AVCOL_TRC_BT709;

    const struct { const char* key; const char* value; } nvencOptions[] = {
        {"preset", "p4"}, {"tune", "hq"}, {"rc", "vbr"}, {"profile", "high"}, {"spatial-aq", "1"},
    };
    for (const auto& opt : nvencOptions)
    {
        ret = av_opt_set(m_codecCtx->priv_data, opt.key, opt.value, 0);
        if (ret < 0)
            qWarning() << "[VideoRecorder] NVENC option" << opt.key << "=" << opt.value << "not applied:" << avError(ret);
    }

    ret = avformat_alloc_output_context2(&m_formatCtx, nullptr, "mp4", pathUtf8.constData());
    if (ret < 0 || !m_formatCtx)
    {
        *errorMessage = tr("Could not create the MP4 file (%1).").arg(avError(ret));
        return false;
    }
    if (m_formatCtx->oformat->flags & AVFMT_GLOBALHEADER)
        m_codecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

    ret = avcodec_open2(m_codecCtx, codec, nullptr);
    if (ret < 0)
    {
        *errorMessage = tr("Could not start the NVIDIA encoder (%1).").arg(avError(ret));
        return false;
    }

    m_stream = avformat_new_stream(m_formatCtx, nullptr);
    if (!m_stream || avcodec_parameters_from_context(m_stream->codecpar, m_codecCtx) < 0)
    {
        *errorMessage = tr("Could not create the video stream.");
        return false;
    }
    m_stream->time_base = m_codecCtx->time_base;
    m_stream->avg_frame_rate = m_codecCtx->framerate;

    ret = avio_open(&m_formatCtx->pb, pathUtf8.constData(), AVIO_FLAG_WRITE);
    if (ret < 0)
    {
        *errorMessage = tr("Could not open the recording file for writing (%1):\n%2").arg(avError(ret), path);
        return false;
    }

    // Fragmented MP4: every keyframe starts a self-contained fragment, so a crash or power cut
    // leaves a playable file up to the last fragment instead of an unreadable one.
    AVDictionary* muxOptions = nullptr;
    av_dict_set(&muxOptions, "movflags", "frag_keyframe+empty_moov+default_base_moof", 0);
    m_formatCtx->flush_packets = 1; // hand each finished fragment to the OS immediately
    ret = avformat_write_header(m_formatCtx, &muxOptions);
    av_dict_free(&muxOptions);
    if (ret < 0)
    {
        *errorMessage = tr("Could not write the recording file header (%1).").arg(avError(ret));
        return false;
    }
    m_headerWritten = true;

    m_packet = av_packet_alloc();
    m_hwFrame = av_frame_alloc();
    if (!m_packet || !m_hwFrame)
    {
        *errorMessage = tr("Out of memory while starting the recording.");
        return false;
    }

    // Device buffer receiving the raw captured frame
    if (!m_devSrc || m_devSrcPitch < static_cast<size_t>(format.rowBytes) ||
        m_devSrcBytes < m_devSrcPitch * static_cast<size_t>(format.height))
    {
        CudaContextScope ctxScope(m_cudaCtx);
        if (m_devSrc)
            cuMemFree(m_devSrc);
        m_devSrc = 0;
        if (!ctxScope.ok || cuMemAllocPitch(&m_devSrc, &m_devSrcPitch, static_cast<size_t>(format.rowBytes),
                                            static_cast<size_t>(format.height), 16) != CUDA_SUCCESS)
        {
            m_devSrc = 0;
            *errorMessage = tr("Could not allocate GPU memory for recording.");
            return false;
        }
        m_devSrcBytes = m_devSrcPitch * format.height;
    }

    m_syncFd = ::open(pathUtf8.constData(), O_RDONLY | O_CLOEXEC);
    m_currentPath = path;
    m_segmentFormat = format;
    m_segmentBaseTime = -1;
    m_lastPts = -1;

    qInfo() << "[VideoRecorder] Segment opened:" << path << format.width << "x" << format.height
            << "fps" << fps << "bitrate" << bitRate;
    return true;
}

void VideoRecorder::closeSegment(bool writeTrailer)
{
    if (m_formatCtx)
    {
        if (writeTrailer && m_headerWritten && m_formatCtx->pb)
        {
            const int ret = av_write_trailer(m_formatCtx);
            if (ret < 0)
                qWarning() << "[VideoRecorder] Writing file trailer failed:" << avError(ret);
        }
        if (m_formatCtx->pb)
            avio_closep(&m_formatCtx->pb);
        avformat_free_context(m_formatCtx);
        m_formatCtx = nullptr;
    }
    m_stream = nullptr;
    m_headerWritten = false;

    if (m_syncFd >= 0)
    {
        ::fdatasync(m_syncFd);
        ::close(m_syncFd);
        m_syncFd = -1;
    }

    avcodec_free_context(&m_codecCtx);
    av_buffer_unref(&m_hwFramesCtx);
    av_frame_free(&m_hwFrame);
    av_packet_free(&m_packet);
    m_segmentFormat = Format();
}

void VideoRecorder::releaseDevice()
{
    if (m_devSrc)
    {
        CudaContextScope ctxScope(m_cudaCtx);
        cuMemFree(m_devSrc);
        m_devSrc = 0;
        m_devSrcPitch = 0;
        m_devSrcBytes = 0;
    }
    m_cudaCtx = nullptr;
    av_buffer_unref(&m_hwDeviceCtx);
}



