# medical_qt_app — Test & Diagnosis Findings

**Date:** 2026-09-22 · **Code:** current `main` checkout · **Machine:** RTX A400 4 GB, DeckLink Mini Recorder 4K, 7 GB RAM, driver 580.82
**Scope:** static audit of all app code, automated tests (4 suites), sanitizers (ASan/LSan/UBSan, TSan, compute-sanitizer), live memory sampling of the running app.
No production behaviour was changed. The opt-in `MEDAPP_DIAG` probes compile to nothing in normal builds.

**Evidence key**
- **M** = measured by a test or sanitizer.
- **L** = observed on the live app.
- **S** = static code review only; needs a probe or a manual run to confirm.

---

## 1. Answer to the two main questions

### Is there a memory leak causing the slowdown?
**No, not on the live-view path.** The running app (pid 11490, `build/`) was sampled every 10 s for 23 minutes while live:

| Metric | t=0 | t=23 min | Change |
|---|---|---|---|
| Anonymous heap | 635,656 KB | 635,452 KB | −0.2 MB |
| RSS | 774,012 KB | 770,500 KB | −3.4 MB |
| Threads / fds | 13 / 43 | 13 / 43 | 0 |
| GPU memory | 66 MiB | 66 MiB | 0 |

The data is in `tests/results_live_pid11490.csv` (138 samples). Memory **does** leak elsewhere, but only through user actions or failures:
- dialogs that are never deleted (§3)
- recorder init failures (§2, F-05)

None of these leak continuously while you watch the stream.

### Why does live latency grow to ~2 s?
It is almost certainly **frames piling up on the GUI thread**, not memory growth. The code has every ingredient for it:
1. **Every DeckLink frame is posted to the GUI thread through an unbounded `Qt::QueuedConnection`** ([DeckLinkOpenGLWidget.cpp:111,132,334](../decklink/DeckLinkOpenGLWidget.cpp)).
   - It goes to 2 receivers: Dashboard and Recording.
   - One more unused event per frame is posted to HomePage ([DeckLinkInputDevice.cpp](../decklink/DeckLinkInputDevice.cpp) `VideoInputFrameArrived`).
   - Nothing drops stale frames, so if the GUI thread ever falls behind, the screen shows old frames.
2. **`paintGL` does a full GPU→CPU→GPU round trip on every frame** ([DeckLinkOpenGLWidget.cpp:228-254](../decklink/DeckLinkOpenGLWidget.cpp)):
   - `grabFramebuffer()` is a blocking `glReadPixels`. It waits on the GPU, so it does *not* show as CPU; the GUI thread measured only ~23–26 % CPU.
   - Then `mirrored()` makes a full-frame CPU copy.
   - Then `QPainter::drawImage()` re-uploads it.
   - This runs even when flip = 0.
3. **The capture is fixed at 4K (`bmdMode4K2160p30`)** ([HomePage.cpp:452,481](../ui/home/HomePage.cpp)).
   - Every discovered device or input feeds the same delegate.
4. **The build is unoptimised.** `CMAKE_BUILD_TYPE` is empty, and the instance you run is `build/`.
5. **While recording, the GUI thread does more per frame**: a second forced render plus readback, and the frame is scaled on the GUI thread (§2).

**Status: this is S (static review) plus indirect L; direct proof is pending.** The `build-diag` binary prints `inflight` (frames queued, not yet consumed) and `lat_avg_ms` (DeckLink callback → GUI) every second. If both climb while the delay grows, the cause is confirmed. That run needs the DeckLink card, so your current instance must be closed first (§6).

---

## 2. Findings, ranked

Severity levels:
- **Critical**: patient-safety or data-loss risk in theatre.
- **High**: serious malfunction.
- **Medium**: leak, integrity or robustness issue.
- **Low**: quality.

| ID | Sev | Finding | Evidence | Location |
|---|---|---|---|---|
| F-01 | **Critical** | **Recordings are lost on crash or power loss.** The MP4 is not fragmented, and the moov atom is only written at a clean stop. After SIGKILL at 5 s, the 3.9 MB file gives "moov atom not found" and is unplayable. | M (tst_videorecorder crashMidRecording) | [VideoRecorder.cpp](../ui/Recording/VideoRecorder.cpp) `avformat_write_header(…, nullptr)` |
| F-02 | **Critical** | **Disk-full is silent.** A write error (EFBIG) occurred at 2 s, yet 0 errors were emitted, `isRecording()` stayed true, and the file is unplayable. | M (diskFull) | VideoRecorder.cpp write/trailer calls unchecked |
| F-03 | **Critical** | **The UI shows "Recording" even when the encoder failed to start.** `startRecording()` returns void. `RecordingPage` doesn't connect `errorOccurred` or check `isRecording()`. It starts its timers and inserts a DB row for a file that doesn't exist. | M (initFailure) + S | [RecordingPage.cpp](../ui/Recording/RecordingPage.cpp) `onToggleRecording` |
| F-04 | **Critical** | **The live view can silently show stale video.** The frame backlog is unbounded and nothing detects or drops stale frames (see §1). | S, probe pending | DeckLinkOpenGLWidget.cpp:111-132, :228-254 |
| F-05 | **High** | **A failed recorder start leaks the GPU and an NVENC session.** `finalizeFFmpeg` is not called on the failure path. Each failure leaks about **110 MiB of GPU memory**, ~100 MB RSS and ~20 fds. After 10 failures: +771 MiB GPU, +1.03 GB RSS, +177 fds. On a 4 GB GPU a few retries exhaust NVENC and memory. | M (initFailure + LSan: 470 KB in 80 blocks, all via `initializeFFmpegHardware`) | VideoRecorder.cpp:41-43, :100-188 |
| F-06 | **High** | **The recorded video is stale, time-compressed and drops frames.** Feed is 30 fps but the encoder is paced at 20 fps (actually ~17 fps), so the 31-frame queue fills in 2.3 s. Measured: 33 % of frames dropped; frames ~1.55 s old when encoded; the file plays 1.155× too fast; a 3 min recording gives a 2 min 38 s file (−12 %). pts = frame counter, not wall-clock. | M (basicRecord, longRun) | VideoRecorder.hpp:110 (`m_fps=20`), VideoRecorder.cpp:224, :246-273, :325 |
| F-07 | **High** | **Stop Recording freezes the GUI for ~2 s**, because `join()` drains the queue at the paced rate. | M (stop 1800–2028 ms) | VideoRecorder.cpp `stopRecording` |
| F-08 | **High** | **There is no signal-loss indication.** The "no input source" flag is computed then ignored, so a frozen last frame looks live. The overlay always says "Live • 3840×2160". | S | DeckLinkInputDevice.cpp `VideoInputFrameArrived`; HomePage `customEvent`; DeckLinkOpenGLWidget overlay |
| F-09 | **High** | **All inputs and devices go into one preview.** Every discovered device starts capture at 4K30 into the shared delegate. Hotplug leaves orphan captures, and `kRemoveDeviceEvent` is ignored. | S | HomePage.cpp:434-452 |
| F-10 | **High** | **A `QMessageBox` is shown from the DeckLink thread** on a format-change failure. That is a crash risk exactly when the source changes. | S | DeckLinkInputDevice.cpp `VideoInputFormatChanged` |
| F-11 | **High** | **The recording path adds GUI-thread load.** It uses a 33 ms timer that calls `grabFramebuffer()`, which forces an extra render and readback. `recordFrame` converts and scales under a mutex on the GUI thread: 10.4 ms median and 18.9 ms max for a full-screen source. | M (recordFrameCost) | RecordingPage.cpp:172; VideoRecorder.cpp:218-241 |
| F-12 | Medium | **Dialogs are never deleted and accumulate while the page lives.** 100 "Edit Patient" opens add **+144 MB RSS**. The Edit Surgery and image/video preview dialogs grow the object tree by 2,200–4,300 objects per 100 opens. | M (tst_widget_lifetime) | SurgeryDetailsPage.cpp:67; SurgeryRecordingPage.cpp:148, :899, :1031 |
| F-13 | Medium | **5 widgets leak per SettingsPage.** They are created without a parent and never added to a layout. As a result the RTSP and storage fields can't be edited in the UI. | M (LSan) | SettingsPage.cpp:178-184 |
| F-14 | Medium | **The IDeckLink reference leaks.** The ctor AddRefs twice and the destructor is defaulted, so each device instance leaks one reference. | M (tst_comptr) | DeckLinkInputDevice.cpp:51, :60 |
| F-15 | Medium | **`com_ptr::releaseAndGetAddressOf` doesn't null the pointer.** If the callee writes nothing, the object is released twice. The same pattern is used for display-mode iteration. | M (tst_comptr) | com_ptr.h:184-188; DeckLinkInputDevice.cpp:165, :168 |
| F-16 | Medium | **The `settings` table grows by one row on every launch.** The real DB has 16 rows. Settings Save updates *all* rows (no WHERE). Qt reads the first row but Flask reads the last, so Flask sees the default RTSP URL and credentials, not the user's settings. | M (tst_database) | init.sql:152; SettingsPage.cpp:393; app.py:71 |
| F-17 | Medium | **The api/* controllers don't match the schema.** Patient, Surgery, Image, Video and CameraZoom methods always fail. `DatabaseManager` hides the real error as "Parameter count mismatch". | M (tst_database) | api/*; DatabaseManager.cpp:31, :45 |
| F-18 | Medium | **Migrations split on `;`**, which breaks on a literal or trigger containing `;`. | M | main.cpp:81 |
| F-19 | Medium | **USB export copies synchronously on the GUI thread**, freezing live view for multi-GB files. The existing destination is deleted before the copy succeeds. | S | SurgeryRecordingPage.cpp `downloadSelectedFiles` |
| F-20 | Medium | **Snapshots:**<br>• Success is reported before the async save finishes, so a full disk still gives a success toast.<br>• The data is JPEG but the name ends in `.png`.<br>• Names have second resolution, so snapshots taken in the same second overwrite each other. | S | DeckLinkOpenGLWidget.cpp `saveSnapshot`; RecordingPage snapshot |
| F-21 | Medium | **The Flask recorder never drains its `Popen` stdout/stderr pipes**, so ffmpeg stalls after a few minutes if that endpoint is used. The zoom request has no timeout, and Flask retries the ESP32 forever. | S | flask_zoom_api/recorder.py, app.py |
| F-22 | Low | **The build is unoptimised** (`CMAKE_BUILD_TYPE` empty). | L | CMakeLists.txt / build/CMakeCache.txt |
| F-23 | Low | **Dead code:**<br>• an unused `RecordingPage` with its own GL context (mainwindow.cpp)<br>• `d_yuvBuffer` (3 MB) and `m_cudaStream` allocated but unused<br>• `cuda/nv12_to_rgba.cu` not compiled<br>• `nvcuvid` linked but unused<br>• `av_opt_set("bf")` fails unchecked | M / S | VideoRecorder.cpp:79-91, :176-178 |
| F-24 | Low | **The recorder aspect ratio is distorted** (`scaled(IgnoreAspectRatio)` from widget size), and the overlay is burned into the recording. | S | VideoRecorder.cpp:235-237 |

### Security and privacy (must fix before clinical use)

| ID | Sev | Finding | Location |
|---|---|---|---|
| S-01 | **Critical** | **Hard-coded, pre-filled login** (`admin`/`123456`); the users table has no password column. | LoginPage.cpp |
| S-02 | **Critical** | **Flask listens on `0.0.0.0:8001` with no auth.** Endpoints return the camera password and patient PHI, and write to caller-chosen paths. | flask_zoom_api/app.py |
| S-03 | High | **The sudo password is in the source** (`echo 1234 \| sudo -S …`), along with a shutdown key and an RTSP URL with credentials. | StreamConfigController.cpp, SystemController.cpp, init.sql |
| S-04 | High | **Patient data is in the repo:** `sqlite.db`, recordings and reports. The old `test_*` binaries modify and delete rows in the real DB. | repo root |

---

## 3. What was confirmed *not* to be a problem
- **Recorder start/stop ×50:** no GPU growth (44 → 44 MiB), fds and threads flat, every file playable. RSS grew +15.6 MB over 45 cycles, which is under the threshold; watch it in the 4 h soak.
- **compute-sanitizer:** 0 device leaks and 0 invalid accesses. The CUDA_ERROR_INVALID_VALUE reports come from inside the NVIDIA libraries, not app code.
- **ASan/UBSan:** no memory-corruption or UB errors in any suite.
- **TSan:** no race in VideoRecorder code.
- **Other dialogs** (AddPatient, AddSurgery, AddComment, PatientPage, …) are deleted correctly.
- **DeckLink frame refcounting** on the live path is balanced.

---

## 3a. Hardware assessment (4K live + recording)

### Measured system
| Part | Measured | Needed for 4K30 live + record | Verdict |
|---|---|---|---|
| CPU | AMD Ryzen 5 5500, 6C/12T Zen 3, 4.27 GHz, AVX2, no iGPU | Light, if the conversion work runs on the GPU | **OK** |
| GPU | RTX A400 4 GB (Ampere): 1 NVENC encoder, 1 NVDEC decoder, 50 W | NVENC H.264/HEVC 4K30: yes. Pro cards have no session limit | **OK**. 4 GB is fine, but F-05 can exhaust it |
| GPU link | PCIe 2.5 GT/s x8 while idle (it drops to low speed when idle); the device reports up to Gen4 | Readback per frame is ~8 MB at 1080p | OK; recheck under load |
| **Capture card** | DeckLink Mini Recorder 4K; **link runs at PCIe Gen2 x1 (5 GT/s x1); the card supports x4** | 2160p30 8-bit 4:2:2 is **498 MB/s**, and 10-bit is **663 MB/s**. Gen2 x1 carries **~400 MB/s usable** | **FAIL for 4K**. OK for 1080p (1080p60 is 249 MB/s) |
| Capture card mode limit | Mini Recorder 4K accepts up to **2160p30** (HDMI 2.0 / 6G-SDI) | A camera sending 2160p50/60 **cannot be captured** by this card | Check the camera output mode |
| RAM | 7.6 GB. Swap is **1.6 of 2 GB used** (mostly Firefox, VS Code, gnome-shell); the app uses 770 MB | App ~0.8 GB + up to 0.25 GB recorder queue + OS/desktop | **Marginal**. 16 GB dual-channel is recommended. Channel config is unknown (needs `sudo dmidecode -t memory`) |
| Disk | SATA SSD 1 TB (Simm S930P), 827 GB free | 8 Mbps is 3.6 GB/h; true 4K at 40–50 Mbps is ~18–22 GB/h | **OK**: ~36 h of 4K recording free |
| Display | 1920×1080 @ 60 Hz (DP) | A 4K source shown downscaled | OK (not a native 4K view) |
| OS | Ubuntu 22.04.5, kernel 6.8, NVIDIA 580.82 | — | OK |

**H-01 (Critical, hardware).** The DeckLink card has only a **PCIe x1** link. At 4K30 the video stream needs more bandwidth than the link carries. The card has to be in a slot with **≥ x4 electrical lanes**.
- On Ryzen 5 5500 boards, the second x16-size slot is often wired x1 or x4 through the chipset. Check the motherboard manual.
- Until it is moved, any 4K latency or drop result is unreliable.
- The Blackmagic driver confirms the link at boot: `BlackmagicIO: Enabled device "DeckLink Mini Recorder 4K" x1/5 GT/s`.
- The card sits behind the B450 chipset (`00:02.1 → 02:00.2 → 03:04.0 → 06:00.0`). The board is a Gigabyte B450M DS3H V3.
- **The user reports that Blackmagic Media Express shows the same growing delay.** That is strong evidence the drift starts below the app, in the capture link, the driver or the source, and not in our code.
- Confirm by setting the camera to 1080p, or by moving the card to the board's x4 slot. The Media Express drift should disappear.

**H-02 (High, config vs hardware).** The code forces `bmdMode4K2160p30` and records **1080p at 20 fps from a screen grab** (F-06, F-24). The current app therefore does not record 4K, whatever the camera sends. True 4K recording needs:
- frames taken from the DeckLink capture instead of a screen grab
- GPU colour conversion
- NVENC at ~40–50 Mbps H.264, or ~25–35 Mbps HEVC

The A400 can do this.

## 4. Test assets delivered
| Path | Contents |
|---|---|
| `tests/CHECKLIST.md` | Release gate + daily pre-surgery + post-surgery checklist |
| `tests/TEST_CASES.md` | ~90 cases across CAP, LAT, MEM, REC, GPU, SNAP, PLAY, DB, UI, NET, SEC, with parameters and pass thresholds |
| `tests/unit/` | tst_comptr, tst_database, tst_widget_lifetime (QtTest + CTest, plain and ASan); `run_tests.sh all` |
| `tests/unit_gpu/` | tst_videorecorder (10 tests incl. crash, disk-full, 50-cycle leak, long run) + ASan/TSan/compute-sanitizer configs |
| `tests/soak/` | `monitor.sh`, `analyze.py` (HTML report + leak/backlog flags), `run_soak.sh`, `scenarios.md` (S1–S7) |
| `diag/DiagProbe.h` | Latency and backlog probes (`-DMEDAPP_DIAG`). Pristine sources are in `diag/orig/` |
| `build-release`, `build-diag`, `build-asan` | Optimised, probe and sanitizer builds of the app (`build/` untouched) |

---

## 4a. Fix status (QT-C-VMS, uncommitted)

**Target mode:** 1080p60 8-bit YUV for both live view and recording. This is the highest mode the PCIe x1 capture link carries reliably (H-01).

| Area | Findings | Status | Evidence |
|---|---|---|---|
| **A: Live view** | F-04, F-11 (preview part), F-22, F-09 (single capture), per-frame HomePage event | **Fixed**. Details below the table | Builds clean; verified with the DeckLink card: pending |
| **B: Recording** | F-01, F-02, F-03, F-05, F-06, F-07, F-11 (recorder part), F-23 (dead CUDA/d_yuvBuffer), F-24 | **Rewritten**. Details below the table | Smoke test: 600/600 frames at 1080p60, 0 dropped, duration 10.000 s, `startRecording()` and `stopRecording()` 0 ms on the GUI thread, SIGKILL leaves the file playable to 4.98 s, decoded pixels exact. Full suite: running |
| Capture mode | H-02 | Initial capture mode is 1080p60; format detection always requests 8-bit YUV, falling back to the detected format only if the card refuses | — |
| C (partial) | F-10 (widgets from DeckLink thread), RGB-source restart loop | **Fixed** | Camera: detected 2160p30 RGB, captured as 8-bit ARGB, no loop |
| D: Leaks | F-12, F-13, F-14, F-15 | **Fixed**: dialogs deleted after use, Settings widgets parented, extra `IDeckLink` AddRef removed, `com_ptr::release()` nulls the pointer, self-move guarded, safe `releaseAndGetAddressOf()` at QueryInterface call sites, report QNAM freed with a 60 s timeout | tst_widget_lifetime 18/18, tst_comptr 19/19 |
| E: Database | F-16, F-17, F-18 | **Fixed**: 1 settings row (seed only when empty, duplicates collapsed to the lowest id), UPDATE and all reads (Qt, Flask, report) use the same row, real SQL errors reported, quote/comment/trigger-aware migration splitter, controllers match the schema | tst_database 40/42 (2 = CameraZoom, needs a decision) |
| Other | F-19, F-20, F-21 | **Fixed**: USB copy on a worker thread with progress, temp file + fdatasync + atomic replace; snapshots report success only after the write, `.jpg` names with ms timestamps; Flask ffmpeg output to a log file, graceful `q` stop, restart after kill/exit | Fake-ffmpeg test: 20 MB of output, no stall |
| C remaining, Security | F-08 (NO SIGNAL indicator, real overlay), S-01 to S-04 | Not started | — |

**A: Live view changes**
- Only the latest frame is delivered to the GUI.
- Flip is done on the GPU.
- No per-frame readback or re-upload.
- One active capture; device removal is handled.
- Release build by default.

**B: Recording changes**
- Frames are taken straight from the DeckLink capture thread into a 30-buffer pool. A drop is counted and shown to the operator, never silent.
- One GPU upload per frame, then CUDA UYVY→NV12 conversion (with flip), then NVENC 1080p60 at ~16 Mbps VBR.
- Fragmented MP4 with a keyframe every second, plus `fdatasync` every 2 s.
- Timestamps come from the capture clock.
- Start checks (signal present, format, folder writable, 2 GB free disk) run instantly on the GUI thread. The encoder and file open on the recorder thread (~0.37 s), so the live view never stalls. The UI shows "Starting…", and the DB row is created only on `recordingStarted`.
- Errors are shown in the UI, and the disk-full stop happens at 1 GB.
- Stop is non-blocking.
- An input format change continues into a `_partN` file with its own DB row.
- The CUDA 13 `nvcc` is used, matching the linked `libcudart`.

### Bugs found in the new recorder by its test suite (all fixed)
| # | Bug | Fix | Re-test |
|---|---|---|---|
| R-1 | Pool-full drops were counted twice (once when dropped, again as a timestamp gap) | Gaps are counted on the capture side only, so the two counts never overlap | 310 encoded + 161 dropped = 471 pushed; 308 + 53 = 361 |
| R-2 | A capture-clock restart dropped every later frame | The timeline is rebased so it continues after the last frame | 361/361, 0 dropped |
| R-3 | A format change reused a GPU buffer with too small a pitch | The buffer is reallocated when pitch or row count is insufficient | 1080p UYVY → 720p BGRA: 361/361 |
| R-4 | Could not start if CUDA was initialised earlier (primary-context flags) | FFmpeg's own context is used, pushed around the conversion kernel | 61/61 recorded |
| R-5 | A crash could lose up to 3 s (flush every 2 s plus a 1 s fragment) | Fragments every 0.5 s, `flush_packets`, `fdatasync` every 1 s | SIGKILL at 6 s: 5.5 s playable, 0 corrupt frames |
| R-6 | 124 MB frame pool kept allocated between recordings | Freed when a recording ends | — |

The test helper `gpuStall` was changed to stall inside the recorder's own CUDA context, which is now separate (R-4). Other GPU work no longer stalls recording.

## 5. Suggested fix order (for discussion; not started)
1. **Live latency (F-04, F-11, F-22):**
   - Remove the readback, mirror and re-upload from `paintGL`; do the flip in GL.
   - Keep only the latest frame per widget (an atomic "latest frame" slot instead of queued copies).
   - Build as Release.
2. **Recording safety (F-01/02/03/05/06/07):**
   - Use fragmented MP4 (`movflags=frag_keyframe+empty_moov`) or segmented files.
   - Surface errors to the UI.
   - Clean up on failure.
   - Use wall-clock pts and matched fps.
   - Take frames from the DeckLink frame, not the widget.
   - Stop without blocking the GUI.
3. **Signal and device handling (F-08/09/10).**
4. **Leaks and integrity (F-12 to F-18).**
5. **Security (S-01 to S-04).**

## 6. Still to run (needs the DeckLink source and an operator)
- **LAT-02/03:** run `tests/soak/run_soak.sh build-diag/medical_qt_app 60 S2_live60`. This confirms or rules out F-04 with `inflight` and `lat_avg_ms`. Your running `build/` instance must be closed first, because only one process can own the input.
- **LAT-01:** glass-to-glass photos at 0, 5, 15, 30 and 60 min.
- **MEM-03:** the 4 h OT soak. **MEM-05:** an ASan whole-app run (`build-asan`).
- **CAP-\*:** the mode, format and signal-loss matrix on real sources.
- **CPU profiling:** needs `sudo sysctl kernel.perf_event_paranoid=1` for perf and nsys CPU sampling (currently 4).
