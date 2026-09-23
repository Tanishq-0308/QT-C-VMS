#pragma once
// Diagnostic probes for the live-latency / leak investigation.
// Compiled in ONLY when MEDAPP_DIAG is defined (e.g. cmake -DCMAKE_CXX_FLAGS=-DMEDAPP_DIAG).
// Without it every DIAG_* macro expands to nothing, so production builds are unchanged.
//
// Once per second a background thread prints to stderr:
//   [DIAG] t=<epoch_ms> inflight=<n> emits=<n/s> recv=<n/s> lat_avg_ms= lat_max_ms=
//          paint_n= paint_avg_ms= paint_max_ms= gui_lag_max_ms= rec_queue= rec_drops= rec_grab_avg_ms= rec_grab_max_ms=
// in_fps    = frames per second delivered by the DeckLink capture callback (true input rate)
// inflight  = frames emitted by DrawFrame (x receivers) not yet consumed by setFrame -> GUI backlog
// lat_*     = time from DrawFrame (DeckLink thread) to setFrame (GUI thread)
// gui_lag   = how late a 50 ms GUI-thread QTimer fires (event-loop saturation)

#ifdef MEDAPP_DIAG

#include <QObject>
#include <QTimer>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace diag {

using Clock = std::chrono::steady_clock;

inline int64_t nowUs()
{
    return std::chrono::duration_cast<std::chrono::microseconds>(Clock::now().time_since_epoch()).count();
}

// Lock-free "max" update
inline void atomicMax(std::atomic<int64_t>& a, int64_t v)
{
    int64_t cur = a.load(std::memory_order_relaxed);
    while (v > cur && !a.compare_exchange_weak(cur, v, std::memory_order_relaxed)) {}
}

struct Stat {
    std::atomic<int64_t> n{0}, sumUs{0}, maxUs{0};
    void add(int64_t us) { n++; sumUs += us; atomicMax(maxUs, us); }
    void takeAndReset(int64_t& outN, double& avgMs, double& maxMs)
    {
        outN = n.exchange(0);
        int64_t s = sumUs.exchange(0);
        int64_t m = maxUs.exchange(0);
        avgMs = outN ? (s / 1000.0) / outN : 0.0;
        maxMs = m / 1000.0;
    }
};

class Probe {
public:
    static Probe& instance() { static Probe p; return p; }

    // DeckLink thread, once per emitted frame
    void frameEmitted(const void* frame, int receivers)
    {
        m_inflight += receivers;
        m_emits++;
        std::lock_guard<std::mutex> lk(m_mapMutex);
        // The frame pointer cannot be reused by the DeckLink pool while a queued event still
        // holds a reference, so keying by pointer is safe for in-flight frames.
        m_emitTime[frame] = nowUs();
    }

    // GUI thread, in each receiving widget's setFrame
    void frameReceived(const void* frame)
    {
        ensureGuiTimer();
        m_inflight--;
        m_recv++;
        int64_t t0 = 0;
        {
            std::lock_guard<std::mutex> lk(m_mapMutex);
            auto it = m_emitTime.find(frame);
            if (it != m_emitTime.end()) t0 = it->second;
        }
        if (t0) m_lat.add(nowUs() - t0);
    }

    void inputFrame() { m_inputFrames++; } // DeckLink capture callback (every captured frame)
    void paintDone(int64_t us) { m_paint.add(us); }
    void recGrabDone(int64_t us) { m_recGrab.add(us); }
    void recQueue(int depth) { m_recQueue = depth; }
    void recDrop() { m_recDrops++; }

private:
    Probe() : m_thread([this] { run(); }) { m_thread.detach(); }

    void ensureGuiTimer()
    {
        // Created lazily on the GUI thread (first setFrame call).
        if (m_guiTimerStarted.exchange(true)) return;
        auto* t = new QTimer(); // intentionally lives for the process lifetime
        t->setTimerType(Qt::PreciseTimer);
        m_lastTick = nowUs();
        QObject::connect(t, &QTimer::timeout, [this] {
            int64_t n = nowUs();
            atomicMax(m_guiLagMaxUs, (n - m_lastTick) - 50000);
            m_lastTick = n;
        });
        t->start(50);
    }

    void run()
    {
        for (;;) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            int64_t latN, paintN, grabN;
            double latAvg, latMax, paintAvg, paintMax, grabAvg, grabMax;
            m_lat.takeAndReset(latN, latAvg, latMax);
            m_paint.takeAndReset(paintN, paintAvg, paintMax);
            m_recGrab.takeAndReset(grabN, grabAvg, grabMax);
            int64_t epochMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                  std::chrono::system_clock::now().time_since_epoch()).count();
            std::fprintf(stderr,
                         "[DIAG] t=%lld in_fps=%lld inflight=%d emits=%lld recv=%lld lat_avg_ms=%.2f lat_max_ms=%.2f "
                         "paint_n=%lld paint_avg_ms=%.2f paint_max_ms=%.2f gui_lag_max_ms=%.2f "
                         "rec_queue=%d rec_drops=%lld rec_grab_n=%lld rec_grab_avg_ms=%.2f rec_grab_max_ms=%.2f\n",
                         (long long)epochMs, (long long)m_inputFrames.exchange(0), m_inflight.load(), (long long)m_emits.exchange(0),
                         (long long)m_recv.exchange(0), latAvg, latMax, (long long)paintN, paintAvg, paintMax,
                         std::max<int64_t>(0, m_guiLagMaxUs.exchange(0)) / 1000.0, m_recQueue.load(),
                         (long long)m_recDrops.load(), (long long)grabN, grabAvg, grabMax);
            std::fflush(stderr);
        }
    }

    std::atomic<int> m_inflight{0};
    std::atomic<int64_t> m_emits{0}, m_recv{0}, m_inputFrames{0};
    std::mutex m_mapMutex;
    std::unordered_map<const void*, int64_t> m_emitTime; // bounded by DeckLink frame-pool size
    Stat m_lat, m_paint, m_recGrab;
    std::atomic<int> m_recQueue{0};
    std::atomic<int64_t> m_recDrops{0};
    std::atomic<bool> m_guiTimerStarted{false};
    int64_t m_lastTick = 0; // GUI thread only
    std::atomic<int64_t> m_guiLagMaxUs{0};
    std::thread m_thread;
};

struct ScopedTimer {
    void (Probe::*fn)(int64_t);
    int64_t t0 = nowUs();
    explicit ScopedTimer(void (Probe::*f)(int64_t)) : fn(f) {}
    ~ScopedTimer() { (Probe::instance().*fn)(nowUs() - t0); }
};

} // namespace diag

#define DIAG_FRAME_EMITTED(frame, receivers) diag::Probe::instance().frameEmitted((frame), (receivers))
#define DIAG_FRAME_RECEIVED(frame)           diag::Probe::instance().frameReceived((frame))
#define DIAG_INPUT_FRAME()                   diag::Probe::instance().inputFrame()
#define DIAG_SCOPE_PAINT()                   diag::ScopedTimer diagPaintTimer_(&diag::Probe::paintDone)
#define DIAG_SCOPE_REC_GRAB()                diag::ScopedTimer diagRecGrabTimer_(&diag::Probe::recGrabDone)
#define DIAG_REC_QUEUE(depth)                diag::Probe::instance().recQueue((depth))
#define DIAG_REC_DROP()                      diag::Probe::instance().recDrop()

#else

#define DIAG_FRAME_EMITTED(frame, receivers) do {} while (0)
#define DIAG_FRAME_RECEIVED(frame)           do {} while (0)
#define DIAG_INPUT_FRAME()                   do {} while (0)
#define DIAG_SCOPE_PAINT()                   do {} while (0)
#define DIAG_SCOPE_REC_GRAB()                do {} while (0)
#define DIAG_REC_QUEUE(depth)                do {} while (0)
#define DIAG_REC_DROP()                      do {} while (0)

#endif
