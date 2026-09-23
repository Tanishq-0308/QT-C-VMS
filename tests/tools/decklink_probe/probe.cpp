// Read-only DeckLink input probe: reports what the card sees on its HDMI input.
#include "DeckLinkAPI.h"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <thread>

static IDeckLinkInput* g_input = nullptr;
static std::atomic<int> g_frames{0}, g_noSignal{0};
static std::atomic<long> g_lumaSum{0}, g_lumaN{0};
static std::atomic<long long> g_latSumUs{0}, g_latMaxUs{0}, g_latN{0};

static void printMode(const char* what, IDeckLinkDisplayMode* m) {
    const char* name = nullptr; m->GetName(&name);
    BMDTimeValue d; BMDTimeScale s; m->GetFrameRate(&d, &s);
    printf("%s %s  %ldx%ld @ %.3f fps\n", what, name ? name : "?", m->GetWidth(), m->GetHeight(), d ? double(s) / d : 0.0);
    if (name) free((void*)name);
}

struct Cb : public IDeckLinkInputCallback {
    HRESULT QueryInterface(REFIID, LPVOID*) override { return E_NOINTERFACE; }
    ULONG AddRef() override { return 1; }
    ULONG Release() override { return 1; }
    HRESULT VideoInputFormatChanged(BMDVideoInputFormatChangedEvents ev, IDeckLinkDisplayMode* m, BMDDetectedVideoInputFormatFlags f) override {
        static BMDDisplayMode lastMode = 0;
        static int events = 0;
        if (m->GetDisplayMode() == lastMode) { if (++events % 30 == 1) printf("  (ignoring colorspace-only change event #%d)\n", events); return S_OK; }
        lastMode = m->GetDisplayMode();
        printMode("  >> FORMAT DETECTED:", m);
        printf("     flags: %s %s%s%s  events=0x%x\n", (f & bmdDetectedVideoInputRGB444) ? "RGB444" : (f & bmdDetectedVideoInputYCbCr422) ? "YCbCr422" : "?",
               (f & bmdDetectedVideoInput8BitDepth) ? "8-bit" : "", (f & bmdDetectedVideoInput10BitDepth) ? "10-bit" : "", (f & bmdDetectedVideoInput12BitDepth) ? "12-bit" : "", ev);
        g_input->StopStreams();
        const char* pf = getenv("PF");
        BMDPixelFormat fmt = (pf && !strcmp(pf, "rgb10")) ? bmdFormat10BitRGB : (pf && !strcmp(pf, "argb")) ? bmdFormat8BitARGB : bmdFormat8BitYUV;
        HRESULT r = g_input->EnableVideoInput(m->GetDisplayMode(), fmt, bmdVideoInputEnableFormatDetection);
        printf("     re-enable as %s: %s\n", pf ? pf : "yuv8", r == S_OK ? "OK" : "FAILED");
        g_input->StartStreams();
        return S_OK;
    }
    HRESULT VideoInputFrameArrived(IDeckLinkVideoInputFrame* fr, IDeckLinkAudioInputPacket*) override {
        if (!fr) return S_OK;
        g_frames++;
        {   // How late does the frame reach software? (card capture time vs card clock now)
            const BMDTimeScale us = 1000000;
            BMDTimeValue capT = 0, capD = 0, now = 0, inFrame = 0, tpf = 0;
            if (fr->GetHardwareReferenceTimestamp(us, &capT, &capD) == S_OK &&
                g_input->GetHardwareReferenceClock(us, &now, &inFrame, &tpf) == S_OK) {
                long long lat = (long long)(now - capT);
                g_latSumUs += lat; g_latN++;
                long long m = g_latMaxUs.load(); while (lat > m && !g_latMaxUs.compare_exchange_weak(m, lat)) {}
            }
        }
        static BMDPixelFormat seen = 0; if (fr->GetPixelFormat() != seen) { seen = fr->GetPixelFormat(); printf("  frame pixel format now 0x%x, %ldx%ld rowBytes=%ld\n", seen, fr->GetWidth(), fr->GetHeight(), fr->GetRowBytes()); }
        if (fr->GetFlags() & bmdFrameHasNoInputSource) { g_noSignal++; return S_OK; }
        IDeckLinkVideoBuffer* buf = nullptr;
        if (fr->GetPixelFormat() == bmdFormat8BitYUV && fr->QueryInterface(IID_IDeckLinkVideoBuffer, (void**)&buf) == S_OK && buf) {
            if (buf->StartAccess(bmdBufferAccessRead) == S_OK) {
                void* p = nullptr; buf->GetBytes(&p);
                const uint8_t* b = (const uint8_t*)p; long h = fr->GetHeight(), rb = fr->GetRowBytes(), w = fr->GetWidth();
                long sum = 0, n = 0;
                for (long y = h / 8; y < h; y += h / 8) for (long x = 0; x < w; x += w / 16) { sum += b[y * rb + x * 2 + 1]; n++; }
                g_lumaSum += sum; g_lumaN += n;
                buf->EndAccess(bmdBufferAccessRead);
            }
            buf->Release();
        }
        return S_OK;
    }
};

int main(int argc, char** argv) {
    const int seconds = argc > 1 ? atoi(argv[1]) : 12;
    IDeckLinkIterator* it = CreateDeckLinkIteratorInstance();
    if (!it) { printf("DeckLink driver not found\n"); return 1; }
    IDeckLink* dl = nullptr;
    if (it->Next(&dl) != S_OK) { printf("No DeckLink device\n"); return 1; }
    const char* name = nullptr; dl->GetDisplayName(&name); printf("Device: %s\n", name); free((void*)name);

    IDeckLinkStatus* status = nullptr; dl->QueryInterface(IID_IDeckLinkStatus, (void**)&status);
    IDeckLinkConfiguration* cfg = nullptr; dl->QueryInterface(IID_IDeckLinkConfiguration, (void**)&cfg);
    dl->QueryInterface(IID_IDeckLinkInput, (void**)&g_input);
    if (!g_input) { printf("No input interface\n"); return 1; }

    int64_t v = 0; bool locked = false;
    if (status) {
        if (status->GetInt(bmdDeckLinkStatusPCIExpressLinkWidth, &v) == S_OK) printf("PCIe link width: x%lld\n", (long long)v);
        if (status->GetInt(bmdDeckLinkStatusPCIExpressLinkSpeed, &v) == S_OK) printf("PCIe link speed: Gen%lld\n", (long long)v);
        if (status->GetInt(bmdDeckLinkStatusBusy, &v) == S_OK) printf("Device busy flags: 0x%llx (0 = free)\n", (long long)v);
    }
    int64_t conn = 0;
    if (cfg && cfg->GetInt(bmdDeckLinkConfigVideoInputConnection, &conn) == S_OK)
        printf("Selected input connection: %s\n", conn == bmdVideoConnectionHDMI ? "HDMI" : conn == bmdVideoConnectionSDI ? "SDI" : "other");
    // CONN=sdi probes the SDI input; default is HDMI (setting is process-local)
    const char* cn = getenv("CONN");
    const int64_t want = (cn && !strcmp(cn, "sdi")) ? bmdVideoConnectionSDI : bmdVideoConnectionHDMI;
    if (cfg && conn != want)
        printf("Setting input to %s (temporary, process-local): %s\n", want == bmdVideoConnectionSDI ? "SDI" : "HDMI",
               cfg->SetInt(bmdDeckLinkConfigVideoInputConnection, want) == S_OK ? "OK" : "FAILED");

    // EDID=sdr: advertise SDR only on the HDMI input (some converters mis-handle an HDR EDID).
    // Held while this process runs; the card restores its default EDID when released.
    IDeckLinkHDMIInputEDID* edid = nullptr;
    if (const char* e = getenv("EDID")) {
        if (dl->QueryInterface(IID_IDeckLinkHDMIInputEDID, (void**)&edid) == S_OK && edid) {
            int64_t range = !strcmp(e, "sdr") ? bmdDynamicRangeSDR
                          : (bmdDynamicRangeSDR | bmdDynamicRangeHDRStaticPQ | bmdDynamicRangeHDRStaticHLG);
            HRESULT a = edid->SetInt(bmdDeckLinkHDMIInputEDIDDynamicRange, range);
            HRESULT b = edid->WriteToEDID();
            printf("EDID dynamic range set to %s: %s -> replug the HDMI cable / power-cycle the source now\n", e,
                   (a == S_OK && b == S_OK) ? "OK" : "FAILED");
        } else {
            printf("EDID interface not available\n");
        }
    }

    Cb cb; g_input->SetCallback(&cb);
    HRESULT r = g_input->EnableVideoInput(bmdModeHD1080p6000, bmdFormat8BitYUV, bmdVideoInputEnableFormatDetection);
    printf("EnableVideoInput(1080p60, 8-bit YUV, detection): %s\n", r == S_OK ? "OK" : "FAILED (device in use?)");
    if (r != S_OK) return 2;
    g_input->StartStreams();

    for (int s = 1; s <= seconds; ++s) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        bool lk = false; if (status) status->GetFlag(bmdDeckLinkStatusVideoInputSignalLocked, &lk);
        int64_t dm = 0; if (status) status->GetInt(bmdDeckLinkStatusDetectedVideoInputMode, &dm);
        char dmc[5] = {char(dm >> 24), char(dm >> 16), char(dm >> 8), char(dm), 0};
        int f = g_frames.exchange(0), ns = g_noSignal.exchange(0); long ls = g_lumaSum.exchange(0), ln = g_lumaN.exchange(0);
        long long lsum = g_latSumUs.exchange(0), ln2 = g_latN.exchange(0), lmax = g_latMaxUs.exchange(0);
        fflush(stdout); printf("t=%3ds  signal_locked=%s  detected_mode='%s'  frames=%d  no_signal_frames=%d  avg_luma=%s  arrival_delay_ms avg=%.1f max=%.1f\n", s, lk ? "YES" : "no", dm ? dmc : "-", f, ns,
               ln ? std::to_string(ls / ln).c_str() : "-", ln2 ? lsum / 1000.0 / ln2 : -1.0, lmax / 1000.0);
        locked = lk;
    }
    g_input->StopStreams(); g_input->DisableVideoInput(); g_input->SetCallback(nullptr);
    printf("%s\n", locked ? "RESULT: card is receiving a signal" : "RESULT: card does NOT see a signal on the selected input");
    return 0;
}
