# medical_qt_app — Test Case Catalogue

## Conventions
- **Build.** Measurements use `build-release`, or `build-diag` where the case needs `[DIAG]` probe output. Probe output is one line per second on stderr. The fields are described in `diag/DiagProbe.h`.
- **Soak tooling.** Scripts are in `tests/soak/`:
  - `monitor.sh` samples RSS, fds, threads, CPU and GPU.
  - `analyze.py` computes slopes and produces an HTML report.
  - `run_soak.sh` is a wrapper around both.
- **Auto column.** This lists the automated test that covers the case, if any. `manual` means it needs the DeckLink hardware and an operator.
- **Priority.** P1 = patient-safety or data-loss risk. P2 = functional. P3 = quality.
- **Result column.** PASS, FAIL (with the measured value) or BLOCKED.

### Global thresholds
| Metric | Pass criterion |
|---|---|
| RSS / anon memory slope (after 5 min warm-up) | < 5 MB/h |
| fds, threads | constant (±2) after warm-up |
| GPU memory of the process | constant (±8 MiB) |
| `inflight` (queued, unconsumed frames) | ≤ 2 at all times |
| `lat_avg_ms` (DeckLink callback → GUI) | < 50 ms, slope ≈ 0 |
| `paint_avg_ms` | < 16 ms at 30 fps input (half the frame budget) |
| `gui_lag_max_ms` | < 100 ms |
| Glass-to-glass latency | ≤ 150 ms, drift ≤ 50 ms over 60 min |

---

## CAP — DeckLink capture

| ID | P | Parameters | Steps | Expected | Auto | Result |
|---|---|---|---|---|---|---|
| CAP-01 | P1 | Input=SDI | Set Settings → Video input = SDI, save, feed an SDI source | Live image on Dashboard and Recording page | manual | |
| CAP-02 | P1 | Input=HDMI | Same as CAP-01 with HDMI | Live image; overlay shows the **real** mode | manual | |
| CAP-03 | P1 | Source modes: 1080p25, 1080p29.97, 1080p30, 1080p50, 1080p59.94, 1080p60, 1080i50, 1080i59.94, 2160p25, 2160p29.97, 2160p30, 2160p50, 2160p60 | For each mode: start the app with the source in that mode, run 5 min under the diag build | Image shown with correct geometry; `emits` equals the source fps; `lat_avg_ms` < 50 and flat. Note: capture is hard-coded to `bmdMode4K2160p30` (HomePage.cpp:452), so auto-detection must switch modes | manual + diag | |
| CAP-04 | P1 | Pixel formats: 8-bit YUV, 10-bit YUV, 8-bit RGB, 10-bit RGB, 12-bit RGB | Change the source colour space/depth mid-stream | Format auto-detection restarts streams, the image recovers in < 2 s, and no crash occurs | manual | |
| CAP-05 | P1 | Mode change mid-stream (1080p60 → 2160p30 → 1080p50) | Change the source mode while live, and again while recording | Image recovers. No `QMessageBox` from a non-GUI thread (DeckLinkInputDevice.cpp VideoInputFormatChanged). Recording either continues or stops with a clear error | manual | |
| CAP-06 | P1 | Signal loss | Pull the cable for 5 s, 60 s and 10 min, then replug | A visible NO SIGNAL indication, not a frozen last frame. Recovery after replug; `inflight` back to ≤ 2 | manual | |
| CAP-07 | P1 | No signal at boot | Start the app with the cable unplugged, then plug in | Clear state shown, then the live image appears | manual | |
| CAP-08 | P1 | Multiple inputs or cards (Duo/Quad/8K) | Connect 2 sources to 2 inputs | Only the selected input is shown, with no interleaving. Today every device starts capture into one shared delegate (HomePage.cpp addDevice) | manual | |
| CAP-09 | P2 | Hotplug | Unplug a Thunderbolt/PCIe DeckLink, if possible, or restart the driver | No crash; the old capture is stopped; `kRemoveDeviceEvent` is handled | manual | |
| CAP-10 | P2 | No card / driver missing | Start the app on a machine without DeckLink | The app starts and shows a clear "no capture device" message | manual | |
| CAP-11 | P2 | Settings save ×50 (restarts capture each time) | Press Save 50 times, alternating SDI/HDMI | Live every time; RSS, fds and threads flat; no orphaned capture (`emits` doesn't double) | manual + monitor | |
| CAP-12 | P2 | Flip steps 0,1,2,3,4 | Cycle the flip button | Orientation correct at each step; step 4 = none; `paint_avg_ms` recorded per step | manual + diag | |
| CAP-13 | P2 | EDID / HDR source | HDR10 / HLG source over HDMI | Image visible, colours acceptable, no crash | manual | |
| CAP-14 | P1 | IDeckLink refcount | Construct/destroy `DeckLinkInputDevice` with a mock | Refcount returns to baseline (suspected extra AddRef, DeckLinkInputDevice.cpp:60) | tst_comptr | |
| CAP-15 | P2 | com_ptr semantics | copy/move/self-move/releaseAndGetAddressOf | Balanced refcounts; pointer nulled | tst_comptr | |

## LAT — Live latency (the reported fault)

| ID | P | Parameters | Steps | Expected | Auto | Result |
|---|---|---|---|---|---|---|
| LAT-01 | P1 | Glass-to-glass | Show a ms stopwatch on a phone or monitor in front of the camera (or on the source monitor). Photograph the source clock and the app screen together at t = 0, 5, 15, 30 and 60 min | Difference ≤ 150 ms; drift between t=0 and t=60 ≤ 50 ms | manual | |
| LAT-02 | P1 | Probe latency, live only, Dashboard | Scenario S2 with `build-diag` | `inflight` ≤ 2; `lat_avg_ms` flat. If `inflight` climbs, the GUI-thread backlog hypothesis is **confirmed** | diag + analyze.py | |
| LAT-03 | P1 | Probe latency while recording | Scenario S3 | Same as LAT-02; also `rec_grab_avg_ms` and `rec_drops` | diag | |
| LAT-04 | P1 | paintGL cost | From LAT-02/03 logs | `paint_avg_ms` < 16 ms. Suspect: grabFramebuffer, mirrored and drawImage per frame (DeckLinkOpenGLWidget.cpp:228-254) | diag | |
| LAT-05 | P2 | Window size / DPI | Dashboard (small) vs full-screen Recording page; 1080p and 4K monitor; scale 100% / 150% | Latency and paint time within thresholds for each | diag | |
| LAT-06 | P2 | GUI load during live | Open the PDF report; load a patient list with 500 rows; open 500 thumbnails; do a USB copy of a 4 GB file | `gui_lag_max_ms` < 100; `inflight` returns to ≤ 2 within 1 s after the load stops | diag | |
| LAT-07 | P2 | Before/after timeline | `nsys profile` for 120 s at start and 120 s after the drift | Compare GUI-thread time in paintGL, glReadPixels and event processing | nsys | |
| LAT-08 | P3 | Optimisation level | Repeat LAT-02 with the default `build/` (no -O) vs `build-release` | Quantifies the effect of the missing CMAKE_BUILD_TYPE | diag | |

## MEM — Memory and resource leaks

| ID | P | Parameters | Steps | Expected | Auto | Result |
|---|---|---|---|---|---|---|
| MEM-01 | P1 | Live only, 60 min | S2 under `monitor.sh` | RSS/anon slope < 5 MB/h; fds, threads and GPU memory flat | monitor + analyze | |
| MEM-02 | P1 | Recording, 60 min | S3 | Same | monitor + analyze | |
| MEM-03 | P1 | OT-length, 4 h | S4 (live + 30-min recording segments + snapshots every 5 min) | Same; no crash | monitor + analyze | |
| MEM-04 | P1 | Recorder start/stop ×50 | tst_videorecorder startStopCycles | RSS growth < 20 MB and GPU growth < 16 MB between cycle 5 and 50 | tst_videorecorder | |
| MEM-05 | P1 | ASan/LSan whole-app run | `build-asan`: navigate all pages, open all dialogs, record ×3, snapshot ×5, clean exit | No LSan leak in app code | asan | |
| MEM-06 | P2 | Dialog lifetime ×100 | Open/close every dialog 100× | Child-QObject count of the parent returns to baseline. Suspects: `new EditPatientDialog(…, this)` in SurgeryDetailsPage and `new QDialog(this)` in SurgeryRecordingPage | tst_widget_lifetime | |
| MEM-07 | P2 | Page navigation ×200 | Login → home → patient → surgery → surgery-recording → record page → back | RSS and QObject count flat; SurgeryRecordingPage rebuilt without leaking | manual + monitor | |
| MEM-08 | P2 | Report generation ×50 | Generate the PDF report 50× | No QNetworkAccessManager growth (one is created per click) | manual + monitor | |
| MEM-09 | P3 | Heap-churn profile | `perf record -g` for 60 s while live | Top allocations per frame identified: QImage in paintGL, av_frame per frame | perf | |

## REC — Recording / encoding (FFmpeg h264_nvenc)

| ID | P | Parameters | Steps | Expected | Auto | Result |
|---|---|---|---|---|---|---|
| REC-01 | P1 | 10 s recording | tst_videorecorder basicRecord | File opens, h264, 1920×1080; frames written ≈ frames fed; drops = 0 | tst_videorecorder | |
| REC-02 | P1 | Start/stop ×50 | startStopCycles | All files valid; no crash; no leak | tst_videorecorder | |
| REC-03 | P1 | Duration accuracy | Record exactly 10 s, 60 s and 30 min wall-clock | File duration = wall-clock ±1 s. Suspect: pts = frame counter at 20 fps with 30 fps input | tst_videorecorder + manual | |
| REC-04 | P1 | Recording staleness | Record a ms clock in view; compare file frames with the clock | Frame timestamp offset ≤ 200 ms and constant. Suspect: 30-frame queue filled, ~1.5 s stale | manual | |
| REC-05 | P1 | Encoder init failure | Run with the GPU unavailable (CUDA_VISIBLE_DEVICES="") or an unwritable output path | UI shows an error; no "Recording" state; no DB row for a missing file; no leak on retry | tst_videorecorder initFailureThenRestart + manual | |
| REC-06 | P1 | Crash / power loss | SIGKILL 5 s into recording | Footage up to the kill is playable. Today the MP4 is non-fragmented, so it is expected to FAIL | tst_videorecorder crashMidRecording | |
| REC-07 | P1 | Disk full | RLIMIT_FSIZE, or output to a small tmpfs | Error surfaced to the user; recording stops cleanly | tst_videorecorder diskFull | |
| REC-08 | P1 | Long recording, 4 h | S4 | File plays through to the end; seek works; size ≈ 8 Mbps × duration | manual | |
| REC-09 | P2 | recordFrame cost on the GUI thread | recordFrameCost at 1920×1080 and 3572×1844 | < 5 ms per call | tst_videorecorder | |
| REC-10 | P2 | Aspect ratio | Record from a non-16:9 window size | No distortion. Suspect: `scaled(IgnoreAspectRatio)` | manual | |
| REC-11 | P2 | Encoder settings | Probe the output | Bitrate ≈ 8 Mbps CBR, GOP = fps, 0 B-frames; av_opt_set results checked | tst_videorecorder | |
| REC-12 | P2 | Signal loss during recording | Pull the cable during CAP-06 while recording | Recording continues (black or no-signal frames) or stops with an error; the file stays valid | manual | |
| REC-13 | P2 | Input switch during recording | Save Settings while recording | Defined behaviour; no crash; file valid | manual | |
| REC-14 | P2 | Stop latency | Measure the time from Stop to UI response | < 1 s. The join() on the GUI thread drains a 30-frame queue at 20 fps | manual | |
| REC-15 | P2 | Output path and name | Two recordings in the same second; patient id with `/` or spaces | No overwrite; path is sanitised; folder matches the DB row | manual | |
| REC-16 | P3 | Thread safety | TSan on basicRecord | No data race (suspect: unlocked `m_frameQueue.empty()` in the worker loop) | tst_videorecorder (tsan) | |

## GPU — CUDA / NVENC

| ID | P | Parameters | Steps | Expected | Auto | Result |
|---|---|---|---|---|---|---|
| GPU-01 | P1 | compute-sanitizer memcheck + leak-check | Run startStopCycles with CYCLES=5 | 0 errors, 0 leaks | compute-sanitizer | |
| GPU-02 | P1 | GPU memory over cycles | nvidia-smi / cudaMemGetInfo per cycle | Flat | tst_videorecorder | |
| GPU-03 | P2 | NVENC session limit | Start a recording while another NVENC user is active | Clear error; no hang | manual | |
| GPU-04 | P2 | Encoder utilisation | `enc_util_pct` during S3 | < 70 %, with headroom | monitor | |
| GPU-05 | P3 | Dead code | `cuda/nv12_to_rgba.cu` is not built; `d_yuvBuffer` / `m_cudaStream` are unused | Documented; decide whether to remove | review | |

## SNAP — Snapshots

| ID | P | Parameters | Steps | Expected | Auto | Result |
|---|---|---|---|---|---|---|
| SNAP-01 | P2 | Single snapshot | Take a snapshot while live | File exists, decodes, and its format matches the extension (today JPG data with a `.png` name) | manual | |
| SNAP-02 | P2 | Burst of 10 in 1 s | Press repeatedly | 10 distinct files and 10 DB rows. Suspect: timestamps have second resolution, so files are overwritten | manual | |
| SNAP-03 | P1 | Disk full | Snapshot with the disk full | Failure shown, not a success toast. Suspect: returns true before the save finishes | manual | |
| SNAP-04 | P2 | Snapshot while recording | Snapshot during S3 | No latency spike above the threshold (`gui_lag_max_ms`) | diag | |

## PLAY — Playback and export

| ID | P | Parameters | Steps | Expected | Auto | Result |
|---|---|---|---|---|---|---|
| PLAY-01 | P2 | Play a recording ×100 | Open/close the player dialog | Plays; RSS and QObject count flat | manual + monitor | |
| PLAY-02 | P2 | Surgery with 500 thumbnails | Open the surgery recording page | UI responsive; live view unaffected | manual + diag | |
| PLAY-03 | P1 | USB export of a 4 GB recording | Export while live | Live view does not freeze (the copy is synchronous on the GUI thread today); progress is shown | manual + diag | |
| PLAY-04 | P2 | No USB / USB removed mid-copy / two USB drives | Export | Clear error; the destination file is not deleted before the copy succeeds; the right drive is used | manual | |
| PLAY-05 | P2 | PDF report | Generate, view, export | Correct patient data; rendering doesn't block the live view for more than 100 ms | manual + diag | |

## DB — Data integrity

| ID | P | Parameters | Steps | Expected | Auto | Result |
|---|---|---|---|---|---|---|
| DB-01 | P1 | Migrations ×10 | Run init.sql 10× | Exactly 1 `settings` row. Suspect: +1 row per launch | tst_database | |
| DB-02 | P1 | Settings read consistency | Update settings, then read from Qt (`LIMIT 1`) and from Flask (`ORDER BY id DESC`) | Same row / same values | tst_database | |
| DB-03 | P2 | Controllers vs schema | Every api/* CRUD method | All succeed on the real schema | tst_database | |
| DB-04 | P1 | End-to-end links | Add patient → surgery → record → snapshot → comment | Rows reference the correct ids; files exist at the stored paths | manual | |
| DB-05 | P2 | Validation | Empty or invalid fields in the Add/Edit dialogs | Rejected with a message | manual | |
| DB-06 | P2 | Migration robustness | A literal containing `;` in the SQL | Migration doesn't break | tst_database | |
| DB-07 | P2 | DB locked / missing | Lock the DB with sqlite3; delete sqlite.db | Clear error; no crash | manual | |

## UI — Navigation and lifecycle

| ID | P | Parameters | Steps | Expected | Auto | Result |
|---|---|---|---|---|---|---|
| UI-01 | P2 | Navigation loop ×200 | See MEM-07 | No crash; flat memory | manual | |
| UI-02 | P2 | Logout/login while capturing | Log out, log in, check live | Live OK; no second capture | manual + diag | |
| UI-03 | P1 | Leave the Recording page while recording | Press Back during a recording | Recording continues or stops explicitly; it is never lost silently | manual | |
| UI-04 | P1 | Close the app while recording | Close the window | Recording is finalised and the file is valid | manual | |
| UI-05 | P2 | Clean shutdown | Close the app | Exits in < 3 s; capture stopped; Flask child stopped; no crash | manual | |
| UI-06 | P2 | Restart / shutdown buttons | Press them | Confirmation shown and authorised | manual | |

## NET — Flask / zoom

| ID | P | Parameters | Steps | Expected | Auto | Result |
|---|---|---|---|---|---|---|
| NET-01 | P2 | Zoom with ESP32 absent | Press zoom 20× | UI never blocks; each request times out cleanly; no pending-reply growth | manual + monitor | |
| NET-02 | P2 | Flask down / port 8001 in use | Kill Flask, or start a second instance | Report generation shows an error; no hang | manual | |
| NET-03 | P3 | Flask child lifetime | Restart the app 5× | Only one Flask process exists (it is started detached today) | manual | |

## SEC — Security and privacy (findings, no exploitation)

| ID | P | Item | Expected | Result |
|---|---|---|---|---|
| SEC-01 | P1 | Hard-coded login (admin/123456, pre-filled), sudo password in source, shutdown key | Removed | |
| SEC-02 | P1 | Flask on 0.0.0.0:8001 with no auth; endpoints return the camera password or PHI and write arbitrary paths | Localhost-only plus auth | |
| SEC-03 | P1 | Patient DB, recordings and reports committed to the repo | Excluded | |
| SEC-04 | P2 | Existing `test_*` binaries modify the real sqlite.db | Tests use temp DBs only | |
