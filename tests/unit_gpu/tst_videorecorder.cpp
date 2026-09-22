// tst_videorecorder - QtTest suite for ui/Recording/VideoRecorder.cpp (pushFrame -> CUDA NV12 -> NVENC -> fMP4).
//
// Safety-critical: every check is strict and every number printed is measured, not assumed.
// The recorder and cuda/frame_convert.cu are compiled unmodified from the repo; only the public API
// is used (pushFrame / startRecording / stopRecording / signals).
//
// Frames are produced by CaptureSim on its own thread (like the DeckLink capture thread), paced at
// the stream frame rate. Signals are received with queued connections on the main (GUI) thread,
// as in the application. For exact frame accounting the feeder is paused while start/stop run:
// frames counted as "fed" are exactly those pushed after recordingStarted() and before
// stopRecording(). startStopCycles runs the feeder free (concurrent with start/stop).
//
// Child processes (self re-exec with --child <mode>) are used for SIGKILL, RLIMIT_FSIZE,
// CUDA_VISIBLE_DEVICES="" and the tmpfs (user-namespace) low-disk scenarios.
//
// Environment knobs: BASIC_SECONDS, DURATION_SECONDS, CYCLES, CYCLE_SECONDS, PUSHCOST_SECONDS,
// POOL_BURST, FAIL_ROUNDS, LONG_RUN_MIN, LONG_RUN_CSV, VR_OUT_DIR, VR_KEEP=1 (keep media),
// VR_RELAX=1 (sanitizer runs: timing / drop checks become informational).

#include <QtTest/QtTest>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStorageInfo>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <mutex>
#include <numeric>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
#include <libavutil/pixdesc.h>
}
#include <cuda_runtime.h>

// Test-only access to VideoRecorder internals (the encoder's CUDA context for gpuStall).
// Everything the header pulls in is included first so the macro only affects VideoRecorder.
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
#define private public
#include "ui/Recording/VideoRecorder.hpp"
#undef private

extern "C" int gpuStallAsync(int ms); // gpu_stall.cu
extern "C" int gpuStallInContext(CUcontext ctx, int ms); // gpu_stall.cu

extern char** environ;

using Clock = std::chrono::steady_clock;
static double msSince(Clock::time_point t0) { return std::chrono::duration<double, std::milli>(Clock::now() - t0).count(); }
static int64_t nowNs() { return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count(); }
static int envInt(const char* k, int def) { bool ok = false; int v = qEnvironmentVariableIntValue(k, &ok); return ok ? v : def; }
static const bool g_relax = qEnvironmentVariableIsSet("VR_RELAX");
static const bool g_keep = qEnvironmentVariableIsSet("VR_KEEP");
static constexpr int64_t TS = 120000;

// ---------------------------------------------------------------- Qt message capture
static std::atomic<int> g_qtWarnings{0};
static void msgHandler(QtMsgType type, const QMessageLogContext&, const QString& msg)
{
    if (type == QtWarningMsg || type == QtCriticalMsg) g_qtWarnings++;
    const char* t = type == QtDebugMsg ? "D" : type == QtWarningMsg ? "W" : type == QtInfoMsg ? "I" : "C";
    fprintf(stderr, "[qt:%s] %s\n", t, msg.toLocal8Bit().constData());
    if (type == QtFatalMsg) abort();
}

// ---------------------------------------------------------------- process metrics
static long procStatusKB(const char* key)
{
    FILE* f = fopen("/proc/self/status", "r");
    if (!f) return -1;
    char line[256];
    long v = -1;
    size_t kl = strlen(key);
    while (fgets(line, sizeof line, f))
        if (strncmp(line, key, kl) == 0 && line[kl] == ':') { v = atol(line + kl + 1); break; }
    fclose(f);
    return v;
}
static int fdCount()
{
    int n = 0;
    DIR* d = opendir("/proc/self/fd");
    if (!d) return -1;
    while (auto* e = readdir(d)) if (e->d_name[0] != '.') n++;
    closedir(d);
    return n - 1; // opendir's own fd
}
// Per-process GPU memory (MiB) as reported by the driver (nvidia-smi); -1 if the pid is not listed.
static long gpuMemPidMiB(pid_t pid = getpid())
{
    FILE* p = popen("nvidia-smi --query-compute-apps=pid,used_memory --format=csv,noheader,nounits 2>/dev/null", "r");
    if (!p) return -1;
    char line[256];
    long total = -1;
    while (fgets(line, sizeof line, p)) {
        long lp = 0, mem = 0;
        if (sscanf(line, "%ld, %ld", &lp, &mem) == 2 && lp == pid) total = (total < 0 ? 0 : total) + mem;
    }
    pclose(p);
    return total;
}
// cudaMemGetInfo() initialises the CUDA runtime, which creates the primary context with default flags.
// The recorder's av_hwdevice_ctx_create(AV_CUDA_USE_PRIMARY_CONTEXT) then fails ("Primary context already
// active with incompatible flags"), see test cudaRuntimeFirst. So the harness only queries it once a
// recording has succeeded in this process.
static std::atomic<bool> g_cudartSafe{false};
static long cudaDeviceUsedMiB()
{
    if (!g_cudartSafe.load()) return -1;
    size_t fr = 0, tot = 0;
    if (cudaMemGetInfo(&fr, &tot) != cudaSuccess) return -1;
    return (long)((tot - fr) >> 20);
}
struct Sample { long rssKB = 0, hwmKB = 0, threads = 0; int fds = 0; long gpuPidMiB = -1, gpuDevMiB = -1; };
static Sample sampleNow(bool gpu = true)
{
    Sample s;
    s.rssKB = procStatusKB("VmRSS");
    s.hwmKB = procStatusKB("VmHWM");
    s.threads = procStatusKB("Threads");
    s.fds = fdCount();
    if (gpu) { s.gpuPidMiB = gpuMemPidMiB(); s.gpuDevMiB = cudaDeviceUsedMiB(); }
    return s;
}
static QString sampleStr(const Sample& s)
{
    return QString("rss=%1KB hwm=%2KB threads=%3 fds=%4 gpu_pid=%5MiB gpu_dev=%6MiB")
        .arg(s.rssKB).arg(s.hwmKB).arg(s.threads).arg(s.fds).arg(s.gpuPidMiB).arg(s.gpuDevMiB);
}

// ---------------------------------------------------------------- synthetic frames
static int bppOf(RawPixelFormat f) { return (f == RawPixelFormat::ARGB || f == RawPixelFormat::BGRA) ? 4 : 2; }
static const char* fmtName(RawPixelFormat f)
{
    switch (f) { case RawPixelFormat::UYVY: return "UYVY"; case RawPixelFormat::ARGB: return "ARGB";
                 case RawPixelFormat::BGRA: return "BGRA"; default: return "Unsupported"; }
}

struct Spec {
    int w = 1920, h = 1080;
    RawPixelFormat fmt = RawPixelFormat::UYVY;
    int64_t dur = 2000; // 60 fps at 120000
    bool hasSignal = true;
    int rowBytes() const { return w * bppOf(fmt); }
};

struct Img {
    int w = 0, h = 0, rb = 0;
    RawPixelFormat fmt = RawPixelFormat::UYVY;
    std::vector<uint8_t> d;
    Img() = default;
    Img(int w_, int h_, RawPixelFormat f) : w(w_), h(h_), rb(w_ * bppOf(f)), fmt(f), d((size_t)rb * h_, 0) {}
    uint8_t* row(int y) { return d.data() + (size_t)y * rb; }
    // UYVY only; x0/x1 even
    void fillYuv(int x0, int y0, int x1, int y1, int Y, int U, int V)
    {
        for (int y = y0; y < y1; ++y) {
            uint8_t* r = row(y);
            for (int x = x0; x < x1; x += 2) { uint8_t* p = r + x * 2; p[0] = U; p[1] = Y; p[2] = V; p[3] = Y; }
        }
    }
    // ARGB / BGRA only
    void fillRgb(int x0, int y0, int x1, int y1, int R, int G, int B, int A = 255)
    {
        for (int y = y0; y < y1; ++y) {
            uint8_t* r = row(y);
            for (int x = x0; x < x1; ++x) {
                uint8_t* p = r + x * 4;
                if (fmt == RawPixelFormat::ARGB) { p[0] = A; p[1] = R; p[2] = G; p[3] = B; }
                else { p[0] = B; p[1] = G; p[2] = R; p[3] = A; }
            }
        }
    }
    // Grey level 0..255 in any format (UYVY: limited range Y, neutral chroma)
    void fillLevel(int x0, int y0, int x1, int y1, int level)
    {
        if (fmt == RawPixelFormat::ARGB || fmt == RawPixelFormat::BGRA) fillRgb(x0, y0, x1, y1, level, level, level);
        else fillYuv(x0 & ~1, y0, x1, y1, 16 + level * 219 / 255, 128, 128);
    }
};

// Frame index (stream slot) encoded as 24 bit-blocks (32x32) in the top band
static constexpr int kIdxBits = 24;
static void writeIndex(Img& im, int64_t idx)
{
    for (int b = 0; b < kIdxBits; ++b) {
        int x = 16 + b * 40;
        im.fillLevel(x, 16, x + 32, 48, ((idx >> b) & 1) ? 255 : 0);
    }
}
static int64_t readIndex(const AVFrame* f)
{
    int64_t v = 0;
    for (int b = 0; b < kIdxBits; ++b) {
        int x = 16 + b * 40;
        long sum = 0;
        for (int y = 24; y < 40; ++y)
            for (int xx = x + 8; xx < x + 24; ++xx) sum += f->data[0][y * f->linesize[0] + xx];
        if (sum / 256 > 125) v |= (int64_t(1) << b);
    }
    return v;
}

// Gradient background + moving bright bar + frame index. Incremental update (cheap per frame).
class MovingPattern {
public:
    MovingPattern(int w, int h, RawPixelFormat f) : base(w, h, f)
    {
        base.fillLevel(0, 0, w, 80, 0);
        for (int x = 0; x < w; x += 2) base.fillLevel(x, 80, x + 2, h, x * 220 / w);
        work = base;
    }
    const uint8_t* frame(int64_t idx)
    {
        const int W = work.w, bpp = bppOf(work.fmt);
        int bx = (int)((idx * 8) % (W - 48)) & ~1;
        if (lastBar >= 0)
            for (int y = 80; y < work.h; ++y)
                memcpy(work.row(y) + lastBar * bpp, base.row(y) + lastBar * bpp, 40 * bpp);
        work.fillLevel(bx, 80, bx + 40, work.h, 250);
        lastBar = bx;
        writeIndex(work, idx);
        return work.d.data();
    }
    Img base, work;
    int lastBar = -1;
};

// K pre-generated frames of moving gradient + pseudo-random noise (+-amp): real encoder load
class NoisePattern {
public:
    NoisePattern(int w, int h, RawPixelFormat f, int K, int amp)
    {
        uint32_t s = 0x12345678u;
        auto rnd = [&]() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; };
        for (int k = 0; k < K; ++k) {
            Img im(w, h, f);
            for (int y = 0; y < h; ++y) {
                uint8_t* r = im.row(y);
                for (int x = 0; x < w; x += 2) {
                    int base = ((x + k * 24) % w) * 180 / w + (y % 64);
                    int n0 = (int)(rnd() % (2 * amp + 1)) - amp, n1 = (int)(rnd() % (2 * amp + 1)) - amp;
                    auto cl = [](int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; };
                    if (f == RawPixelFormat::UYVY) {
                        uint8_t* p = r + x * 2;
                        p[0] = cl(128 + n0 / 2, 16, 240); p[1] = cl(16 + base + n0, 16, 235);
                        p[2] = cl(128 + n1 / 2, 16, 240); p[3] = cl(16 + base + n1, 16, 235);
                    } else {
                        for (int c = 0; c < 2; ++c) {
                            uint8_t* p = r + (x + c) * 4;
                            int v = cl(base + (c ? n1 : n0), 0, 255);
                            p[0] = v; p[1] = cl(v + n1 / 2, 0, 255); p[2] = cl(v - n0 / 2, 0, 255); p[3] = v;
                        }
                    }
                }
            }
            frames.push_back(std::move(im));
        }
    }
    const uint8_t* frame(int64_t idx) { return frames[(size_t)(idx % (int64_t)frames.size())].d.data(); }
    std::vector<Img> frames;
};

// ---------------------------------------------------------------- capture thread simulator
struct PushEntry { int64_t slot; int w, h; RawPixelFormat fmt; };

class CaptureSim {
public:
    using Gen = std::function<const uint8_t*(int64_t slot)>;
    explicit CaptureSim(VideoRecorder& r) : rec(r) {}
    ~CaptureSim() { stop(); }
    void setSource(const Spec& s, Gen g) { std::lock_guard<std::mutex> l(m); spec = s; gen = std::move(g); }
    void setSignal(bool on) { std::lock_guard<std::mutex> l(m); spec.hasSignal = on; }
    void start() { quit = false; th = std::thread([this] { run(); }); }
    void stop() { quit = true; if (th.joinable()) th.join(); }
    void pause() { pauseReq = true; while (!paused.load() && th.joinable()) std::this_thread::sleep_for(std::chrono::microseconds(100)); }
    void resume() { pauseReq = false; }
    std::vector<PushEntry> takeLog() { std::lock_guard<std::mutex> l(logM); return log; }
    std::vector<double> takeCost() { std::lock_guard<std::mutex> l(logM); return costUs; }
    void clearLog() { std::lock_guard<std::mutex> l(logM); log.clear(); costUs.clear(); lateMaxMs = 0; }

    std::atomic<bool> counting{false}, recordCost{false};
    std::atomic<int> skipSlots{0}, burst{0};
    std::atomic<int64_t> resetSlotTo{-1};
    std::atomic<int64_t> pushed{0}, counted{0}, slot{1000};
    double lateMaxMs = 0; // guarded by logM

private:
    void run()
    {
        auto next = Clock::now();
        while (!quit.load()) {
            if (pauseReq.load()) {
                paused = true;
                while (pauseReq.load() && !quit.load()) std::this_thread::sleep_for(std::chrono::microseconds(200));
                paused = false;
                next = Clock::now();
                continue;
            }
            Spec s; Gen g;
            { std::lock_guard<std::mutex> l(m); s = spec; g = gen; }
            if (!g) { std::this_thread::sleep_for(std::chrono::milliseconds(1)); continue; }
            int64_t r = resetSlotTo.exchange(-1);
            if (r >= 0) slot = r;
            slot += skipSlots.exchange(0);
            const int64_t sl = slot.load();
            RawVideoFrame f;
            f.data = g(sl);
            f.rowBytes = s.rowBytes(); f.width = s.w; f.height = s.h; f.format = s.fmt;
            f.streamTime = sl * s.dur; f.frameDuration = s.dur; f.timeScale = TS; f.hasSignal = s.hasSignal;
            const bool c = counting.load();
            auto t0 = Clock::now();
            rec.pushFrame(f);
            const double us = std::chrono::duration<double, std::micro>(Clock::now() - t0).count();
            {
                std::lock_guard<std::mutex> l(logM);
                if (c) log.push_back({sl, s.w, s.h, s.fmt});
                if (c && recordCost) costUs.push_back(us);
            }
            if (c) counted++;
            pushed++;
            slot++;
            if (burst.load() > 0) { burst--; next = Clock::now(); continue; }
            next += std::chrono::nanoseconds(s.dur * 1000000000LL / TS);
            auto now = Clock::now();
            if (now > next) {
                double late = std::chrono::duration<double, std::milli>(now - next).count();
                { std::lock_guard<std::mutex> l(logM); lateMaxMs = std::max(lateMaxMs, late); }
                if (late > 50) next = now;
            } else {
                std::this_thread::sleep_until(next);
            }
        }
    }
    VideoRecorder& rec;
    std::thread th;
    std::mutex m;
    Spec spec;
    Gen gen;
    std::atomic<bool> quit{false}, pauseReq{false}, paused{false};
    std::mutex logM;
    std::vector<PushEntry> log;
    std::vector<double> costUs;
};

// ---------------------------------------------------------------- signal receiver (queued, GUI thread)
struct Recv : QObject {
    struct Stop { QString path; qint64 enc, drop; };
    std::vector<Stop> stops;
    QStringList errors, segments, started, order;
    std::vector<qint64> dropSig;
    std::atomic<int64_t> stopEmitNs{0}, startEmitNs{0};
    explicit Recv(VideoRecorder& r)
    {
        connect(&r, &VideoRecorder::recordingStarted, this, [this](const QString& p) { started << p; order << "started"; });
        connect(&r, &VideoRecorder::recordingStopped, this, [this](const QString& p, qint64 e, qint64 d) {
            stops.push_back({p, e, d}); order << "stopped"; });
        connect(&r, &VideoRecorder::errorOccurred, this, [this](const QString& m) {
            errors << m; order << "error"; fprintf(stderr, "[errorOccurred] %s\n", m.toUtf8().constData()); });
        connect(&r, &VideoRecorder::segmentStarted, this, [this](const QString& p) { segments << p; order << "segment"; });
        connect(&r, &VideoRecorder::framesDropped, this, [this](qint64 n) { dropSig.push_back(n); order << "drops"; });
        // Direct (worker thread) timestamps of the emissions themselves
        connect(&r, &VideoRecorder::recordingStopped, this, [this] { stopEmitNs = nowNs(); }, Qt::DirectConnection);
        connect(&r, &VideoRecorder::recordingStarted, this, [this] { startEmitNs = nowNs(); }, Qt::DirectConnection);
    }
};

template <class P> static bool waitFor(P pred, int timeoutMs)
{
    auto t0 = Clock::now();
    while (!pred()) {
        if (msSince(t0) > timeoutMs) return false;
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}
static void pump(int ms) { waitFor([] { return false; }, ms); }

// ---------------------------------------------------------------- exact start / stop protocol
struct Run {
    bool startRet = false, started = false;
    QString err;
    double startCallMs = 0, initMs = 0, stopCallMs = 0, finaliseMs = 0, finaliseEmitMs = 0, wallS = 0;
    qint64 enc = -1, drop = -1;
    QString stopPath;
    std::vector<PushEntry> log;
    std::vector<double> costUs;
    double lateMaxMs = 0;
    size_t stops0 = 0; // rv.stops.size() when recording began
    Clock::time_point tRec;
};

// Start with the feeder paused; count frames only once recordingStarted has been delivered
// (m_accepting is set before that emission), then resume the feeder.
static bool beginExact(VideoRecorder& rec, CaptureSim& sim, Recv& rv, const QString& path, Run& r, int timeoutMs = 5000)
{
    sim.pause();
    const size_t n0 = rv.started.size(), s0 = rv.stops.size();
    auto t0 = Clock::now();
    r.startRet = rec.startRecording(path, &r.err);
    r.startCallMs = msSince(t0);
    if (!r.startRet) { sim.resume(); return false; }
    waitFor([&] { return rv.started.size() > n0 || rv.stops.size() > s0; }, timeoutMs);
    r.initMs = msSince(t0);
    r.started = rv.started.size() > n0;
    if (!r.started) {
        sim.resume();
        r.err = "recordingStarted not received; errors: " + rv.errors.join(" | ");
        return false;
    }
    g_cudartSafe = true;
    sim.clearLog();
    sim.counting = true;
    r.stops0 = rv.stops.size();
    r.tRec = Clock::now();
    sim.resume();
    return true;
}

static void endExact(VideoRecorder& rec, CaptureSim& sim, Recv& rv, Run& r, int timeoutMs = 10000)
{
    sim.pause();
    sim.counting = false;
    r.wallS = std::chrono::duration<double>(Clock::now() - r.tRec).count();
    const size_t s0 = r.stops0; // a recordingStopped emitted by the recorder itself (error) also counts
    auto t0 = Clock::now();
    const int64_t t0ns = nowNs();
    rec.stopRecording();
    r.stopCallMs = msSince(t0);
    sim.resume();
    bool ok = waitFor([&] { return rv.stops.size() > s0; }, timeoutMs);
    r.finaliseMs = msSince(t0);
    r.finaliseEmitMs = ok ? (rv.stopEmitNs.load() - t0ns) / 1e6 : -1;
    if (ok) { r.enc = rv.stops.back().enc; r.drop = rv.stops.back().drop; r.stopPath = rv.stops.back().path; }
    r.log = sim.takeLog();
    r.costUs = sim.takeCost();
    r.lateMaxMs = sim.lateMaxMs;
}

// ---------------------------------------------------------------- file analysis (libavformat / libavcodec)
struct Pkt { int64_t pts, dts, dur; bool key; int size; };
struct FileInfo {
    bool opened = false;
    QString err, codec;
    int w = 0, h = 0, colorRange = -1, colorSpace = -1;
    double fmtDurS = -1, avgFps = 0, lastEndS = -1, bitrateMbps = 0;
    AVRational tb{0, 1};
    std::vector<Pkt> pkts; // sorted by pts
    int64_t bytes = 0, fileSize = -1, decoded = 0, decErr = 0;
    int readErr = 0;
    std::vector<int64_t> idx, fpts; // per decoded frame (display order)
    std::vector<int64_t> errPts;     // pts of frames / packets that failed to decode
};
using FrameCb = std::function<void(const AVFrame*, int64_t n)>;

static QString averr(int e) { char b[AV_ERROR_MAX_STRING_SIZE] = {0}; av_strerror(e, b, sizeof b); return QString("%1 (%2)").arg(b).arg(e); }

static FileInfo analyze(const QString& path, bool decode, bool readIdx = false, FrameCb cb = nullptr)
{
    FileInfo fi;
    QFileInfo qfi(path);
    fi.fileSize = qfi.exists() ? qfi.size() : -1;
    AVFormatContext* fc = nullptr;
    int r = avformat_open_input(&fc, path.toUtf8().constData(), nullptr, nullptr);
    if (r < 0) { fi.err = "avformat_open_input: " + averr(r); return fi; }
    r = avformat_find_stream_info(fc, nullptr);
    if (r < 0) { fi.err = "avformat_find_stream_info: " + averr(r); avformat_close_input(&fc); return fi; }
    int si = av_find_best_stream(fc, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (si < 0) { fi.err = "no video stream"; avformat_close_input(&fc); return fi; }
    fi.opened = true;
    AVStream* st = fc->streams[si];
    fi.codec = avcodec_get_name(st->codecpar->codec_id);
    fi.w = st->codecpar->width; fi.h = st->codecpar->height;
    fi.colorRange = st->codecpar->color_range; fi.colorSpace = st->codecpar->color_space;
    fi.tb = st->time_base;
    fi.fmtDurS = fc->duration != AV_NOPTS_VALUE ? fc->duration / (double)AV_TIME_BASE : -1;
    fi.avgFps = st->avg_frame_rate.den ? av_q2d(st->avg_frame_rate) : 0;

    AVCodecContext* dc = nullptr;
    if (decode) {
        const AVCodec* dec = avcodec_find_decoder(st->codecpar->codec_id);
        if (dec) {
            dc = avcodec_alloc_context3(dec);
            avcodec_parameters_to_context(dc, st->codecpar);
            dc->thread_count = 1; // no frame delay: each decode error maps to the packet just sent
            if (avcodec_open2(dc, dec, nullptr) < 0) avcodec_free_context(&dc);
        }
        if (!dc) fi.err = "decoder open failed";
    }
    AVPacket* pkt = av_packet_alloc();
    AVFrame* fr = av_frame_alloc();
    int64_t lastSent = AV_NOPTS_VALUE;
    auto drain = [&]() {
        for (;;) {
            int rr = avcodec_receive_frame(dc, fr);
            if (rr == AVERROR(EAGAIN) || rr == AVERROR_EOF) break;
            if (rr < 0) { fi.decErr++; fi.errPts.push_back(lastSent); break; }
            if (fr->decode_error_flags || (fr->flags & AV_FRAME_FLAG_CORRUPT)) { fi.decErr++; fi.errPts.push_back(fr->best_effort_timestamp); }
            if (readIdx) fi.idx.push_back(readIndex(fr));
            fi.fpts.push_back(fr->best_effort_timestamp);
            if (cb) cb(fr, fi.decoded);
            fi.decoded++;
            av_frame_unref(fr);
        }
    };
    int64_t maxEnd = INT64_MIN;
    while ((r = av_read_frame(fc, pkt)) >= 0) {
        if (pkt->stream_index == si) {
            fi.pkts.push_back({pkt->pts, pkt->dts, pkt->duration, (pkt->flags & AV_PKT_FLAG_KEY) != 0, pkt->size});
            fi.bytes += pkt->size;
            if (pkt->pts != AV_NOPTS_VALUE) maxEnd = std::max(maxEnd, pkt->pts + pkt->duration);
            lastSent = pkt->pts;
            if (dc) { if (avcodec_send_packet(dc, pkt) < 0) { fi.decErr++; fi.errPts.push_back(pkt->pts); } drain(); }
        }
        av_packet_unref(pkt);
    }
    if (r != AVERROR_EOF) fi.readErr = r;
    if (dc) { avcodec_send_packet(dc, nullptr); drain(); avcodec_free_context(&dc); }
    std::sort(fi.pkts.begin(), fi.pkts.end(), [](const Pkt& a, const Pkt& b) { return a.pts < b.pts; });
    if (maxEnd != INT64_MIN) fi.lastEndS = maxEnd * av_q2d(fi.tb);
    const double dur = fi.lastEndS > 0 ? fi.lastEndS : fi.fmtDurS;
    if (dur > 0) fi.bitrateMbps = fi.bytes * 8.0 / dur / 1e6;
    av_frame_free(&fr);
    av_packet_free(&pkt);
    avformat_close_input(&fc);
    return fi;
}

// Decode errors located only in the final packet (a fragment truncated by kill / write failure)
static int errorsNotAtTail(const FileInfo& fi)
{
    const int64_t lastPts = fi.pkts.empty() ? -1 : fi.pkts.back().pts;
    int n = 0;
    for (auto e : fi.errPts) n += e != lastPts;
    return n;
}

static void printFile(const char* tag, const FileInfo& fi)
{
    int keys = 0; for (auto& p : fi.pkts) keys += p.key;
    printf("  [%s] size=%lld opened=%d err='%s' codec=%s %dx%d fmt_dur=%.4fs last_pts_end=%.4fs avg_fps=%.4f "
           "packets=%zu keyframes=%d bitrate=%.2fMbps decoded=%lld dec_err=%lld read_err=%s tb=%d/%d\n",
           tag, (long long)fi.fileSize, fi.opened, fi.err.toUtf8().constData(), fi.codec.toUtf8().constData(), fi.w, fi.h,
           fi.fmtDurS, fi.lastEndS, fi.avgFps, fi.pkts.size(), keys, fi.bitrateMbps, (long long)fi.decoded,
           (long long)fi.decErr, fi.readErr ? averr(fi.readErr).toUtf8().constData() : "EOF", fi.tb.num, fi.tb.den);
    fflush(stdout);
}

// pts of packet k must equal (slot_k - slot_0) * dur exactly (file time base)
static int timelineMismatches(const FileInfo& fi, const std::vector<PushEntry>& log, int64_t dur, int* firstBad = nullptr)
{
    int bad = 0;
    if (firstBad) *firstBad = -1;
    size_t n = std::min(fi.pkts.size(), log.size());
    for (size_t k = 0; k < n; ++k) {
        int64_t exp = av_rescale_q((log[k].slot - log[0].slot) * dur, AVRational{1, (int)TS}, fi.tb);
        if (fi.pkts[k].pts - fi.pkts[0].pts != exp) { if (firstBad && *firstBad < 0) *firstBad = (int)k; bad++; }
    }
    if (fi.pkts.size() != log.size()) bad += (int)std::abs((long)fi.pkts.size() - (long)log.size());
    return bad;
}

struct Pct { double med = 0, p99 = 0, max = 0, mean = 0; size_t n = 0; };
static Pct pct(std::vector<double> v)
{
    Pct p; p.n = v.size();
    if (v.empty()) return p;
    std::sort(v.begin(), v.end());
    auto q = [&](double f) { size_t i = (size_t)std::ceil(f * v.size()); return v[std::min(v.size() - 1, i ? i - 1 : 0)]; };
    p.med = q(0.5); p.p99 = q(0.99); p.max = v.back();
    p.mean = std::accumulate(v.begin(), v.end(), 0.0) / v.size();
    return p;
}

// ---------------------------------------------------------------- child processes
static QString selfExe()
{
    char buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    buf[n > 0 ? n : 0] = 0;
    return QString::fromLocal8Bit(buf);
}
struct Child { pid_t pid = -1; int outFd = -1; };
static Child spawnProc(const std::vector<std::string>& args, const std::map<std::string, std::string>& envOver)
{
    Child c;
    int pfd[2];
    if (pipe(pfd) != 0) return c;
    std::vector<std::string> envs;
    for (char** e = environ; *e; ++e) {
        std::string s(*e);
        if (envOver.count(s.substr(0, s.find('=')))) continue;
        envs.push_back(s);
    }
    for (auto& kv : envOver) envs.push_back(kv.first + "=" + kv.second);
    std::vector<char*> envp, argv;
    for (auto& s : envs) envp.push_back(const_cast<char*>(s.c_str()));
    envp.push_back(nullptr);
    for (auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, pfd[1], 1);
    posix_spawn_file_actions_addclose(&fa, pfd[0]);
    posix_spawn_file_actions_addclose(&fa, pfd[1]);
    if (posix_spawn(&c.pid, args[0].c_str(), &fa, nullptr, argv.data(), envp.data()) != 0) c.pid = -1;
    posix_spawn_file_actions_destroy(&fa);
    close(pfd[1]);
    c.outFd = pfd[0];
    return c;
}
static Child spawnSelf(const std::vector<std::string>& childArgs, const std::map<std::string, std::string>& envOver = {})
{
    std::vector<std::string> a{selfExe().toStdString(), "--child"};
    a.insert(a.end(), childArgs.begin(), childArgs.end());
    return spawnProc(a, envOver);
}
static QString readChild(Child& c, int timeoutMs, const char* until = nullptr)
{
    QString out;
    auto t0 = Clock::now();
    char buf[4096];
    while (msSince(t0) < timeoutMs) {
        pollfd p{c.outFd, POLLIN, 0};
        if (poll(&p, 1, 100) > 0) {
            ssize_t n = read(c.outFd, buf, sizeof buf);
            if (n <= 0) break;
            out += QString::fromLocal8Bit(buf, (int)n);
            if (until && out.contains(until)) break;
        }
    }
    return out;
}
static int reap(Child& c, int timeoutMs)
{
    int st = -1;
    auto t0 = Clock::now();
    while (msSince(t0) < timeoutMs) {
        pid_t r = waitpid(c.pid, &st, WNOHANG);
        if (r == c.pid) { if (c.outFd >= 0) close(c.outFd); c.outFd = -1; return st; }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    kill(c.pid, SIGKILL);
    waitpid(c.pid, &st, 0);
    if (c.outFd >= 0) close(c.outFd);
    c.outFd = -1;
    return -1;
}
static QString waitStr(int st)
{
    if (st == -1) return "timeout(killed)";
    if (WIFEXITED(st)) return QString("exited(%1)").arg(WEXITSTATUS(st));
    if (WIFSIGNALED(st)) return QString("signal(%1)").arg(WTERMSIG(st));
    return QString("status(%1)").arg(st);
}
// "REPORT k=v k=v" -> map
static std::map<QString, QString> parseReport(const QString& out)
{
    std::map<QString, QString> m;
    for (const QString& line : out.split('\n')) {
        if (!line.startsWith("REPORT ")) continue;
        for (const QString& kv : line.mid(7).split(' ', Qt::SkipEmptyParts)) {
            int e = kv.indexOf('=');
            if (e > 0) m[kv.left(e)] = kv.mid(e + 1);
        }
    }
    return m;
}
static QStringList childMsgs(const QString& out)
{
    QStringList l;
    for (const QString& line : out.split('\n')) if (line.startsWith("MSG ")) l << line.mid(4);
    return l;
}

// ---------------------------------------------------------------- child modes
static int childMain(int argc, char** argv)
{
    prctl(PR_SET_PDEATHSIG, SIGKILL); // never outlive the test runner
    const std::string mode = argv[2];
    const QString path = argc > 3 ? QString::fromLocal8Bit(argv[3]) : QString();
    int qargc = 1;
    char a0[] = "child";
    char* qargv[] = {a0, nullptr};
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QCoreApplication app(qargc, qargv);
    qInstallMessageHandler(msgHandler);
    setvbuf(stdout, nullptr, _IOLBF, 0);

    if (mode == "crash") {
        const bool noisy = argc > 4 && !strcmp(argv[4], "noise");
        std::unique_ptr<MovingPattern> mp;
        std::unique_ptr<NoisePattern> np;
        if (noisy) np = std::make_unique<NoisePattern>(1920, 1080, RawPixelFormat::UYVY, 16, 12);
        else mp = std::make_unique<MovingPattern>(1920, 1080, RawPixelFormat::UYVY);
        VideoRecorder rec;
        Recv rv(rec);
        CaptureSim sim(rec);
        sim.setSource(Spec{}, [&](int64_t s) { return noisy ? np->frame(s) : mp->frame(s); });
        sim.start();
        pump(100);
        Run r;
        if (!beginExact(rec, sim, rv, path, r)) { printf("STARTFAIL %s\n", r.err.toUtf8().constData()); return 3; }
        printf("STARTED\n");
        fflush(stdout);
        for (;;) pump(1000);
    }

    if (mode == "diskfull") {
        const long limit = argc > 4 ? atol(argv[4]) : 3000000;
        signal(SIGXFSZ, SIG_IGN);
        rlimit rl{(rlim_t)limit, (rlim_t)limit};
        setrlimit(RLIMIT_FSIZE, &rl);
        NoisePattern pat(1920, 1080, RawPixelFormat::UYVY, 16, 60); // declared before the feeder: must outlive it
        VideoRecorder rec;
        Recv rv(rec);
        CaptureSim sim(rec);
        sim.setSource(Spec{}, [&](int64_t s) { return pat.frame(s); });
        sim.start();
        pump(100);
        Run r;
        if (!beginExact(rec, sim, rv, path, r)) { printf("REPORT started=0\nMSG %s\n", r.err.toUtf8().constData()); return 0; }
        auto tRec = Clock::now();
        waitFor([&] { return !rv.errors.isEmpty() || !rv.stops.empty(); }, 15000);
        const double tErrMs = rv.errors.isEmpty() ? -1 : msSince(tRec);
        waitFor([&] { return !rv.stops.empty(); }, 5000);
        const bool stoppedBySelf = !rv.stops.empty();
        const bool recAfter = rec.isRecording();
        if (!stoppedBySelf) { rec.stopRecording(); waitFor([&] { return !rv.stops.empty(); }, 5000); }
        sim.pause();
        sim.counting = false;
        printf("REPORT started=1 errors=%d t_error_ms=%.0f stops=%zu stopped_by_itself=%d is_recording_after=%d "
               "enc=%lld drop=%lld fed=%lld order=%s\n",
               (int)rv.errors.size(), tErrMs, rv.stops.size(), stoppedBySelf, recAfter,
               rv.stops.empty() ? -1LL : (long long)rv.stops.back().enc, rv.stops.empty() ? -1LL : (long long)rv.stops.back().drop,
               (long long)sim.counted.load(), rv.order.join(",").toUtf8().constData());
        for (auto& e : rv.errors) printf("MSG %s\n", QString(e).replace('\n', ' ').toUtf8().constData());
        fflush(stdout);
        return 0;
    }

    if (mode == "cudartfirst") {
        // Any CUDA runtime use before the first recording (e.g. a CUDA preview path)
        const int ce = (int)cudaFree(nullptr);
        MovingPattern pat(1920, 1080, RawPixelFormat::UYVY);
        VideoRecorder rec;
        Recv rv(rec);
        CaptureSim sim(rec);
        sim.setSource(Spec{}, [&](int64_t s) { return pat.frame(s); });
        sim.start();
        pump(200);
        Run r;
        bool ok = beginExact(rec, sim, rv, path, r);
        qint64 enc = -1;
        if (ok) { pump(1000); endExact(rec, sim, rv, r); enc = r.enc; }
        printf("REPORT cudaFree=%d start_ret=%d started=%d enc=%lld errors=%d\n", ce, r.startRet, ok, (long long)enc, (int)rv.errors.size());
        for (auto& e : rv.errors) printf("MSG %s\n", QString(e).replace('\n', ' ').toUtf8().constData());
        QFile::remove(path);
        return 0;
    }

    if (mode == "nocuda") {
        MovingPattern pat(1920, 1080, RawPixelFormat::UYVY); // declared before the feeder: must outlive it
        VideoRecorder rec;
        Recv rv(rec);
        CaptureSim sim(rec);
        sim.setSource(Spec{}, [&](int64_t s) { return pat.frame(s); });
        sim.start();
        pump(200);
        int syncTrue = 0, syncFalse = 0, filesLeft = 0;
        Sample s1, s10;
        QStringList syncMsgs;
        for (int i = 0; i < 10; ++i) {
            QString p = path + QString("/nocuda_%1.mp4").arg(i), err;
            size_t st0 = rv.stops.size();
            bool ok = rec.startRecording(p, &err);
            if (ok) { syncTrue++; waitFor([&] { return rv.stops.size() > st0; }, 8000); }
            else { syncFalse++; syncMsgs << err; }
            pump(20);
            if (QFile::exists(p)) filesLeft++;
            if (i == 0) s1 = sampleNow(false);
        }
        pump(200);
        s10 = sampleNow(false);
        printf("REPORT attempts=10 sync_true=%d sync_false=%d errors=%d stops=%zu started_signals=%d files_left=%d "
               "is_recording=%d rss_delta_kb=%ld fd_delta=%d thr_delta=%ld stops_enc0=%d\n",
               syncTrue, syncFalse, (int)rv.errors.size(), rv.stops.size(), (int)rv.started.size(), filesLeft,
               rec.isRecording(), s10.rssKB - s1.rssKB, s10.fds - s1.fds, s10.threads - s1.threads,
               (int)std::count_if(rv.stops.begin(), rv.stops.end(), [](const Recv::Stop& s) { return s.enc == 0 && s.drop == 0; }));
        if (!rv.errors.isEmpty()) printf("MSG %s\n", QString(rv.errors.first()).replace('\n', ' ').toUtf8().constData());
        for (auto& m : syncMsgs) printf("MSG sync:%s\n", QString(m).replace('\n', ' ').toUtf8().constData());
        fflush(stdout);
        return 0;
    }

    if (mode == "lowdisk") {
        // Runs inside `unshare -rm` with a private tmpfs mounted at <path>
        const std::string sub = argc > 4 ? argv[4] : "refuse";
        MovingPattern pat(1920, 1080, RawPixelFormat::UYVY); // declared before the feeder: must outlive it
        VideoRecorder rec;
        Recv rv(rec);
        CaptureSim sim(rec);
        sim.setSource(Spec{}, [&](int64_t s) { return pat.frame(s); });
        sim.start();
        pump(200);
        QStorageInfo si(path);
        printf("INFO tmpfs_free_mib=%lld\n", (long long)(si.bytesAvailable() >> 20));
        const QString file = path + "/low.mp4";
        if (sub == "refuse") {
            QString err;
            bool ok = rec.startRecording(file, &err);
            if (ok) { pump(1500); rec.stopRecording(); waitFor([&] { return !rv.stops.empty(); }, 5000); }
            printf("REPORT start_ret=%d file_exists=%d\nMSG %s\n", ok, QFile::exists(file), QString(err).replace('\n', ' ').toUtf8().constData());
            return 0;
        }
        // runlow: start with >= 2 GiB free, then fill the tmpfs to < 1 GiB free
        Run r;
        if (!beginExact(rec, sim, rv, file, r)) { printf("REPORT start_ret=0\nMSG %s\n", r.err.toUtf8().constData()); return 0; }
        pump(1000);
        const long long fillMiB = argc > 5 ? atoll(argv[5]) : 1040;
        const QString filler = path + "/filler.bin";
        int fd = ::open(filler.toLocal8Bit().constData(), O_CREAT | O_WRONLY, 0600);
        int fa = fd >= 0 ? posix_fallocate(fd, 0, (off_t)fillMiB << 20) : -1;
        if (fd >= 0) ::close(fd);
        auto tFill = Clock::now();
        printf("INFO filled=%lldMiB fallocate=%d free_now_mib=%lld\n", fillMiB, fa, (long long)(QStorageInfo(path).bytesAvailable() >> 20));
        waitFor([&] { return !rv.stops.empty(); }, 8000);
        const double tStopMs = rv.stops.empty() ? -1 : msSince(tFill);
        const bool selfStopped = !rv.stops.empty();
        if (!selfStopped) { rec.stopRecording(); waitFor([&] { return !rv.stops.empty(); }, 5000); }
        sim.pause();
        sim.counting = false;
        ::unlink(filler.toLocal8Bit().constData());
        FileInfo fi = analyze(file, true);
        printf("REPORT start_ret=1 errors=%d stops=%zu self_stopped=%d t_stop_after_fill_ms=%.0f enc=%lld drop=%lld "
               "file_opened=%d file_dur=%.3f decoded=%lld dec_err=%lld order=%s\n",
               (int)rv.errors.size(), rv.stops.size(), selfStopped, tStopMs,
               rv.stops.empty() ? -1LL : (long long)rv.stops.back().enc, rv.stops.empty() ? -1LL : (long long)rv.stops.back().drop,
               fi.opened, fi.lastEndS, (long long)fi.decoded, (long long)fi.decErr, rv.order.join(",").toUtf8().constData());
        for (auto& e : rv.errors) printf("MSG %s\n", QString(e).replace('\n', ' ').toUtf8().constData());
        return 0;
    }
    fprintf(stderr, "unknown child mode %s\n", mode.c_str());
    return 2;
}

// ---------------------------------------------------------------- checks / reporting
static QStringList g_summary;
struct Checks {
    QString test;
    QStringList fails, metrics;
    explicit Checks(const QString& t) : test(t) {}
    void check(bool ok, const QString& what)
    {
        printf("  [%s] %s\n", ok ? "ok  " : "FAIL", what.toUtf8().constData());
        fflush(stdout);
        if (!ok) fails << what;
    }
    // Timing / real-time checks: informational under sanitizers (VR_RELAX=1)
    void timing(bool ok, const QString& what)
    {
        if (g_relax) { printf("  [%s] (relaxed) %s\n", ok ? "ok  " : "info", what.toUtf8().constData()); fflush(stdout); return; }
        check(ok, what);
    }
    void metric(const QString& m) { metrics << m; printf("  METRIC %s %s\n", test.toUtf8().constData(), m.toUtf8().constData()); fflush(stdout); }
    void finish()
    {
        g_summary << QString("%1 %2 | %3%4").arg(fails.isEmpty() ? "PASS" : "FAIL", -5).arg(test, metrics.join("; "),
                                                 fails.isEmpty() ? QString() : " | FAILED: " + fails.join(" ; "));
        QVERIFY2(fails.isEmpty(), fails.join(" ; ").toUtf8().constData());
    }
};

static QString F(double v, int p = 3) { return QString::number(v, 'f', p); }

// ---------------------------------------------------------------- test class
class TstVideoRecorder : public QObject {
    Q_OBJECT
    QString m_dir;
    QString out(const QString& name) const { return m_dir + "/" + name; }
    void rm(const QString& p) const { if (!g_keep) QFile::remove(p); }
    static QString resultsDir() { return QString(SRC_DIR) + "/results"; }

    // Declaration order matters: the feeder (sim) is destroyed first, then the patterns, then the recorder.
    struct Rig {
        VideoRecorder rec;
        Recv rv{rec};
        std::map<std::tuple<int, int, int>, std::unique_ptr<MovingPattern>> pats; // reused: no RSS growth from the harness
        CaptureSim sim{rec};
        explicit Rig(Spec s = Spec{}) { setPattern(s); sim.start(); pump(100); }
        void setPattern(const Spec& s)
        {
            auto& p = pats[std::make_tuple(s.w, s.h, int(s.fmt))];
            if (!p) p = std::make_unique<MovingPattern>(s.w, s.h, s.fmt);
            MovingPattern* raw = p.get();
            sim.setSource(s, [raw](int64_t sl) { return raw->frame(sl); });
        }
    };

    // Common timeline / accounting checks for one exact run into one file
    void commonChecks(Checks& c, const Run& r, const FileInfo& fi, int64_t dur, int w, int h, bool expectNoDrop = true)
    {
        const double fedS = r.log.empty() ? 0 : (r.log.back().slot - r.log.front().slot + 1) * dur / double(TS);
        const double frameS = dur / double(TS);
        c.check(r.enc == (qint64)r.log.size(), QString("encoded == fed (%1 == %2)").arg(r.enc).arg(r.log.size()));
        if (expectNoDrop) c.check(r.drop == 0, QString("dropped == 0 (got %1)").arg(r.drop));
        c.check(fi.opened, "file opens with libavformat: " + fi.err);
        c.check(fi.codec == "h264" && fi.w == w && fi.h == h, QString("h264 %1x%2 (got %3 %4x%5)").arg(w).arg(h).arg(fi.codec).arg(fi.w).arg(fi.h));
        c.check((qint64)fi.pkts.size() == r.enc, QString("packets in file == encoded (%1 == %2)").arg(fi.pkts.size()).arg(r.enc));
        c.check(std::fabs(fi.lastEndS - fedS) <= frameS, QString("last pts end %1 s within 1 frame of fed stream time %2 s").arg(F(fi.lastEndS, 4), F(fedS, 4)));
        c.check(std::fabs(fi.fmtDurS - fedS) <= frameS, QString("container duration %1 s within 1 frame of fed %2 s").arg(F(fi.fmtDurS, 4), F(fedS, 4)));
        int firstBad = -1;
        const int tl = timelineMismatches(fi, r.log, dur, &firstBad);
        c.check(tl == 0, QString("pts timeline exact vs capture clock (mismatches=%1 first_bad=%2)").arg(tl).arg(firstBad));
    }

private slots:
    void initTestCase()
    {
        m_dir = qEnvironmentVariable("VR_OUT_DIR", "/tmp/claude-1000/vr_tests");
        QDir().mkpath(m_dir);
        QDir().mkpath(resultsDir());
        const AVCodec* enc = avcodec_find_encoder_by_name("h264_nvenc");
        const AVCodec* dec = avcodec_find_decoder(AV_CODEC_ID_H264);
        printf("# out=%s relax=%d nvenc=%p h264dec=%p libavcodec=%s pid=%d\n", m_dir.toUtf8().constData(), g_relax,
               (const void*)enc, (const void*)dec, av_version_info(), getpid());
        QVERIFY2(enc && dec, "h264_nvenc encoder and h264 decoder required");
    }

    void cleanupTestCase()
    {
        printf("\n==================== SUMMARY ====================\n");
        for (auto& s : g_summary) printf("%s\n", s.toUtf8().constData());
        fflush(stdout);
        if (!g_keep) {
            QDir d(m_dir);
            for (const QString& f : d.entryList({"*.mp4", "*.bin"}, QDir::Files)) d.remove(f);
        }
    }

    // 1 -------------------------------------------------------------------------------------------
    void basic1080p60()
    {
        Checks c("basic1080p60");
        const int secs = envInt("BASIC_SECONDS", 10);
        Rig g;
        const QString path = out("basic.mp4");
        Run r;
        QVERIFY2(beginExact(g.rec, g.sim, g.rv, path, r), r.err.toUtf8().constData());
        pump(secs * 1000);
        endExact(g.rec, g.sim, g.rv, r);
        FileInfo fi = analyze(path, true, true);
        printFile("basic", fi);

        const double feedFps = r.log.size() / r.wallS;
        c.metric(QString("fed=%1 encoded=%2 dropped=%3 feed_fps=%4 start_call_ms=%5 recordingStarted_ms=%6 stop_call_ms=%7 finalise_ms=%8 file_dur=%9s")
                     .arg(r.log.size()).arg(r.enc).arg(r.drop).arg(F(feedFps, 2)).arg(F(r.startCallMs, 2)).arg(F(r.initMs, 0))
                     .arg(F(r.stopCallMs, 3)).arg(F(r.finaliseMs, 0)).arg(F(fi.lastEndS, 4)));
        c.timing(r.log.size() >= size_t(secs * 60 * 0.98), QString("feeder kept a realistic 60 fps pace (%1 fps, max late %2 ms)").arg(F(feedFps, 2), F(r.lateMaxMs, 1)));
        commonChecks(c, r, fi, 2000, 1920, 1080);
        c.check(fi.decoded == (int64_t)r.log.size() && fi.decErr == 0, QString("all frames decode (%1 decoded, %2 errors)").arg(fi.decoded).arg(fi.decErr));
        int idxBad = 0;
        for (size_t k = 0; k < std::min(fi.idx.size(), r.log.size()); ++k) idxBad += fi.idx[k] != r.log[k].slot;
        c.check(idxBad == 0 && fi.idx.size() == r.log.size(), QString("decoded frame content == fed frame, in order, no duplicates (%1 mismatches)").arg(idxBad));
        c.check(std::fabs(fi.avgFps - 60.0) < 0.01, "avg_frame_rate 60 fps (got " + F(fi.avgFps, 4) + ")");
        std::vector<int> keyPos;
        for (size_t k = 0; k < fi.pkts.size(); ++k) if (fi.pkts[k].key) keyPos.push_back((int)k);
        int badGop = 0;
        // GOP is 0.5 s (30 frames at 60 fps): each keyframe starts an MP4 fragment, which bounds crash loss
        for (size_t k = 1; k < keyPos.size(); ++k) badGop += (keyPos[k] - keyPos[k - 1]) < 29 || (keyPos[k] - keyPos[k - 1]) > 31;
        c.check(!keyPos.empty() && keyPos[0] == 0, "first packet is a keyframe");
        c.check(badGop == 0 && keyPos.size() >= size_t(secs * 2), QString("keyframe every 30 frames (0.5 s): %1 keyframes, %2 bad intervals").arg(keyPos.size()).arg(badGop));
        c.check(g.rv.errors.isEmpty() && g.rv.dropSig.empty(), "no errorOccurred / framesDropped: " + g.rv.errors.join("|"));
        c.check(r.stopPath == path, "recordingStopped path == requested path");
        c.timing(r.startCallMs < 50, QString("startRecording() returns on GUI thread in < 50 ms (%1 ms)").arg(F(r.startCallMs, 2)));
        c.timing(r.initMs < 2000, QString("recordingStarted within 2 s (%1 ms)").arg(F(r.initMs, 0)));
        c.timing(r.stopCallMs < 50, QString("stopRecording() < 50 ms (%1 ms)").arg(F(r.stopCallMs, 3)));
        c.timing(r.finaliseMs < 2000, QString("finalisation (stop -> recordingStopped delivered) < 2 s (%1 ms, emitted at %2 ms)").arg(F(r.finaliseMs, 0), F(r.finaliseEmitMs, 0)));
        rm(path);
        c.finish();
    }

    // 2 -------------------------------------------------------------------------------------------
    void pixelExact_data()
    {
        QTest::addColumn<int>("fmt");
        QTest::newRow("UYVY") << int(RawPixelFormat::UYVY);
        QTest::newRow("ARGB") << int(RawPixelFormat::ARGB);
        QTest::newRow("BGRA") << int(RawPixelFormat::BGRA);
    }
    void pixelExact()
    {
        QFETCH(int, fmt);
        const RawPixelFormat pf = RawPixelFormat(fmt);
        Checks c(QString("pixelExact/%1").arg(fmtName(pf)));
        const int W = 1920, H = 1080, PW = 240, PH = 540;
        struct Yuv { int y, u, v; };
        std::vector<Yuv> expect;
        Img im(W, H, pf);
        const int yuvTab[16][3] = {{16,128,128},{235,128,128},{126,128,128},{63,102,240},{173,42,26},{32,240,118},{145,54,34},{106,202,222},
                                   {81,90,240},{41,240,110},{210,16,146},{170,166,16},{50,200,60},{200,60,200},{100,30,180},{220,180,90}};
        const int rgbTab[16][3] = {{0,0,0},{255,255,255},{255,0,0},{0,255,0},{0,0,255},{255,255,0},{0,255,255},{255,0,255},
                                   {128,128,128},{200,100,50},{50,100,200},{16,235,128},{240,16,16},{64,32,200},{180,200,20},{30,30,30}};
        uint32_t s = 99;
        auto rnd = [&]() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; };
        for (int p = 0; p < 16; ++p) {
            const int x0 = (p % 8) * PW, y0 = (p / 8) * PH;
            if (pf == RawPixelFormat::UYVY) {
                im.fillYuv(x0, y0, x0 + PW, y0 + PH, yuvTab[p][0], yuvTab[p][1], yuvTab[p][2]);
                expect.push_back({yuvTab[p][0], yuvTab[p][1], yuvTab[p][2]});
            } else {
                const int R = rgbTab[p][0], G = rgbTab[p][1], B = rgbTab[p][2];
                for (int y = y0; y < y0 + PH; ++y)
                    for (int x = x0; x < x0 + PW; ++x) im.fillRgb(x, y, x + 1, y + 1, R, G, B, rnd() & 255); // alpha must be ignored
                // BT.709 limited range, from the definition (Kr=0.2126, Kb=0.0722)
                const double r = R / 255.0, g = G / 255.0, b = B / 255.0;
                const double Yp = 0.2126 * r + 0.7152 * g + 0.0722 * b;
                expect.push_back({(int)std::lround(16 + 219 * Yp), (int)std::lround(128 + 224 * (b - Yp) / 1.8556),
                                  (int)std::lround(128 + 224 * (r - Yp) / 1.5748)});
            }
        }
        Spec sp; sp.fmt = pf;
        Rig g(sp);
        g.sim.setSource(sp, [&](int64_t) { return im.d.data(); });
        const QString path = out(QString("pixel_%1.mp4").arg(fmtName(pf)));
        Run r;
        QVERIFY2(beginExact(g.rec, g.sim, g.rv, path, r), r.err.toUtf8().constData());
        waitFor([&] { return g.sim.counted.load() >= 30; }, 5000);
        endExact(g.rec, g.sim, g.rv, r);
        int maxY = 0, maxU = 0, maxV = 0, worstP = -1, framesChecked = 0;
        QString worst;
        FileInfo fi = analyze(path, true, false, [&](const AVFrame* f, int64_t n) {
            if (n != 0 && n != 15 && n != 29) return;
            framesChecked++;
            for (int p = 0; p < 16; ++p) {
                const int x0 = (p % 8) * PW + 24, y0 = (p / 8) * PH + 24, x1 = x0 + PW - 48, y1 = y0 + PH - 48;
                for (int y = y0; y < y1; ++y)
                    for (int x = x0; x < x1; ++x) {
                        int e = std::abs(f->data[0][y * f->linesize[0] + x] - expect[p].y);
                        if (e > maxY) { maxY = e; worstP = p; worst = QString("Y patch %1 frame %2 got %3 exp %4").arg(p).arg(n).arg(f->data[0][y * f->linesize[0] + x]).arg(expect[p].y); }
                    }
                for (int y = y0 / 2; y < y1 / 2; ++y)
                    for (int x = x0 / 2; x < x1 / 2; ++x) {
                        maxU = std::max(maxU, std::abs(f->data[1][y * f->linesize[1] + x] - expect[p].u));
                        maxV = std::max(maxV, std::abs(f->data[2][y * f->linesize[2] + x] - expect[p].v));
                    }
            }
        });
        printFile("pixel", fi);
        const int tol = pf == RawPixelFormat::UYVY ? 1 : 3;
        c.metric(QString("max_abs_err Y=%1 Cb=%2 Cr=%3 (tolerance %4, frames checked %5, all interior pixels of 16 patches)").arg(maxY).arg(maxU).arg(maxV).arg(tol).arg(framesChecked));
        c.check(r.enc == (qint64)r.log.size() && r.drop == 0 && fi.decoded == r.enc && fi.decErr == 0,
                QString("fed=%1 encoded=%2 dropped=%3 decoded=%4").arg(r.log.size()).arg(r.enc).arg(r.drop).arg(fi.decoded));
        c.check(framesChecked == 3, "frames 0/15/29 decoded and checked");
        c.check(maxY <= tol, QString("luma within +-%1 (max %2; worst: %3)").arg(tol).arg(maxY).arg(worst));
        c.check(maxU <= tol && maxV <= tol, QString("chroma within +-%1 (Cb %2, Cr %3)").arg(tol).arg(maxU).arg(maxV));
        c.check(fi.colorRange == AVCOL_RANGE_MPEG && fi.colorSpace == AVCOL_SPC_BT709,
                QString("stream signals limited range BT.709 (range=%1 space=%2)").arg(fi.colorRange).arg(fi.colorSpace));
        Q_UNUSED(worstP);
        rm(path);
        c.finish();
    }

    // 3 -------------------------------------------------------------------------------------------
    void flipSteps_data()
    {
        QTest::addColumn<int>("fmt");
        QTest::addColumn<int>("step");
        for (int f : {int(RawPixelFormat::UYVY), int(RawPixelFormat::BGRA)})
            for (int s = 0; s < 4; ++s) QTest::newRow(QString("%1_step%2").arg(fmtName(RawPixelFormat(f))).arg(s).toLatin1().constData()) << f << s;
    }
    void flipSteps()
    {
        QFETCH(int, fmt);
        QFETCH(int, step);
        const RawPixelFormat pf = RawPixelFormat(fmt);
        Checks c(QString("flipSteps/%1_step%2").arg(fmtName(pf)).arg(step));
        const int W = 1920, H = 1080, bx0 = 100, bx1 = 400, by0 = 60, by1 = 300;
        Img im(W, H, pf);
        im.fillLevel(0, 0, W, H, 0);
        int expY, expU, expV;
        if (pf == RawPixelFormat::UYVY) { im.fillYuv(bx0, by0, bx1, by1, 180, 60, 200); expY = 180; expU = 60; expV = 200; }
        else {
            im.fillRgb(bx0, by0, bx1, by1, 255, 200, 40);
            const double r = 1.0, g = 200 / 255.0, b = 40 / 255.0, Yp = 0.2126 * r + 0.7152 * g + 0.0722 * b;
            expY = (int)std::lround(16 + 219 * Yp); expU = (int)std::lround(128 + 224 * (b - Yp) / 1.8556); expV = (int)std::lround(128 + 224 * (r - Yp) / 1.5748);
        }
        const bool fh = step == 1 || step == 2, fv = step == 2 || step == 3;
        const int ex0 = fh ? W - bx1 : bx0, ex1 = fh ? W - bx0 : bx1, ey0 = fv ? H - by1 : by0, ey1 = fv ? H - by0 : by1;
        Spec sp; sp.fmt = pf;
        Rig g(sp);
        g.sim.setSource(sp, [&](int64_t) { return im.d.data(); });
        g.rec.setFlipStep(step);
        const QString path = out(QString("flip_%1_%2.mp4").arg(fmtName(pf)).arg(step));
        Run r;
        QVERIFY2(beginExact(g.rec, g.sim, g.rv, path, r), r.err.toUtf8().constData());
        waitFor([&] { return g.sim.counted.load() >= 20; }, 5000);
        endExact(g.rec, g.sim, g.rv, r);
        int minx = W, maxx = -1, miny = H, maxy = -1;
        long cnt = 0;
        int cU = -1, cV = -1;
        FileInfo fi = analyze(path, true, false, [&](const AVFrame* f, int64_t n) {
            if (n != 10) return;
            const int thr = (16 + expY) / 2;
            for (int y = 0; y < H; ++y)
                for (int x = 0; x < W; ++x)
                    if (f->data[0][y * f->linesize[0] + x] > thr) { cnt++; minx = std::min(minx, x); maxx = std::max(maxx, x); miny = std::min(miny, y); maxy = std::max(maxy, y); }
            const int cx = (ex0 + ex1) / 4, cy = (ey0 + ey1) / 4;
            cU = f->data[1][cy * f->linesize[1] + cx]; cV = f->data[2][cy * f->linesize[2] + cx];
        });
        const long area = long(bx1 - bx0) * (by1 - by0);
        c.metric(QString("block bbox x=[%1,%2] y=[%3,%4] expected x=[%5,%6] y=[%7,%8] bright_px=%9/%10 centre Cb/Cr=%11/%12 (exp %13/%14)")
                     .arg(minx).arg(maxx).arg(miny).arg(maxy).arg(ex0).arg(ex1 - 1).arg(ey0).arg(ey1 - 1).arg(cnt).arg(area)
                     .arg(cU).arg(cV).arg(expU).arg(expV));
        c.check(r.enc == (qint64)r.log.size() && r.drop == 0 && fi.decErr == 0, "all frames encoded/decoded");
        c.check(std::abs(minx - ex0) <= 1 && std::abs(maxx - (ex1 - 1)) <= 1 && std::abs(miny - ey0) <= 1 && std::abs(maxy - (ey1 - 1)) <= 1,
                "bright block at the flipped position (+-1 px)");
        c.check(std::labs(cnt - area) <= area / 100, "bright pixel count == block area (+-1%)");
        c.check(std::abs(cU - expU) <= 3 && std::abs(cV - expV) <= 3, "block chroma preserved after flip (+-3)");
        rm(path);
        c.finish();
    }

    // 4 -------------------------------------------------------------------------------------------
    void durationAccuracy()
    {
        Checks c("durationAccuracy");
        const int secs = envInt("DURATION_SECONDS", 60);
        Spec sp; sp.dur = 2002; // 59.94 fps
        Rig g(sp);
        const QString path = out("dur5994.mp4");
        Run r;
        QVERIFY2(beginExact(g.rec, g.sim, g.rv, path, r), r.err.toUtf8().constData());
        pump(secs * 1000);
        endExact(g.rec, g.sim, g.rv, r);
        FileInfo fi = analyze(path, false);
        printFile("dur", fi);
        const double fedS = r.log.size() * 2002 / double(TS);
        c.metric(QString("fed=%1 encoded=%2 dropped=%3 fed_stream_time=%4s file_last_pts_end=%5s container_dur=%6s diff=%7ms wall=%8s avg_fps=%9")
                     .arg(r.log.size()).arg(r.enc).arg(r.drop).arg(F(fedS, 4)).arg(F(fi.lastEndS, 4)).arg(F(fi.fmtDurS, 4))
                     .arg(F((fi.lastEndS - fedS) * 1000, 2)).arg(F(r.wallS, 3)).arg(F(fi.avgFps, 4)));
        commonChecks(c, r, fi, 2002, 1920, 1080);
        c.check(std::fabs(fi.avgFps - 60000.0 / 1001.0) < 0.01, "avg_frame_rate 59.94 (got " + F(fi.avgFps, 4) + ")");
        c.timing(std::fabs(fedS - r.wallS) < 0.1, QString("feeder real-time (stream %1 s vs wall %2 s)").arg(F(fedS, 3), F(r.wallS, 3)));
        rm(path);
        c.finish();
    }

    // 5 -------------------------------------------------------------------------------------------
    void streamGaps()
    {
        Checks c("streamGaps");
        Rig g;
        const QString path = out("gaps.mp4");
        Run r;
        QVERIFY2(beginExact(g.rec, g.sim, g.rv, path, r), r.err.toUtf8().constData());
        waitFor([&] { return g.sim.counted.load() >= 90; }, 5000);
        g.sim.skipSlots = 5;
        pump(3500); // > 2 s housekeeping interval so framesDropped is emitted
        endExact(g.rec, g.sim, g.rv, r);
        FileInfo fi = analyze(path, true, true);
        printFile("gaps", fi);
        int gapsFound = 0, gapAt = -1, otherBad = 0;
        for (size_t k = 1; k < fi.pkts.size(); ++k) {
            int64_t d = fi.pkts[k].pts - fi.pkts[k - 1].pts;
            if (d == 6 * 2000) { gapsFound++; gapAt = (int)k; }
            else if (d != 2000) otherBad++;
        }
        c.metric(QString("fed=%1 encoded=%2 dropped_reported=%3 gap_at_packet=%4 framesDropped_signals=[%5]")
                     .arg(r.log.size()).arg(r.enc).arg(r.drop).arg(gapAt)
                     .arg([&] { QStringList l; for (auto v : g.rv.dropSig) l << QString::number(v); return l.join(","); }()));
        commonChecks(c, r, fi, 2000, 1920, 1080, false);
        c.check(r.drop == 5, QString("recordingStopped reports dropped == 5 (got %1)").arg(r.drop));
        c.check(gapsFound == 1 && otherBad == 0, QString("exactly one 6-frame pts step in the file, others 1 frame (gaps=%1 other=%2)").arg(gapsFound).arg(otherBad));
        c.check(!g.rv.dropSig.empty() && g.rv.dropSig.back() == 5, "framesDropped(5) emitted during recording");
        int idxBad = 0;
        for (size_t k = 0; k < std::min(fi.idx.size(), r.log.size()); ++k) idxBad += fi.idx[k] != r.log[k].slot;
        c.check(idxBad == 0 && fi.decErr == 0, QString("decoded content matches the timeline (%1 mismatches)").arg(idxBad));
        rm(path);
        c.finish();
    }

    // 6 -------------------------------------------------------------------------------------------
    void poolOverflow_data()
    {
        QTest::addColumn<QString>("mode");
        QTest::newRow("burst") << "burst";       // 200 frames as fast as possible
        QTest::newRow("gpuStall") << "gpuStall"; // encoder stalled 1.5 s by a spin kernel, capture at 60 fps
    }
    void poolOverflow()
    {
        QFETCH(QString, mode);
        Checks c(QString("poolOverflow/%1").arg(mode));
        const int burstN = envInt("POOL_BURST", 200);
        Rig g;
        const QString path = out("pool.mp4");
        Run r;
        QVERIFY2(beginExact(g.rec, g.sim, g.rv, path, r), r.err.toUtf8().constData());
        pump(1000);
        g.sim.recordCost = true;
        const int64_t before = g.sim.counted.load();
        auto tb = Clock::now();
        if (mode == "burst") {
            g.sim.burst = burstN;
            waitFor([&] { return g.sim.burst.load() == 0; }, 20000);
        } else {
            // Spin inside the recorder's own CUDA context: its conversion kernel and
            // cuCtxSynchronize() queue behind it, so the encoder stalls while capture keeps pushing
            const int ce = gpuStallInContext(g.rec.m_cudaCtx, 1500);
            c.check(ce == 0, QString("stall kernel launched (cuda error %1)").arg(ce));
            pump(1500);
        }
        const double burstMs = msSince(tb);
        const int64_t burstPushed = g.sim.counted.load() - before;
        g.sim.recordCost = false;
        pump(3500); // housekeeping must report the drops while recording
        endExact(g.rec, g.sim, g.rv, r);
        FileInfo fi = analyze(path, true, true);
        printFile("pool", fi);
        const int64_t pushed = (int64_t)r.log.size();
        const int64_t lost = pushed - r.enc; // frames that never reached the file
        int64_t missingSlots = 0;            // gaps in the file's timeline
        if (!fi.pkts.empty()) missingSlots = (fi.pkts.back().pts - fi.pkts.front().pts) / 2000 + 1 - (int64_t)fi.pkts.size();
        Pct cost = pct(r.costUs);
        c.metric(QString("overflow phase: %1 frames in %2 ms (%3 fps); pushed=%4 encoded=%5 not_encoded=%6 dropped_reported=%7 "
                         "timeline_missing_slots=%8 framesDropped_signals=%9 pushFrame_us(burst) med=%10 p99=%11 max=%12")
                     .arg(burstPushed).arg(F(burstMs, 1)).arg(F(burstPushed * 1000.0 / burstMs, 0)).arg(pushed).arg(r.enc).arg(lost)
                     .arg(r.drop).arg(missingSlots).arg(g.rv.dropSig.size()).arg(F(cost.med, 1)).arg(F(cost.p99, 1)).arg(F(cost.max, 1)));
        c.check(lost > 0, QString("pool overflow was provoked (%1 frames not encoded)").arg(lost));
        c.check(r.enc + r.drop == pushed, QString("encoded + dropped == pushed (%1 + %2 = %3, pushed %4)").arg(r.enc).arg(r.drop).arg(r.enc + r.drop).arg(pushed));
        c.check(r.drop >= lost, QString("no silent loss: dropped (%1) >= frames not encoded (%2)").arg(r.drop).arg(lost));
        c.check(!g.rv.dropSig.empty() && g.rv.dropSig.back() > 0, "framesDropped signal emitted while recording");
        c.check((qint64)fi.pkts.size() == r.enc && fi.decErr == 0, "file packets == encoded, decodes cleanly");
        int idxBad = 0;
        for (size_t k = 0; k + 1 < fi.idx.size(); ++k) idxBad += fi.idx[k + 1] <= fi.idx[k];
        c.check(idxBad == 0, "no reordering / duplicates after overflow");
        int tl = 0;
        for (size_t k = 0; k < std::min(fi.idx.size(), fi.pkts.size()); ++k)
            tl += (fi.pkts[k].pts - fi.pkts[0].pts) != (fi.idx[k] - fi.idx[0]) * 2000;
        c.check(tl == 0, QString("timestamps of surviving frames follow the capture clock (%1 mismatches)").arg(tl));
        rm(path);
        c.finish();
    }

    // 7 -------------------------------------------------------------------------------------------
    void noSignal()
    {
        Checks c("noSignal");
        MovingPattern pat(1920, 1080, RawPixelFormat::UYVY);
        auto pushOne = [&](VideoRecorder& rec, bool sig) {
            RawVideoFrame f; f.data = pat.frame(1); f.rowBytes = 3840; f.width = 1920; f.height = 1080; f.format = RawPixelFormat::UYVY;
            f.streamTime = 2000; f.frameDuration = 2000; f.timeScale = TS; f.hasSignal = sig; rec.pushFrame(f); };
        QString err;
        {   // a) no frame ever
            VideoRecorder rec; const QString p = out("nosig_a.mp4");
            bool ok = rec.startRecording(p, &err);
            c.check(!ok && err.contains("signal", Qt::CaseInsensitive) && !rec.isRecording() && !QFile::exists(p), "no frame ever: false, '" + err + "'");
        }
        {   // b) last frame 600 ms ago
            VideoRecorder rec; const QString p = out("nosig_b.mp4"); err.clear();
            pushOne(rec, true); std::this_thread::sleep_for(std::chrono::milliseconds(600));
            bool ok = rec.startRecording(p, &err);
            c.check(!ok && err.contains("signal", Qt::CaseInsensitive) && !QFile::exists(p), "last frame 600 ms ago: false, '" + err + "'");
        }
        {   // c) last frame 300 ms ago -> still valid signal (threshold is 500 ms)
            VideoRecorder rec; Recv rv(rec); const QString p = out("nosig_c.mp4"); err.clear();
            pushOne(rec, true); std::this_thread::sleep_for(std::chrono::milliseconds(300));
            bool ok = rec.startRecording(p, &err);
            c.check(ok, "last frame 300 ms ago: start accepted (" + err + ")");
            if (ok) { waitFor([&] { return !rv.started.isEmpty() || !rv.stops.empty(); }, 5000); rec.stopRecording(); waitFor([&] { return !rv.stops.empty(); }, 5000); }
            c.check(!rec.isRecording(), "stopped cleanly");
            rm(p);
        }
        {   // d) only hasSignal=false frames
            VideoRecorder rec; const QString p = out("nosig_d.mp4"); err.clear();
            CaptureSim sim(rec); Spec sp; sp.hasSignal = false;
            sim.setSource(sp, [&](int64_t s) { return pat.frame(s); }); sim.start(); pump(700);
            bool ok = rec.startRecording(p, &err);
            sim.stop();
            c.check(!ok && err.contains("signal", Qt::CaseInsensitive) && !QFile::exists(p), "only no-input-source frames: false, '" + err + "'");
        }
        {   // e) good frame, then 700 ms of hasSignal=false frames at 60 fps -> stale; then signal returns -> start ok
            VideoRecorder rec; Recv rv(rec); const QString p = out("nosig_e.mp4"); err.clear();
            pushOne(rec, true);
            CaptureSim sim(rec); Spec sp; sp.hasSignal = false;
            sim.setSource(sp, [&](int64_t s) { return pat.frame(s); }); sim.start(); pump(700);
            bool ok = rec.startRecording(p, &err);
            c.check(!ok && err.contains("signal", Qt::CaseInsensitive), "hasSignal=false frames for 700 ms after a good frame: false, '" + err + "'");
            sim.setSignal(true); pump(100); err.clear();
            ok = rec.startRecording(p, &err);
            c.check(ok, "signal back: start accepted (" + err + ")");
            if (ok) {
                c.check(waitFor([&] { return !rv.started.isEmpty(); }, 5000), "recordingStarted");
                pump(500); rec.stopRecording(); waitFor([&] { return !rv.stops.empty(); }, 5000);
                FileInfo fi = analyze(p, false);
                c.check(fi.opened && !fi.pkts.empty(), QString("file playable (%1 packets)").arg(fi.pkts.size()));
            }
            sim.stop();
            rm(p);
        }
        c.finish();
    }

    // 8 -------------------------------------------------------------------------------------------
    void startFailures()
    {
        Checks c("startFailures");
        const int rounds = envInt("FAIL_ROUNDS", 10);
        Rig g;
        QDir().mkpath(out("ro_dir"));
        chmod(out("ro_dir").toLocal8Bit().constData(), 0555);
        const QString roFile = out("ro_file.mp4");
        chmod(roFile.toLocal8Bit().constData(), 0644);
        { QFile f(roFile); f.open(QIODevice::WriteOnly | QIODevice::Truncate); f.write("READONLY"); f.close(); }
        chmod(roFile.toLocal8Bit().constData(), 0444);
        QDir().mkpath(out("isdir.mp4"));

        // warm-up: one good recording so lazily initialised libraries are loaded
        Run w;
        QVERIFY2(beginExact(g.rec, g.sim, g.rv, out("warm.mp4"), w), w.err.toUtf8().constData());
        pump(1000);
        endExact(g.rec, g.sim, g.rv, w);
        rm(out("warm.mp4"));
        pump(300);
        const Sample s0 = sampleNow();
        Sample s1;
        int syncOk = 0, asyncOk = 0, total = 0;
        QStringList firstMsgs;
        for (int round = 0; round < rounds; ++round) {
            auto expectSync = [&](const QString& what, const QString& p, const char* needle) {
                QString err;
                bool ok = g.rec.startRecording(p, &err);
                bool good = !ok && !err.isEmpty() && err.contains(needle, Qt::CaseInsensitive) && !g.rec.isRecording() && !QFile::exists(p);
                if (ok) { waitFor([&] { return !g.rv.stops.empty(); }, 5000); g.rec.stopRecording(); pump(500); }
                syncOk += good; total++;
                if (round == 0) { firstMsgs << what + ": " + QString(err).replace('\n', ' '); c.check(good, what + " -> synchronous false: '" + QString(err).replace('\n', ' ') + "'"); }
                else if (!good) c.check(false, QString("round %1: %2 (ret=%3 '%4')").arg(round).arg(what).arg(ok).arg(err));
            };
            auto expectAsync = [&](const QString& what, const QString& p, const std::function<bool()>& unchanged) {
                QString err;
                const size_t st0 = g.rv.stops.size(), e0 = g.rv.errors.size(), sg0 = g.rv.started.size();
                bool ok = g.rec.startRecording(p, &err);
                bool got = ok && waitFor([&] { return g.rv.stops.size() > st0; }, 8000);
                pump(20);
                bool good = got && g.rv.errors.size() == e0 + 1 && g.rv.started.size() == sg0 && g.rv.stops.back().enc == 0 &&
                            g.rv.stops.back().drop == 0 && g.rv.stops.back().path == p && !g.rec.isRecording() && unchanged() &&
                            g.rv.order.size() >= 2 && g.rv.order[g.rv.order.size() - 2] == "error" && g.rv.order.back() == "stopped";
                asyncOk += good; total++;
                const QString msg = g.rv.errors.size() > e0 ? QString(g.rv.errors.back()).replace('\n', ' ') : QString("<none>");
                if (round == 0) { firstMsgs << what + ": " + msg; c.check(good, QString("%1 -> start()=%2, then errorOccurred + recordingStopped(path,0,0): '%3'").arg(what).arg(ok).arg(msg)); }
                else if (!good) c.check(false, QString("round %1: %2 async failure not reported correctly").arg(round).arg(what));
            };
            expectSync("missing folder", out("no_such_dir/x.mp4"), "folder");
            expectSync("read-only folder (0555)", out("ro_dir/x.mp4"), "folder");
            g.setPattern(Spec{1920, 1080, RawPixelFormat::Unsupported});
            pump(60);
            expectSync("unsupported pixel format", out("unsup.mp4"), "pixel format");
            g.setPattern(Spec{1280, 721, RawPixelFormat::UYVY});
            pump(60);
            expectSync("odd resolution 1280x721", out("odd.mp4"), "resolution");
            g.setPattern(Spec{});
            pump(60);
            expectAsync("existing read-only file (0444)", roFile, [&] { QFile f(roFile); return f.open(QIODevice::ReadOnly) && f.readAll() == "READONLY"; });
            expectAsync("path is a directory", out("isdir.mp4"), [&] { return QFileInfo(out("isdir.mp4")).isDir(); });
            pump(100);
            if (round == 0) s1 = sampleNow();
        }
        pump(300);
        const Sample sN = sampleNow();
        c.metric(QString("%1 rounds x 6 failure kinds = %2 attempts; sync ok %3, async ok %4").arg(rounds).arg(total).arg(syncOk).arg(asyncOk));
        c.metric("after warm-up: " + sampleStr(s0));
        c.metric("after round 1: " + sampleStr(s1));
        c.metric(QString("after round %1: ").arg(rounds) + sampleStr(sN));
        for (auto& m : firstMsgs) printf("  MSG %s\n", m.toUtf8().constData());
        c.check(sN.rssKB - s1.rssKB <= 4096, QString("RSS growth round1->%1 <= 4 MB (%2 KB)").arg(rounds).arg(sN.rssKB - s1.rssKB));
        // Compared against round 1, not the warm-up sample: the harness's own cudaMemGetInfo() in
        // sampleNow() creates a CUDA runtime context in this process (+~18 fds, +2 threads, ~44 MiB)
        // right after the warm-up sample is taken. That context belongs to the test, not the recorder.
        c.check(sN.fds - s1.fds <= 0, QString("no fd leak over rounds 1->%1 (%2 -> %3; warm-up %4)").arg(rounds).arg(s1.fds).arg(sN.fds).arg(s0.fds));
        c.check(sN.threads - s1.threads <= 0, QString("no thread leak (%1 -> %2)").arg(s1.threads).arg(sN.threads));
        c.check(sN.gpuPidMiB - s1.gpuPidMiB <= 2, QString("no per-process GPU memory growth (%1 -> %2 MiB)").arg(s1.gpuPidMiB).arg(sN.gpuPidMiB));

        // valid start after all failures
        const QString good = out("after_fail.mp4");
        Run r;
        bool began = beginExact(g.rec, g.sim, g.rv, good, r);
        c.check(began, "valid start after failures succeeds: " + r.err);
        if (began) {
            pump(1000);
            endExact(g.rec, g.sim, g.rv, r);
            FileInfo fi = analyze(good, true);
            c.check(fi.opened && (qint64)fi.pkts.size() == r.enc && r.enc == (qint64)r.log.size() && r.drop == 0 && fi.decErr == 0,
                    QString("recording after failures is complete (fed %1 enc %2 drop %3 packets %4)").arg(r.log.size()).arg(r.enc).arg(r.drop).arg(fi.pkts.size()));
        }
        rm(good);

        // no CUDA device: child process with CUDA_VISIBLE_DEVICES=""
        Child ch = spawnSelf({"nocuda", m_dir.toStdString()}, {{"CUDA_VISIBLE_DEVICES", ""}});
        QString o = readChild(ch, 90000, "REPORT");
        o += readChild(ch, 1000);
        int st = reap(ch, 10000);
        auto rep = parseReport(o);
        QStringList msgs = childMsgs(o);
        printf("%s", o.toUtf8().constData());
        c.metric(QString("CUDA_VISIBLE_DEVICES='' child: %1 sync_true=%2 errors=%3 stops=%4 started=%5 files_left=%6 rss_delta=%7KB fd_delta=%8 thr_delta=%9")
                     .arg(waitStr(st), rep["sync_true"], rep["errors"], rep["stops"], rep["started_signals"], rep["files_left"],
                          rep["rss_delta_kb"], rep["fd_delta"], rep["thr_delta"]));
        c.check(WIFEXITED(st) && WEXITSTATUS(st) == 0, "no-GPU child exits cleanly: " + waitStr(st));
        c.check(rep["attempts"] == "10" && rep["errors"] == "10" && rep["stops"] == "10" && rep["started_signals"] == "0" &&
                    rep["files_left"] == "0" && rep["is_recording"] == "0" && rep["stops_enc0"] == "10",
                "no-GPU child: 10 starts -> 10 x (errorOccurred + recordingStopped(0,0)), no recordingStarted, no files");
        c.check(!msgs.isEmpty() && msgs.first().contains("GPU", Qt::CaseInsensitive), "no-GPU message names the GPU: '" + msgs.value(0) + "'");
        c.check(rep["fd_delta"].toInt() <= 0 && rep["thr_delta"].toInt() <= 0 && rep["rss_delta_kb"].toLong() <= 4096,
                "no-GPU child: no fd/thread/RSS growth over 9 further failures");
        chmod(out("ro_dir").toLocal8Bit().constData(), 0755);
        QDir(out("ro_dir")).removeRecursively();
        QDir(out("isdir.mp4")).removeRecursively();
        chmod(roFile.toLocal8Bit().constData(), 0644);
        QFile::remove(roFile);
        c.finish();
    }

    // 9 -------------------------------------------------------------------------------------------
    void startStopCycles()
    {
        Checks c("startStopCycles");
        const int cycles = envInt("CYCLES", 50), secs = envInt("CYCLE_SECONDS", 2);
        Rig g; // free-running feeder: pushes continue concurrently with start / stop
        QFile csv(resultsDir() + "/cycles.csv");
        csv.open(QIODevice::WriteOnly | QIODevice::Truncate);
        csv.write("cycle,rss_kb,hwm_kb,threads,fds,gpu_pid_mib,gpu_dev_mib,encoded,dropped,init_ms,finalise_ms,file_packets,file_dur_s,dec_err\n");
        std::vector<Sample> samples;
        const Sample base = sampleNow(); // before the first recording of this test
        int notPlayable = 0, badAccount = 0, startFail = 0, drops = 0;
        std::vector<double> initMs, finMs;
        for (int i = 0; i < cycles; ++i) {
            const QString p = out(QString("cycle_%1.mp4").arg(i));
            const size_t sg0 = g.rv.started.size(), st0 = g.rv.stops.size();
            QString err;
            auto t0 = Clock::now();
            bool ok = g.rec.startRecording(p, &err);
            bool started = ok && waitFor([&] { return g.rv.started.size() > sg0 || g.rv.stops.size() > st0; }, 5000) && g.rv.started.size() > sg0;
            const double im = msSince(t0);
            if (!started) { startFail++; c.check(false, QString("cycle %1 start failed: %2 %3").arg(i).arg(err, g.rv.errors.join("|"))); pump(500); continue; }
            pump(secs * 1000);
            auto t1 = Clock::now();
            g.rec.stopRecording();
            bool stopped = waitFor([&] { return g.rv.stops.size() > st0; }, 10000);
            const double fm = msSince(t1);
            qint64 enc = stopped ? g.rv.stops.back().enc : -1, drop = stopped ? g.rv.stops.back().drop : -1;
            FileInfo fi = analyze(p, true);
            bool playable = fi.opened && fi.decErr == 0 && fi.decoded == (int64_t)fi.pkts.size() && !fi.pkts.empty();
            notPlayable += !playable;
            badAccount += !(stopped && (qint64)fi.pkts.size() == enc && enc > 0);
            drops += drop != 0;
            initMs.push_back(im); finMs.push_back(fm);
            rm(p);
            Sample s = sampleNow();
            samples.push_back(s);
            csv.write(QString("%1,%2,%3,%4,%5,%6,%7,%8,%9,%10,%11,%12,%13,%14\n").arg(i + 1).arg(s.rssKB).arg(s.hwmKB).arg(s.threads).arg(s.fds)
                          .arg(s.gpuPidMiB).arg(s.gpuDevMiB).arg(enc).arg(drop).arg(F(im, 0)).arg(F(fm, 0)).arg(fi.pkts.size()).arg(F(fi.lastEndS, 3)).arg(fi.decErr).toUtf8());
            csv.flush();
            if (i < 3 || i == 4 || (i + 1) % 10 == 0)
                printf("  cycle %d: enc=%lld drop=%lld packets=%zu dur=%.3f init=%.0fms fin=%.0fms %s\n", i + 1, (long long)enc, (long long)drop,
                       fi.pkts.size(), fi.lastEndS, im, fm, sampleStr(s).toUtf8().constData());
        }
        csv.close();
        QVERIFY(!samples.empty());
        const size_t ref = std::min<size_t>(4, samples.size() - 1);
        const Sample a = samples[ref], b = samples.back();
        Pct pi = pct(initMs), pf = pct(finMs);
        c.metric(QString("%1 cycles x %2 s; cycle %3 -> %4: RSS %5 -> %6 KB (%7 KB), GPU/pid %8 -> %9 MiB, fds %10 -> %11, threads %12 -> %13; "
                         "init ms med %14 max %15; finalise ms med %16 max %17; not playable %18, accounting errors %19, cycles with drops %20")
                     .arg(cycles).arg(secs).arg(ref + 1).arg(samples.size()).arg(a.rssKB).arg(b.rssKB).arg(b.rssKB - a.rssKB).arg(a.gpuPidMiB)
                     .arg(b.gpuPidMiB).arg(a.fds).arg(b.fds).arg(a.threads).arg(b.threads).arg(F(pi.med, 0)).arg(F(pi.max, 0)).arg(F(pf.med, 0))
                     .arg(F(pf.max, 0)).arg(notPlayable).arg(badAccount).arg(drops));
        c.check(startFail == 0, "every cycle started");
        c.check(notPlayable == 0, "all files playable (open, decode every packet without error)");
        c.check(badAccount == 0, "file packets == reported encoded in every cycle");
        c.timing(drops == 0, QString("no dropped frames in any cycle (%1 cycles with drops)").arg(drops));
        // RSS is bimodal (+-1 pool of 30 x frame buffers depending on allocator reuse), so also compare
        // the envelope: max RSS of cycles 5..10 vs max RSS of the last 5 cycles.
        long maxEarly = 0, maxLate = 0;
        for (size_t k = ref; k < std::min<size_t>(samples.size(), 10); ++k) maxEarly = std::max(maxEarly, samples[k].rssKB);
        for (size_t k = samples.size() > 5 ? samples.size() - 5 : 0; k < samples.size(); ++k) maxLate = std::max(maxLate, samples[k].rssKB);
        c.metric(QString("RSS before first cycle %1 KB, after cycle 1 %2 KB (idle retention %3 KB); envelope max(c5..10)=%4 KB max(last5)=%5 KB")
                     .arg(base.rssKB).arg(samples[0].rssKB).arg(samples[0].rssKB - base.rssKB).arg(maxEarly).arg(maxLate));
        c.metric(QString("spec comparison cycle %1 -> %2: RSS %3 KB (threshold 20480 KB)").arg(ref + 1).arg(samples.size()).arg(b.rssKB - a.rssKB));
        c.check(maxLate - maxEarly <= 20 * 1024, QString("RSS envelope growth <= 20 MB (%1 KB)").arg(maxLate - maxEarly));
        c.check(b.gpuPidMiB - a.gpuPidMiB <= 16, QString("GPU per-process growth <= 16 MiB (%1 MiB)").arg(b.gpuPidMiB - a.gpuPidMiB));
        c.check(b.fds <= a.fds, QString("fds flat (%1 -> %2)").arg(a.fds).arg(b.fds));
        c.check(b.threads <= a.threads, QString("threads flat (%1 -> %2)").arg(a.threads).arg(b.threads));
        c.finish();
    }

    // 10 ------------------------------------------------------------------------------------------
    void crashMidRecording_data()
    {
        QTest::addColumn<QString>("pattern");
        QTest::newRow("lowDetail") << "moving";   // gradient + moving bar: ~15 KB/s
        QTest::newRow("realisticNoise") << "noise"; // ~16 Mbps, like camera content
    }
    void crashMidRecording()
    {
        QFETCH(QString, pattern);
        Checks c(QString("crashMidRecording/%1").arg(QTest::currentDataTag()));
        const QString path = out(QString("crash_%1.mp4").arg(pattern));
        QFile::remove(path);
        Child ch = spawnSelf({"crash", path.toStdString(), pattern.toStdString()});
        QString o = readChild(ch, 20000, "STARTED");
        QVERIFY2(o.contains("STARTED"), ("child did not start: " + o).toUtf8().constData());
        auto t0 = Clock::now();
        std::this_thread::sleep_for(std::chrono::milliseconds(envInt("CRASH_AFTER_MS", 6000)));
        const qint64 sizeAtKill = QFileInfo(path).size();
        kill(ch.pid, SIGKILL);
        const double killedAt = msSince(t0) / 1000.0;
        int st = reap(ch, 5000);
        FileInfo fi = analyze(path, true, pattern == "moving");
        printFile("crash", fi);
        int idxBad = 0;
        for (size_t k = 1; k < fi.idx.size(); ++k) idxBad += fi.idx[k] != fi.idx[k - 1] + 1;
        c.metric(QString("killed %1 s after recordingStarted (%2); file %3 bytes (%4 at kill), playable %5 s, %6 frames decoded, %7 decode errors, lost %8 s")
                     .arg(F(killedAt, 2), waitStr(st)).arg(fi.fileSize).arg(sizeAtKill).arg(F(fi.lastEndS, 3)).arg(fi.decoded).arg(fi.decErr).arg(F(killedAt - fi.lastEndS, 2)));
        c.check(WIFSIGNALED(st) && WTERMSIG(st) == SIGKILL, "child was SIGKILLed");
        c.check(fi.opened, "file opens after SIGKILL: " + fi.err);
        c.check(fi.lastEndS >= 4.0, QString("playable duration >= 4 s (%1 s)").arg(F(fi.lastEndS, 3)));
        c.check(errorsNotAtTail(fi) == 0 && fi.decoded + fi.decErr >= (int64_t)fi.pkts.size(),
                QString("every packet decodes except at most the final truncated one (%1 errors, %2 not at the tail)").arg(fi.decErr).arg(errorsNotAtTail(fi)));
        c.metric(QString("corrupt frames from a truncated final fragment: %1").arg(fi.decErr));
        c.check(idxBad == 0, QString("frames consecutive, none missing (%1 breaks)").arg(idxBad));
        rm(path);
        c.finish();
    }

    // 11 ------------------------------------------------------------------------------------------
    void diskFull()
    {
        Checks c("diskFull");
        const QString path = out("diskfull.mp4");
        QFile::remove(path);
        const long limit = envInt("DISKFULL_BYTES", 3000000);
        Child ch = spawnSelf({"diskfull", path.toStdString(), std::to_string(limit)});
        QString o = readChild(ch, 60000, "REPORT");
        o += readChild(ch, 1500);
        int st = reap(ch, 10000);
        printf("%s", o.toUtf8().constData());
        auto rep = parseReport(o);
        QStringList msgs = childMsgs(o);
        FileInfo fi = analyze(path, true);
        printFile("diskfull", fi);
        const double tErr = rep["t_error_ms"].toDouble() / 1000.0;
        c.metric(QString("RLIMIT_FSIZE=%1 B; child %2; errorOccurred=%3 after %4 s: '%5'; recordingStopped=%6 (enc %7, drop %8), "
                         "stopped by itself=%9; file %10 B, playable %11 s, %12 decoded, %13 decode errors; order=%14")
                     .arg(limit).arg(waitStr(st), rep["errors"], F(tErr, 2), msgs.value(0), rep["stops"], rep["enc"], rep["drop"],
                          rep["stopped_by_itself"]).arg(fi.fileSize).arg(F(fi.lastEndS, 3)).arg(fi.decoded).arg(fi.decErr).arg(rep["order"]));
        c.check(rep["started"] == "1", "recording started");
        c.check(rep["errors"].toInt() >= 1, "errorOccurred emitted on write failure");
        c.check(rep["stops"] == "1" && rep["stopped_by_itself"] == "1", "recordingStopped emitted (by the recorder itself)");
        c.check(rep["is_recording_after"] == "0", "isRecording() false after the error");
        c.check(rep["order"].contains("error,stopped"), "errorOccurred precedes recordingStopped");
        c.check(fi.opened, "file opens: " + fi.err);
        const int errNotAtTail = errorsNotAtTail(fi);
        c.metric(QString("decode errors at pts [%1], last packet pts %2").arg([&] { QStringList l; for (auto e : fi.errPts) l << QString::number(e); return l.join(","); }()).arg(fi.pkts.empty() ? -1 : fi.pkts.back().pts));
        c.check(fi.decoded > 0 && errNotAtTail == 0, QString("all frames before the truncated final packet decode cleanly (%1 frames, %2 errors, %3 not in the final packet)").arg(fi.decoded).arg(fi.decErr).arg(errNotAtTail));
        // Informational: a write that fails mid-fragment leaves a truncated last fragment on disk
        c.metric(QString("corrupt frames from the truncated final fragment: %1").arg(fi.decErr));
        c.check(fi.lastEndS >= tErr - 1.2, QString("file plays up to the error minus <= 1 fragment (%1 s playable, error at %2 s)").arg(F(fi.lastEndS, 3), F(tErr, 3)));
        if (fi.fileSize > 0 && fi.fileSize < 6 * 1024 * 1024) { // keep as small evidence
            QDir().mkpath(m_dir + "/evidence");
            QFile::remove(m_dir + "/evidence/diskfull.mp4");
            QFile::copy(path, m_dir + "/evidence/diskfull.mp4");
        }
        QFile::remove(path);
        c.finish();
    }

    // 12 ------------------------------------------------------------------------------------------
    void lowDiskStart()
    {
        Checks c("lowDiskStart");
        const QString mnt = out("lowdisk_mnt");
        QDir().mkpath(mnt);
        auto runNs = [&](const QString& size, const QString& sub, const QString& extra) {
            const QString cmd = QString("mount -t tmpfs -o size=%1 tmpfs '%2' && exec '%3' --child lowdisk '%2' %4 %5")
                                    .arg(size, mnt, selfExe(), sub, extra);
            Child ch = spawnProc({"/usr/bin/unshare", "-rm", "/bin/sh", "-c", cmd.toStdString()}, {});
            QString o = readChild(ch, 60000, "REPORT");
            o += readChild(ch, 1500);
            int st = reap(ch, 15000);
            printf("  [child %s %s] %s\n%s", sub.toUtf8().constData(), waitStr(st).toUtf8().constData(), size.toUtf8().constData(), o.toUtf8().constData());
            return o;
        };
        QString o1 = runNs("1024m", "refuse", "");
        if (!o1.contains("REPORT")) QSKIP("user-namespace tmpfs not available: low-disk start is not testable without root");
        auto r1 = parseReport(o1);
        QStringList m1 = childMsgs(o1);
        c.metric(QString("tmpfs 1 GiB: start()=%1 msg='%2'").arg(r1["start_ret"], m1.value(0)));
        c.check(r1["start_ret"] == "0" && m1.value(0).contains("disk space", Qt::CaseInsensitive) && r1["file_exists"] == "0",
                "start refused synchronously below 2 GB free, no file created");

        const long availKB = [] { FILE* f = fopen("/proc/meminfo", "r"); char l[256]; long v = -1;
                                   while (f && fgets(l, sizeof l, f)) if (!strncmp(l, "MemAvailable:", 13)) v = atol(l + 13);
                                   if (f) fclose(f); return v; }();
        if (availKB < 2200 * 1024) {
            c.metric(QString("recording-time low-disk case NOT RUN: MemAvailable %1 MiB < 2200 MiB (tmpfs filler needs ~1 GiB RAM)").arg(availKB / 1024));
        } else {
            QString o2 = runNs("2056m", "runlow", "1040");
            auto r2 = parseReport(o2);
            QStringList m2 = childMsgs(o2);
            c.metric(QString("tmpfs 2056 MiB, 1040 MiB filler after 1 s: start=%1 errors=%2 '%3' self_stopped=%4 after %5 ms, enc=%6 drop=%7, file %8 s, dec_err=%9")
                         .arg(r2["start_ret"], r2["errors"], m2.value(0), r2["self_stopped"], r2["t_stop_after_fill_ms"], r2["enc"], r2["drop"],
                              r2["file_dur"], r2["dec_err"]));
            c.check(r2["start_ret"] == "1", "start accepted with >= 2 GB free");
            c.check(r2["errors"] == "1" && m2.value(0).contains("almost full", Qt::CaseInsensitive), "errorOccurred('disk is almost full') below 1 GB free");
            c.check(r2["self_stopped"] == "1" && r2["t_stop_after_fill_ms"].toDouble() <= 3000, "recording stopped by itself within 3 s of crossing 1 GB");
            c.check(r2["file_opened"] == "1" && r2["dec_err"] == "0" && r2["file_dur"].toDouble() >= 1.0, "saved file playable");
        }
        QDir(mnt).removeRecursively();
        c.finish();
    }

    // 13 ------------------------------------------------------------------------------------------
    void formatChangeSegment_data()
    {
        QTest::addColumn<int>("w1"); QTest::addColumn<int>("h1"); QTest::addColumn<int>("f1");
        QTest::addColumn<int>("w2"); QTest::addColumn<int>("h2"); QTest::addColumn<int>("f2");
        const int U = int(RawPixelFormat::UYVY), B = int(RawPixelFormat::BGRA);
        QTest::newRow("1080p_UYVY_to_720p_UYVY") << 1920 << 1080 << U << 1280 << 720 << U;
        QTest::newRow("720p_UYVY_to_1080p_UYVY") << 1280 << 720 << U << 1920 << 1080 << U;
        QTest::newRow("1080p_UYVY_to_720p_BGRA") << 1920 << 1080 << U << 1280 << 720 << B;
    }
    void formatChangeSegment()
    {
        QFETCH(int, w1); QFETCH(int, h1); QFETCH(int, f1); QFETCH(int, w2); QFETCH(int, h2); QFETCH(int, f2);
        Checks c(QString("formatChangeSegment/%1").arg(QTest::currentDataTag()));
        Spec a; a.w = w1; a.h = h1; a.fmt = RawPixelFormat(f1);
        Spec b; b.w = w2; b.h = h2; b.fmt = RawPixelFormat(f2);
        Rig g(a);
        const QString base = out(QString("seg_%1.mp4").arg(QTest::currentDataTag()));
        const QString part2 = out(QString("seg_%1_part2.mp4").arg(QTest::currentDataTag()));
        QFile::remove(part2);
        Run r;
        QVERIFY2(beginExact(g.rec, g.sim, g.rv, base, r), r.err.toUtf8().constData());
        pump(3000);
        g.setPattern(b);
        pump(3000);
        endExact(g.rec, g.sim, g.rv, r);
        std::vector<PushEntry> la, lb;
        for (auto& e : r.log) (e.w == w1 && e.fmt == a.fmt ? la : lb).push_back(e);
        FileInfo fa = analyze(base, true, true), fb = analyze(part2, true, true);
        printFile("seg1", fa);
        printFile("seg2", fb);
        c.metric(QString("fed %1 (%2 + %3) encoded=%4 dropped=%5 segmentStarted=[%6] errors=[%7] stop path=%8; part1 %9 pkts %10x%11 %12 s; part2 %13 pkts %14x%15 %16 s")
                     .arg(r.log.size()).arg(la.size()).arg(lb.size()).arg(r.enc).arg(r.drop).arg(g.rv.segments.join(","), g.rv.errors.join("|"), QFileInfo(r.stopPath).fileName())
                     .arg(fa.pkts.size()).arg(fa.w).arg(fa.h).arg(F(fa.lastEndS)).arg(fb.pkts.size()).arg(fb.w).arg(fb.h).arg(F(fb.lastEndS)));
        c.check(g.rv.errors.isEmpty(), "no errorOccurred: " + g.rv.errors.join("|"));
        c.check(g.rv.segments.size() == 1 && g.rv.segments.value(0) == part2, "segmentStarted(<name>_part2.mp4) emitted once");
        c.check(r.stopPath == part2, "recordingStopped names the last segment");
        c.check(r.enc == (qint64)r.log.size() && r.drop == 0, QString("encoded == fed, 0 dropped (%1/%2/%3)").arg(r.enc).arg(r.log.size()).arg(r.drop));
        c.check(fa.opened && fa.w == w1 && fa.h == h1 && fa.pkts.size() == la.size() && fa.decErr == 0 && timelineMismatches(fa, la, 2000) == 0,
                QString("part 1: %1x%2, all %3 frames, exact timeline").arg(w1).arg(h1).arg(la.size()));
        c.check(fb.opened && fb.w == w2 && fb.h == h2 && fb.pkts.size() == lb.size() && fb.decErr == 0 && timelineMismatches(fb, lb, 2000) == 0,
                QString("part 2: %1x%2, all %3 frames, exact timeline (got %4 pkts %5x%6 '%7')").arg(w2).arg(h2).arg(lb.size()).arg(fb.pkts.size()).arg(fb.w).arg(fb.h).arg(fb.err));
        int badA = 0, badB = 0;
        for (size_t k = 0; k < std::min(fa.idx.size(), la.size()); ++k) badA += fa.idx[k] != la[k].slot;
        for (size_t k = 0; k < std::min(fb.idx.size(), lb.size()); ++k) badB += fb.idx[k] != lb[k].slot;
        c.check(badA == 0 && badB == 0 && !fb.idx.empty(), QString("decoded content matches fed frames (%1 / %2 mismatches)").arg(badA).arg(badB));
        rm(base); rm(part2);
        c.finish();
    }

    // 14 ------------------------------------------------------------------------------------------
    void restartWhileFinalising()
    {
        Checks c("restartWhileFinalising");
        Rig g;
        const QString p1 = out("restart1.mp4"), p2 = out("restart2.mp4");
        Run r;
        QVERIFY2(beginExact(g.rec, g.sim, g.rv, p1, r), r.err.toUtf8().constData());
        pump(2000);
        const size_t st0 = g.rv.stops.size();
        auto t0 = Clock::now();
        g.rec.stopRecording();
        QString err;
        bool again = g.rec.startRecording(p2, &err);
        const double tAgain = msSince(t0);
        bool stopped = waitFor([&] { return g.rv.stops.size() > st0; }, 10000);
        const double fin = msSince(t0);
        c.metric(QString("start %1 ms after stop -> %2 '%3'; recordingStopped after %4 ms").arg(F(tAgain, 3)).arg(again).arg(err).arg(F(fin, 0)));
        c.check(!again && err.contains("being saved"), "start during finalisation refused with 'still ... being saved': '" + err + "'");
        c.check(stopped && g.rv.stops.back().path == p1, "first recording finalised");
        err.clear();
        const size_t sg0 = g.rv.started.size();
        bool ok = g.rec.startRecording(p2, &err);
        c.check(ok && waitFor([&] { return g.rv.started.size() > sg0; }, 5000), "start after recordingStopped succeeds: " + err);
        pump(1000);
        const size_t st1 = g.rv.stops.size();
        g.rec.stopRecording();
        waitFor([&] { return g.rv.stops.size() > st1; }, 10000);
        FileInfo f1 = analyze(p1, true), f2 = analyze(p2, true);
        c.check(f1.opened && f1.decErr == 0 && f1.lastEndS > 1.9 && f2.opened && f2.decErr == 0 && f2.lastEndS > 0.9,
                QString("both files playable (%1 s, %2 s)").arg(F(f1.lastEndS)).arg(F(f2.lastEndS)));
        rm(p1); rm(p2);
        c.finish();
    }

    // 15 ------------------------------------------------------------------------------------------
    void pushFrameCost()
    {
        Checks c("pushFrameCost");
        const int secs = envInt("PUSHCOST_SECONDS", 10);
        Rig g;
        const QString path = out("pushcost.mp4");
        // Not recording: bookkeeping-only path
        g.sim.clearLog(); g.sim.counting = true; g.sim.recordCost = true;
        pump(1000);
        g.sim.pause(); g.sim.counting = false;
        Pct idle = pct(g.sim.takeCost());
        g.sim.resume();
        Run r;
        QVERIFY2(beginExact(g.rec, g.sim, g.rv, path, r), r.err.toUtf8().constData());
        pump(secs * 1000);
        endExact(g.rec, g.sim, g.rv, r);
        g.sim.recordCost = false;
        Pct p = pct(r.costUs);
        c.metric(QString("recording 1080p UYVY (4147200 B/frame): n=%1 median=%2 ms p99=%3 ms max=%4 ms mean=%5 ms; idle n=%6 median=%7 ms max=%8 ms; dropped=%9")
                     .arg(p.n).arg(F(p.med / 1000, 3)).arg(F(p.p99 / 1000, 3)).arg(F(p.max / 1000, 3)).arg(F(p.mean / 1000, 3))
                     .arg(idle.n).arg(F(idle.med / 1000, 4)).arg(F(idle.max / 1000, 4)).arg(r.drop));
        c.check(p.n >= size_t(secs * 55), QString("enough samples (%1)").arg(p.n));
        c.timing(p.med < 2000, QString("median < 2 ms (%1 ms)").arg(F(p.med / 1000, 3)));
        c.timing(p.p99 < 5000, QString("p99 < 5 ms (%1 ms)").arg(F(p.p99 / 1000, 3)));
        c.timing(p.max < 16000, QString("max < 16 ms, one frame period (%1 ms)").arg(F(p.max / 1000, 3)));
        c.check(r.drop == 0 && r.enc == (qint64)r.log.size(), "no drops while measuring");
        rm(path);
        c.finish();
    }

    // 16 ------------------------------------------------------------------------------------------
    void longRun()
    {
        const int minutes = envInt("LONG_RUN_MIN", 0);
        if (minutes <= 0) QSKIP("set LONG_RUN_MIN=<minutes> to run");
        Checks c("longRun");
        Rig g;
        const QString path = out("longrun.mp4");
        QFile csv(qEnvironmentVariable("LONG_RUN_CSV", resultsDir() + "/longrun.csv"));
        csv.open(QIODevice::WriteOnly | QIODevice::Truncate);
        csv.write("t_s,rss_kb,hwm_kb,threads,fds,gpu_pid_mib,gpu_dev_mib,fed,drop_signal_total,file_bytes\n");
        Run r;
        QVERIFY2(beginExact(g.rec, g.sim, g.rv, path, r), r.err.toUtf8().constData());
        std::vector<std::pair<double, Sample>> samples;
        auto t0 = Clock::now();
        while (msSince(t0) < minutes * 60000.0) {
            pump(10000);
            Sample s = sampleNow();
            double t = msSince(t0) / 1000;
            samples.push_back({t, s});
            csv.write(QString("%1,%2,%3,%4,%5,%6,%7,%8,%9,%10\n").arg(F(t, 1)).arg(s.rssKB).arg(s.hwmKB).arg(s.threads).arg(s.fds).arg(s.gpuPidMiB)
                          .arg(s.gpuDevMiB).arg(g.sim.counted.load()).arg(g.rv.dropSig.empty() ? 0 : g.rv.dropSig.back()).arg(QFileInfo(path).size()).toUtf8());
            csv.flush();
        }
        endExact(g.rec, g.sim, g.rv, r);
        csv.close();
        FileInfo fi = analyze(path, false);
        printFile("longrun", fi);
        size_t ref = 0;
        while (ref + 1 < samples.size() && samples[ref].first < 60) ref++;
        const Sample a = samples[ref].second, b = samples.back().second;
        c.metric(QString("%1 min: fed=%2 encoded=%3 dropped=%4 file %5 s; RSS %6 -> %7 KB, GPU/pid %8 -> %9 MiB, fds %10 -> %11, threads %12 -> %13 (from t=%14 s)")
                     .arg(minutes).arg(r.log.size()).arg(r.enc).arg(r.drop).arg(F(fi.lastEndS)).arg(a.rssKB).arg(b.rssKB).arg(a.gpuPidMiB).arg(b.gpuPidMiB)
                     .arg(a.fds).arg(b.fds).arg(a.threads).arg(b.threads).arg(F(samples[ref].first, 0)));
        commonChecks(c, r, fi, 2000, 1920, 1080);
        c.check(b.rssKB - a.rssKB <= 20 * 1024, "RSS growth <= 20 MB");
        c.check(b.gpuPidMiB - a.gpuPidMiB <= 16, "GPU growth <= 16 MiB");
        c.check(b.fds <= a.fds && b.threads <= a.threads, "fds / threads flat");
        rm(path);
        c.finish();
    }

    // 17 ------------------------------------------------------------------------------------------
    void realisticBitrate()
    {
        Checks c("realisticBitrate");
        const int secs = envInt("BITRATE_SECONDS", 10);
        NoisePattern noise(1920, 1080, RawPixelFormat::UYVY, 16, 12); // must outlive the feeder
        Rig g;
        g.sim.setSource(Spec{}, [&](int64_t s) { return noise.frame(s); });
        const QString path = out("bitrate.mp4");
        Run r;
        QVERIFY2(beginExact(g.rec, g.sim, g.rv, path, r), r.err.toUtf8().constData());
        pump(secs * 1000);
        endExact(g.rec, g.sim, g.rv, r);
        FileInfo fi = analyze(path, true);
        printFile("bitrate", fi);
        std::map<int64_t, int64_t> perSec;
        for (auto& p : fi.pkts) perSec[(p.pts - fi.pkts.front().pts) / 120000] += p.size;
        double maxSec = 0, minSec = 1e9;
        for (auto& kv : perSec) if (kv.first < (int64_t)perSec.size() - 1) { maxSec = std::max(maxSec, kv.second * 8 / 1e6); minSec = std::min(minSec, kv.second * 8 / 1e6); }
        const double target = 1920.0 * 1080 * 60 * 0.13 / 1e6;
        c.metric(QString("avg bitrate %1 Mbps (configured target %2, maxrate %3); per-second min %4 max %5 Mbps; fed=%6 encoded=%7 dropped=%8 max feeder lateness %9 ms")
                     .arg(F(fi.bitrateMbps, 2)).arg(F(target, 2)).arg(F(target * 1.25, 2)).arg(F(minSec, 2)).arg(F(maxSec, 2))
                     .arg(r.log.size()).arg(r.enc).arg(r.drop).arg(F(r.lateMaxMs, 1)));
        commonChecks(c, r, fi, 2000, 1920, 1080, false);
        c.timing(r.drop == 0, QString("encoder keeps up with noisy 1080p60: 0 dropped (got %1)").arg(r.drop));
        c.check(fi.decErr == 0, "decodes cleanly");
        c.check(fi.bitrateMbps >= target * 0.75 && fi.bitrateMbps <= target * 1.25, QString("average bitrate within 75..125% of target (%1 Mbps)").arg(F(fi.bitrateMbps, 2)));
        rm(path);
        c.finish();
    }

    // ------------------------------------------------------------------------------------------ extra
    // Fresh process that initialises the CUDA runtime (cudaFree(0)) before the first recording
    void cudaRuntimeFirst()
    {
        Checks c("cudaRuntimeFirst");
        Child ch = spawnSelf({"cudartfirst", out("cudartfirst.mp4").toStdString()});
        QString o = readChild(ch, 60000, "REPORT");
        o += readChild(ch, 1000);
        int st = reap(ch, 10000);
        printf("%s", o.toUtf8().constData());
        auto rep = parseReport(o);
        c.metric(QString("child %1: cudaFree(0)=%2, startRecording()=%3, recordingStarted=%4, encoded=%5, error='%6'")
                     .arg(waitStr(st), rep["cudaFree"], rep["start_ret"], rep["started"], rep["enc"], childMsgs(o).value(0)));
        c.check(rep["cudaFree"] == "0", "CUDA runtime initialised");
        c.check(rep["started"] == "1" && rep["enc"].toLongLong() > 0, "recording starts although the CUDA runtime was initialised first");
        c.finish();
    }


    // Capture clock goes backwards with the same layout (DeckLink StopStreams/StartStreams, e.g. frame-rate change)
    void streamClockReset()
    {
        Checks c("streamClockReset");
        Rig g;
        const QString path = out("clockreset.mp4");
        Run r;
        QVERIFY2(beginExact(g.rec, g.sim, g.rv, path, r), r.err.toUtf8().constData());
        pump(3000);
        const int64_t before = g.sim.counted.load();
        g.sim.resetSlotTo = 0; // stream time restarts at 0
        pump(3000);
        endExact(g.rec, g.sim, g.rv, r);
        FileInfo fi = analyze(path, false);
        printFile("clockreset", fi);
        c.metric(QString("fed %1 before + %2 after the clock reset; encoded=%3 dropped=%4 file_packets=%5 file_dur=%6 s")
                     .arg(before).arg((int64_t)r.log.size() - before).arg(r.enc).arg(r.drop).arg(fi.pkts.size()).arg(F(fi.lastEndS)));
        c.check(r.enc >= (qint64)r.log.size() - 1, QString("frames after a capture-clock reset are still recorded (encoded %1 of %2 fed)").arg(r.enc).arg(r.log.size()));
        c.check(fi.opened && fi.lastEndS >= 5.5, QString("file covers the whole recording (%1 s of ~6 s)").arg(F(fi.lastEndS)));
        rm(path);
        c.finish();
    }

    void stopDuringInit()
    {
        Checks c("stopDuringInit");
        Rig g;
        const QString path = out("stopinit.mp4");
        QString err;
        QVERIFY(g.rec.startRecording(path, &err));
        g.rec.stopRecording(); // before recordingStarted
        bool stopped = waitFor([&] { return !g.rv.stops.empty(); }, 10000);
        pump(300);
        c.metric(QString("signal order: %1; isRecording=%2").arg(g.rv.order.join(",")).arg(g.rec.isRecording()));
        c.check(stopped && g.rv.stops.size() == 1, "exactly one recordingStopped");
        c.check(!g.rec.isRecording(), "not recording afterwards");
        c.check(g.rv.errors.isEmpty(), "no error");
        Run r;
        bool ok = beginExact(g.rec, g.sim, g.rv, out("stopinit2.mp4"), r);
        c.check(ok, "next start works: " + r.err);
        if (ok) { pump(500); endExact(g.rec, g.sim, g.rv, r); c.check(r.enc == (qint64)r.log.size() && r.drop == 0, "next recording complete"); }
        rm(path); rm(out("stopinit2.mp4"));
        c.finish();
    }

    void destroyWhileRecording()
    {
        Checks c("destroyWhileRecording");
        const QString path = out("destroy.mp4");
        auto rec = std::make_unique<VideoRecorder>();
        auto rv = std::make_unique<Recv>(*rec);
        auto sim = std::make_unique<CaptureSim>(*rec);
        MovingPattern pat(1920, 1080, RawPixelFormat::UYVY);
        sim->setSource(Spec{}, [&](int64_t s) { return pat.frame(s); });
        sim->start();
        pump(100);
        Run r;
        QVERIFY2(beginExact(*rec, *sim, *rv, path, r), r.err.toUtf8().constData());
        pump(2000);
        sim->pause();
        sim->counting = false;
        auto log = sim->takeLog();
        sim->stop();
        auto t0 = Clock::now();
        rec.reset(); // application shutdown while recording
        const double delMs = msSince(t0);
        FileInfo fi = analyze(path, true);
        printFile("destroy", fi);
        c.metric(QString("destructor took %1 ms; fed=%2 file packets=%3").arg(F(delMs, 0)).arg(log.size()).arg(fi.pkts.size()));
        c.check(fi.opened && fi.decErr == 0 && fi.pkts.size() == log.size(), "every fed frame is in the finalised file");
        c.timing(delMs < 2000, "destructor finalises within 2 s");
        rm(path);
        c.finish();
    }
};

int main(int argc, char** argv)
{
    if (argc >= 3 && !strcmp(argv[1], "--child")) return childMain(argc, argv);
    qputenv("QT_QPA_PLATFORM", "offscreen");
    if (!qEnvironmentVariableIsSet("QTEST_FUNCTION_TIMEOUT")) qputenv("QTEST_FUNCTION_TIMEOUT", "14400000"); // longRun
    QCoreApplication app(argc, argv);
    qInstallMessageHandler(msgHandler);
    setvbuf(stdout, nullptr, _IOLBF, 0);
    TstVideoRecorder tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "tst_videorecorder.moc"
