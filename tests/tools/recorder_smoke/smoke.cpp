#include "ui/Recording/VideoRecorder.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTimer>
#include <cstdio>
#include <thread>
extern "C" {
#include <libavformat/avformat.h>
}
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const int W = 1920, H = 1080, secs = argc > 1 ? atoi(argv[1]) : 10;
    const char* path = argc > 2 ? argv[2] : "/tmp/claude-1000/smoke.mp4";
    std::vector<uint8_t> frame(W * 2 * H);
    VideoRecorder rec;
    bool stopped = false, started = false; qint64 enc = 0, drop = 0; int64_t fedAtStart = -1;
    QObject::connect(&rec, &VideoRecorder::recordingStopped, [&](const QString& p, qint64 e, qint64 d) { stopped = true; enc = e; drop = d; });
    QObject::connect(&rec, &VideoRecorder::recordingStarted, [&](const QString&) { started = true; });
    QObject::connect(&rec, &VideoRecorder::errorOccurred, [&](const QString& m) { printf("ERROR: %s\n", qPrintable(m)); });
    const int64_t dur = 2000; // 60 fps at 120000
    auto feed = [&](int64_t i) {
        // moving vertical bar pattern
        for (int y = 0; y < H; ++y) { uint8_t* r = frame.data() + y * W * 2;
            for (int x = 0; x < W; x += 2) { bool bar = ((x + i * 8) % 400) < 40; r[x*2]=128; r[x*2+1]= bar?235:16; r[x*2+2]=128; r[x*2+3]= bar?235:16; } }
        RawVideoFrame f; f.data = frame.data(); f.rowBytes = W * 2; f.width = W; f.height = H; f.format = RawPixelFormat::UYVY;
        f.streamTime = i * dur; f.frameDuration = dur; f.timeScale = 120000; rec.pushFrame(f); };
    feed(0); // establish format
    QString err;
    QElapsedTimer t; t.start();
    if (!rec.startRecording(path, &err)) { printf("start failed: %s\n", qPrintable(err)); return 1; }
    printf("startRecording() returned in %lld ms\n", (long long)t.elapsed());
    QElapsedTimer wall; wall.start();
    int64_t n = 0;
    while (wall.elapsed() < secs * 1000) {
        feed(++n);
        app.processEvents();
        if (started && fedAtStart < 0) { fedAtStart = n; printf("recordingStarted after %lld ms (frame %lld)\n", (long long)t.elapsed(), (long long)n); }
        int64_t next = (n * 1000) / 60; int64_t now = wall.elapsed(); if (next > now) std::this_thread::sleep_for(std::chrono::milliseconds(next - now));
    }
    t.restart(); rec.stopRecording(); printf("stopRecording() returned in %lld ms\n", (long long)t.elapsed());
    while (!stopped) { app.processEvents(); std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
    printf("finalised after %lld ms; fed after start=%lld encoded=%lld dropped=%lld wall=%.2fs\n", (long long)t.elapsed(), (long long)(n - fedAtStart + 1), (long long)enc, (long long)drop, wall.elapsed()/1000.0);
    AVFormatContext* fc = nullptr;
    if (avformat_open_input(&fc, path, nullptr, nullptr) < 0) { printf("FILE NOT PLAYABLE\n"); return 2; }
    avformat_find_stream_info(fc, nullptr);
    AVStream* st = fc->streams[0]; int64_t pk = 0; AVPacket* p = av_packet_alloc(); int kf = 0;
    while (av_read_frame(fc, p) >= 0) { pk++; if (p->flags & AV_PKT_FLAG_KEY) kf++; av_packet_unref(p); }
    printf("file: %dx%d codec=%s fps=%.3f duration=%.3fs packets=%lld keyframes=%d bitrate=%.1f Mbps\n", st->codecpar->width, st->codecpar->height,
        avcodec_get_name(st->codecpar->codec_id), av_q2d(st->avg_frame_rate), fc->duration / 1e6, (long long)pk, kf, fc->bit_rate / 1e6);
    return 0;
}
