#include "VideoRecorder.hpp"
#include <QDebug>

VideoRecorder::VideoRecorder(QObject* parent)
    : QObject(parent), m_recording(false),
      m_formatCtx(nullptr), m_codecCtx(nullptr), m_videoStream(nullptr),
      m_hwDeviceCtx(nullptr), m_hwFramesCtx(nullptr), m_swsCtx(nullptr),
      m_cudaStream(0), d_yuvBuffer(nullptr), m_frameCounter(0)
{
    av_log_set_level(AV_LOG_ERROR);
}

VideoRecorder::~VideoRecorder()
{
    stopRecording();
}

void VideoRecorder::startRecording(const QString& outputPath)
{
    if (m_recording.load()) {
        qWarning() << "Recording already in progress";
        return;
    }

    m_outputPath = outputPath;
    m_frameCounter = 0;

    // Clear queue
    {
        QMutexLocker locker(&m_queueMutex);
        std::queue<QImage> empty;
        m_frameQueue.swap(empty);
    }

    if (!initializeGPU()) {
        emit errorOccurred("Failed to initialize GPU");
        return;
    }

    if (!initializeFFmpegHardware()) {
        emit errorOccurred("Failed to initialize hardware encoding");
        finalizeGPU();
        return;
    }

    m_recording.store(true);
    m_recordingThread = std::thread(&VideoRecorder::recordingThreadFunc, this);
    
    emit recordingStarted();
    qDebug() << "GPU recording started:" << outputPath;
}

void VideoRecorder::stopRecording()
{
    if (!m_recording.load()) return;
    
    m_recording.store(false);
    m_queueCond.notify_all();

    if (m_recordingThread.joinable()) {
        m_recordingThread.join();
    }

    emit recordingStopped();
    qDebug() << "GPU recording stopped";
}

bool VideoRecorder::initializeGPU()
{
    // Initialize CUDA
    cudaError_t result = cudaSetDevice(0);
    if (result != cudaSuccess) {
        qWarning() << "Failed to set CUDA device:" << cudaGetErrorString(result);
        return false;
    }

    // Create CUDA stream
    result = cudaStreamCreate(&m_cudaStream);
    if (result != cudaSuccess) {
        qWarning() << "Failed to create CUDA stream";
        return false;
    }

    // Allocate GPU YUV buffer
    m_yuvBufferSize = m_width * m_height * 3 / 2; // YUV420P
    result = cudaMalloc(&d_yuvBuffer, m_yuvBufferSize);
    if (result != cudaSuccess) {
        qWarning() << "Failed to allocate GPU YUV buffer";
        return false;
    }

    qDebug() << "GPU initialization successful";
    return true;
}

bool VideoRecorder::initializeFFmpegHardware()
{
    // Create hardware device context
    int ret = av_hwdevice_ctx_create(&m_hwDeviceCtx, AV_HWDEVICE_TYPE_CUDA, nullptr, nullptr, 0);
    if (ret < 0) {
        qWarning() << "Failed to create CUDA hardware context";
        return false;
    }

    // Allocate format context
    avformat_alloc_output_context2(&m_formatCtx, nullptr, nullptr, m_outputPath.toUtf8().constData());
    if (!m_formatCtx) {
        qWarning() << "Failed to allocate output context";
        return false;
    }

    // Find NVENC encoder
    const AVCodec* codec = avcodec_find_encoder_by_name("h264_nvenc");
    if (!codec) {
        qWarning() << "NVENC encoder not found";
        return false;
    }

    // Create video stream
    m_videoStream = avformat_new_stream(m_formatCtx, codec);
    if (!m_videoStream) {
        qWarning() << "Failed to create video stream";
        return false;
    }
    
    m_videoStream->time_base = AVRational{1, m_fps};

    // Allocate codec context
    m_codecCtx = avcodec_alloc_context3(codec);
    if (!m_codecCtx) {
        qWarning() << "Failed to allocate codec context";
        return false;
    }

    // Configure codec for hardware encoding
    m_codecCtx->codec_id = codec->id;
    m_codecCtx->bit_rate = 8000000;
    m_codecCtx->width = m_width;
    m_codecCtx->height = m_height;
    m_codecCtx->time_base = AVRational{1, m_fps};
    m_codecCtx->framerate = AVRational{m_fps, 1};
    m_codecCtx->gop_size = m_fps;
    m_codecCtx->max_b_frames = 0;
    m_codecCtx->pix_fmt = AV_PIX_FMT_CUDA; // Hardware pixel format

    // Create hardware frames context BEFORE opening codec
    m_hwFramesCtx = av_hwframe_ctx_alloc(m_hwDeviceCtx);
    if (!m_hwFramesCtx) {
        qWarning() << "Failed to allocate hardware frames context";
        return false;
    }

    AVHWFramesContext* framesCtx = (AVHWFramesContext*)m_hwFramesCtx->data;
    framesCtx->format = AV_PIX_FMT_CUDA;
    framesCtx->sw_format = AV_PIX_FMT_YUV420P;
    framesCtx->width = m_width;
    framesCtx->height = m_height;
    framesCtx->initial_pool_size = 10;

    ret = av_hwframe_ctx_init(m_hwFramesCtx);
    if (ret < 0) {
        qWarning() << "Failed to initialize hardware frames context";
        return false;
    }

    // Set BOTH hardware contexts in codec
    m_codecCtx->hw_device_ctx = av_buffer_ref(m_hwDeviceCtx);
    m_codecCtx->hw_frames_ctx = av_buffer_ref(m_hwFramesCtx);

    if (m_formatCtx->oformat->flags & AVFMT_GLOBALHEADER) {
        m_codecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    // NVENC settings for performance
    av_opt_set(m_codecCtx->priv_data, "preset", "fast", 0);
    av_opt_set(m_codecCtx->priv_data, "rc", "cbr", 0);
    av_opt_set(m_codecCtx->priv_data, "bf", "0", 0);

    // Open codec AFTER setting hw_frames_ctx
    ret = avcodec_open2(m_codecCtx, codec, nullptr);
    if (ret < 0) {
        qWarning() << "Failed to open NVENC codec";
        return false;
    }

    // Copy codec parameters
    if (avcodec_parameters_from_context(m_videoStream->codecpar, m_codecCtx) < 0) {
        return false;
    }

    // Open output file
    if (!(m_formatCtx->oformat->flags & AVFMT_NOFILE)) {
        if (avio_open(&m_formatCtx->pb, m_outputPath.toUtf8().constData(), AVIO_FLAG_WRITE) < 0) {
            return false;
        }
    }

    // Write header
    if (avformat_write_header(m_formatCtx, nullptr) < 0) {
        return false;
    }

    // Initialize software scaling context (for CPU color conversion)
    m_swsCtx = sws_getContext(
        m_width, m_height, AV_PIX_FMT_RGB32,
        m_width, m_height, AV_PIX_FMT_YUV420P,
        SWS_FAST_BILINEAR, nullptr, nullptr, nullptr
    );

    if (!m_swsCtx) {
        return false;
    }

    qDebug() << "Hardware encoding initialization successful";
    return true;
}

void VideoRecorder::recordFrame(const QImage& image)
{
    if (!m_recording.load()) return;
    
    QMutexLocker locker(&m_queueMutex);
    
    if (m_frameQueue.size() > 30) {
        qDebug() << "Frame queue full, dropping frame";
        return;
    }
    
    // Minimal CPU preprocessing
    QImage processedImage = image;
    if (image.format() != QImage::Format_RGB32) {
        processedImage = image.convertToFormat(QImage::Format_RGB32);
    }
    if (processedImage.size() != QSize(m_width, m_height)) {
        processedImage = processedImage.scaled(m_width, m_height, 
                                             Qt::IgnoreAspectRatio, 
                                             Qt::FastTransformation);
    }
    
    m_frameQueue.push(processedImage);
    m_queueCond.notify_one();
}

void VideoRecorder::recordingThreadFunc()
{
    auto frameInterval = std::chrono::microseconds(1000000 / m_fps); // Frame interval
    auto lastFrameTime = std::chrono::steady_clock::now();
    
    while (m_recording.load() || !m_frameQueue.empty()) {
        QImage frame;
        bool hasFrame = false;

        {
            QMutexLocker locker(&m_queueMutex);
            if (m_frameQueue.empty() && m_recording.load()) {
                m_queueCond.wait(&m_queueMutex, 100);
            }
            
            if (!m_frameQueue.empty()) {
                frame = m_frameQueue.front();
                m_frameQueue.pop();
                hasFrame = true;
            }
        }

        if (hasFrame) {
            // Wait for proper frame timing
            auto now = std::chrono::steady_clock::now();
            auto elapsed = now - lastFrameTime;
            if (elapsed < frameInterval) {
                std::this_thread::sleep_for(frameInterval - elapsed);
            }
            
            processFrame(frame);
            lastFrameTime = std::chrono::steady_clock::now();
        }
    }

    flushEncoder();
    finalizeFFmpeg();
    finalizeGPU();
}

void VideoRecorder::processFrame(const QImage& image)
{
    if (!m_codecCtx || !m_hwFramesCtx) return;

    // Create software frame for color conversion
    AVFrame* swFrame = av_frame_alloc();
    if (!swFrame) return;

    swFrame->format = AV_PIX_FMT_YUV420P;
    swFrame->width = m_width;
    swFrame->height = m_height;

    if (av_frame_get_buffer(swFrame, 32) < 0) {
        av_frame_free(&swFrame);
        return;
    }

    // CPU color conversion (RGB -> YUV420P)
    uint8_t* inData[1] = { const_cast<uchar*>(image.bits()) };
    int inLinesize[1] = { image.bytesPerLine() };
    sws_scale(m_swsCtx, inData, inLinesize, 0, m_height, swFrame->data, swFrame->linesize);

    // Create hardware frame
    AVFrame* hwFrame = av_frame_alloc();
    if (!hwFrame) {
        av_frame_free(&swFrame);
        return;
    }

    if (av_hwframe_get_buffer(m_hwFramesCtx, hwFrame, 0) < 0) {
        av_frame_free(&swFrame);
        av_frame_free(&hwFrame);
        return;
    }

    hwFrame->format = AV_PIX_FMT_CUDA;
    hwFrame->width = m_width;
    hwFrame->height = m_height;
    hwFrame->pts = m_frameCounter;

    // Transfer data from CPU to GPU (this uploads to GPU memory)
    if (av_hwframe_transfer_data(hwFrame, swFrame, 0) < 0) {
        av_frame_free(&swFrame);
        av_frame_free(&hwFrame);
        return;
    }

    // Encode on GPU using NVENC
    int ret = avcodec_send_frame(m_codecCtx, hwFrame);
    if (ret >= 0) {
        AVPacket* pkt = av_packet_alloc();
        if (pkt) {
            while (avcodec_receive_packet(m_codecCtx, pkt) >= 0) {
                pkt->stream_index = m_videoStream->index;
                av_packet_rescale_ts(pkt, m_codecCtx->time_base, m_videoStream->time_base);
                av_interleaved_write_frame(m_formatCtx, pkt);
                av_packet_unref(pkt);
            }
            av_packet_free(&pkt);
        }
    }

    av_frame_free(&swFrame);
    av_frame_free(&hwFrame);
    m_frameCounter++;
}

void VideoRecorder::flushEncoder()
{
    if (!m_codecCtx) return;

    avcodec_send_frame(m_codecCtx, nullptr);
    
    AVPacket* pkt = av_packet_alloc();
    if (pkt) {
        while (avcodec_receive_packet(m_codecCtx, pkt) >= 0) {
            pkt->stream_index = m_videoStream->index;
            av_packet_rescale_ts(pkt, m_codecCtx->time_base, m_videoStream->time_base);
            av_interleaved_write_frame(m_formatCtx, pkt);
            av_packet_unref(pkt);
        }
        av_packet_free(&pkt);
    }

    if (m_formatCtx) {
        av_write_trailer(m_formatCtx);
    }
}

void VideoRecorder::finalizeGPU()
{
    if (d_yuvBuffer) {
        cudaFree(d_yuvBuffer);
        d_yuvBuffer = nullptr;
    }
    
    if (m_cudaStream) {
        cudaStreamDestroy(m_cudaStream);
        m_cudaStream = 0;
    }
}

void VideoRecorder::finalizeFFmpeg()
{
    if (m_swsCtx) {
        sws_freeContext(m_swsCtx);
        m_swsCtx = nullptr;
    }
    
    if (m_codecCtx) {
        avcodec_free_context(&m_codecCtx);
    }
    
    if (m_formatCtx) {
        if (!(m_formatCtx->oformat->flags & AVFMT_NOFILE)) {
            avio_closep(&m_formatCtx->pb);
        }
        avformat_free_context(m_formatCtx);
    }
    
    if (m_hwFramesCtx) {
        av_buffer_unref(&m_hwFramesCtx);
    }
    
    if (m_hwDeviceCtx) {
        av_buffer_unref(&m_hwDeviceCtx);
    }
}

// void VideoRecorder::recordFrame(const QImage& image)
// {
//     if (!m_recording.load() || !m_codecCtx || !m_formatCtx || !m_videoStream || !m_swsCtx) {
//         return;
//     }

//     // Convert and scale image if needed
//     QImage converted = image.convertToFormat(QImage::Format_RGB32);
//     if (converted.size() != QSize(m_width, m_height)) {
//         converted = converted.scaled(m_width, m_height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
//     }

//     // Allocate frame
//     AVFrame* frame = av_frame_alloc();
//     if (!frame) {
//         qWarning() << "Failed to allocate frame";
//         return;
//     }

//     frame->format = AV_PIX_FMT_YUV420P;
//     frame->width = m_width;
//     frame->height = m_height;

//     if (av_frame_get_buffer(frame, 32) < 0 || av_frame_make_writable(frame) < 0) {
//         qWarning() << "Failed to allocate frame buffers";
//         av_frame_free(&frame);
//         return;
//     }

//     // Convert from BGRA to YUV420P
//     uint8_t* inData[1] = { const_cast<uchar*>(converted.bits()) };
//     int inLinesize[1] = { converted.bytesPerLine() };
//     sws_scale(m_swsCtx, inData, inLinesize, 0, m_height, frame->data, frame->linesize);

//     // Calculate timestamp
//     auto now = std::chrono::steady_clock::now();
//     auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - m_startTime).count();
//     frame->pts = av_rescale_q(elapsed, AVRational{1, 1000000}, m_codecCtx->time_base);

//     // Encode frame
//     int ret = avcodec_send_frame(m_codecCtx, frame);
//     if (ret < 0) {
//         // qWarning() << "Failed to send frame to encoder:" << av_err2str(ret);
//         av_frame_free(&frame);
//         return;
//     }

//     // Process packets
//     AVPacket* pkt = av_packet_alloc();
//     if (!pkt) {
//         av_frame_free(&frame);
//         return;
//     }

//     while (ret >= 0) {
//         ret = avcodec_receive_packet(m_codecCtx, pkt);
//         if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
//             break;
//         } 
//         else if (ret < 0) {
//             // qWarning() << "Error during encoding:" << av_err2str(ret);
//             break;
//         }

//         // Set stream index and rescale timestamps
//         pkt->stream_index = m_videoStream->index;
//         av_packet_rescale_ts(pkt, m_codecCtx->time_base, m_videoStream->time_base);

//         // Write packet
//         if (av_interleaved_write_frame(m_formatCtx, pkt) < 0) {
//             qWarning() << "Failed to write frame";
//         }

//         av_packet_unref(pkt);
//     }

//     av_packet_free(&pkt);
//     av_frame_free(&frame);
//     m_frameCounter++;
// }


// #include "VideoRecorder.hpp"
// #include <QDebug>
// #include <chrono>

// extern "C" {
// #include <libavutil/imgutils.h>
// #include <libavutil/opt.h>
// #include <libavutil/time.h>
// }

// VideoRecorder::VideoRecorder(QObject* parent)
//     : QObject(parent), m_recording(false),
//       m_formatCtx(nullptr), m_codecCtx(nullptr), m_videoStream(nullptr), m_swsCtx(nullptr),
//       m_width(1920), m_height(1080), m_fps(30), m_frameCounter(0)
// {
//     av_log_set_level(AV_LOG_ERROR);
// }

// VideoRecorder::~VideoRecorder()
// {
//     stopRecording();
// }

// void VideoRecorder::startRecording(const QString& outputPath)
// {
//     if (m_recording.load()) {
//         qWarning() << "Recording already in progress";
//         return;
//     }

//     m_outputPath = outputPath;
//     m_frameCounter = 0;
//     m_startTime = std::chrono::steady_clock::now();

//     if (!initializeFFmpeg()) {
//         emit errorOccurred("Failed to initialize FFmpeg");
//         finalizeFFmpeg();
//         return;
//     }

//     m_recording.store(true);
//     m_recordingThread = std::thread(&VideoRecorder::recordingThreadFunc, this);
//     emit recordingStarted();
//     qDebug() << "Recording started:" << outputPath;
// }

// void VideoRecorder::stopRecording()
// {
//     if (!m_recording.load()) return;

//     m_recording.store(false);
//     m_queueCond.wakeAll();

//     if (m_recordingThread.joinable())
//         m_recordingThread.join();

//     // Flush encoder
//     if (m_codecCtx) {
//         avcodec_send_frame(m_codecCtx, nullptr);
//         AVPacket* pkt = av_packet_alloc();
//         while (avcodec_receive_packet(m_codecCtx, pkt) >= 0) {
//             if (m_formatCtx && m_videoStream) {
//                 av_packet_rescale_ts(pkt, m_codecCtx->time_base, m_videoStream->time_base);
//                 pkt->stream_index = m_videoStream->index;
//                 av_interleaved_write_frame(m_formatCtx, pkt);
//             }
//             av_packet_unref(pkt);
//         }
//         av_packet_free(&pkt);
//     }

//     if (m_formatCtx) {
//         av_write_trailer(m_formatCtx);
//     }

//     finalizeFFmpeg();
//     emit recordingStopped();
//     qDebug() << "Recording stopped and resources released";
// }

// void VideoRecorder::recordFrame(const QImage& image)
// {
//     if (!m_recording.load())
//         return;
//     pushFrame(image);
// }

// void VideoRecorder::pushFrame(const QImage& image)
// {
//     QMutexLocker locker(&m_queueMutex);
//     m_frameQueue.push(image);
//     m_queueCond.wakeOne();
// }

// void VideoRecorder::recordingThreadFunc()
// {
//     while (m_recording.load() || !m_frameQueue.empty()) {
//         QImage frame;
//         {
//             QMutexLocker locker(&m_queueMutex);
//             if (m_frameQueue.empty()) {
//                 m_queueCond.wait(&m_queueMutex, 10); // Wait max 10ms
//                 continue;
//             }
//             frame = m_frameQueue.front();
//             m_frameQueue.pop();
//         }

//         // Convert and scale
//         QImage converted = frame.convertToFormat(QImage::Format_RGB32);
//         if (converted.size() != QSize(m_width, m_height)) {
//             converted = converted.scaled(m_width, m_height, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
//         }

//         AVFrame* avFrame = av_frame_alloc();
//         if (!avFrame) continue;

//         avFrame->format = AV_PIX_FMT_YUV420P;
//         avFrame->width = m_width;
//         avFrame->height = m_height;

//         if (av_frame_get_buffer(avFrame, 32) < 0 || av_frame_make_writable(avFrame) < 0) {
//             av_frame_free(&avFrame);
//             continue;
//         }

//         uint8_t* inData[1] = { const_cast<uchar*>(converted.bits()) };
//         int inLinesize[1] = { converted.bytesPerLine() };
//         sws_scale(m_swsCtx, inData, inLinesize, 0, m_height, avFrame->data, avFrame->linesize);

//         // Timestamp
//         auto now = std::chrono::steady_clock::now();
//         auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - m_startTime).count();
//         avFrame->pts = av_rescale_q(elapsed, AVRational{1, 1000000}, m_codecCtx->time_base);

//         int ret = avcodec_send_frame(m_codecCtx, avFrame);
//         av_frame_free(&avFrame);
//         if (ret < 0) continue;

//         AVPacket* pkt = av_packet_alloc();
//         while (ret >= 0) {
//             ret = avcodec_receive_packet(m_codecCtx, pkt);
//             if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) break;
//             else if (ret < 0) break;

//             pkt->stream_index = m_videoStream->index;
//             av_packet_rescale_ts(pkt, m_codecCtx->time_base, m_videoStream->time_base);
//             av_interleaved_write_frame(m_formatCtx, pkt);
//             av_packet_unref(pkt);
//         }
//         av_packet_free(&pkt);

//         m_frameCounter++;
//     }
// }

// bool VideoRecorder::initializeFFmpeg()
// {
//     avformat_alloc_output_context2(&m_formatCtx, nullptr, nullptr, m_outputPath.toUtf8().constData());
//     if (!m_formatCtx) return false;

//     const AVCodec* codec = avcodec_find_encoder_by_name("libx264");
//     if (!codec) return false;

//     m_videoStream = avformat_new_stream(m_formatCtx, codec);
//     if (!m_videoStream) return false;

//     m_videoStream->id = m_formatCtx->nb_streams - 1;
//     m_videoStream->time_base = AVRational{1, 1000000};

//     m_codecCtx = avcodec_alloc_context3(codec);
//     if (!m_codecCtx) return false;

//     m_codecCtx->codec_id = codec->id;
//     m_codecCtx->bit_rate = 8000000;
//     m_codecCtx->width = m_width;
//     m_codecCtx->height = m_height;
//     m_codecCtx->time_base = m_videoStream->time_base;
//     m_codecCtx->framerate = AVRational{m_fps, 1};
//     m_codecCtx->gop_size = m_fps;
//     m_codecCtx->max_b_frames = 1;
//     m_codecCtx->pix_fmt = AV_PIX_FMT_YUV420P;

//     av_opt_set(m_codecCtx->priv_data, "preset", "fast", 0);
//     av_opt_set(m_codecCtx->priv_data, "crf", "23", 0);

//     if (m_formatCtx->oformat->flags & AVFMT_GLOBALHEADER)
//         m_codecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

//     if (avcodec_open2(m_codecCtx, codec, nullptr) < 0) return false;
//     if (avcodec_parameters_from_context(m_videoStream->codecpar, m_codecCtx) < 0) return false;

//     if (!(m_formatCtx->oformat->flags & AVFMT_NOFILE)) {
//         if (avio_open(&m_formatCtx->pb, m_outputPath.toUtf8().constData(), AVIO_FLAG_WRITE) < 0) return false;
//     }

//     if (avformat_write_header(m_formatCtx, nullptr) < 0) return false;

//     m_swsCtx = sws_getContext(m_width, m_height, AV_PIX_FMT_BGRA,
//                               m_width, m_height, AV_PIX_FMT_YUV420P,
//                               SWS_BILINEAR, nullptr, nullptr, nullptr);
//     if (!m_swsCtx) return false;

//     return true;
// }

// void VideoRecorder::finalizeFFmpeg()
// {
//     if (m_codecCtx) {
//         avcodec_free_context(&m_codecCtx);
//         m_codecCtx = nullptr;
//     }

//     if (m_formatCtx) {
//         if (!(m_formatCtx->oformat->flags & AVFMT_NOFILE))
//             avio_closep(&m_formatCtx->pb);
//         avformat_free_context(m_formatCtx);
//         m_formatCtx = nullptr;
//     }

//     if (m_swsCtx) {
//         sws_freeContext(m_swsCtx);
//         m_swsCtx = nullptr;
//     }

//     m_videoStream = nullptr;
// }
