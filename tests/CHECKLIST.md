# medical_qt_app — Release & Pre-Surgery Checklist

Tick every box. Any unticked **[C]** (critical) item = **do not use in theatre**.
Test-case IDs refer to [TEST_CASES.md](TEST_CASES.md).

---

## A. Release gate (run once per build, by QA)

### Build
- [ ] **[C]** Built with `-DCMAKE_BUILD_TYPE=Release` or `RelWithDebInfo` (the current default build has *no* optimisation)
- [ ] **[C]** Built without `MEDAPP_DIAG`: `strings medical_qt_app | grep -c '\[DIAG\]'` prints `0`
- [ ] Zero compiler warnings in `decklink/`, `ui/Recording/`, `widgets/`
- [ ] Unit tests pass: `ctest --test-dir build-tests` and `ctest --test-dir build-tests-gpu`
- [ ] ASan/LSan run: no leak reported in app code (MEM-05)
- [ ] `compute-sanitizer --leak-check full` on the recorder: 0 errors, 0 leaks (GPU-01)

### Stability and memory (soak)
- [ ] **[C]** 60 min live-only: RSS slope < 5 MB/h after warm-up; fds, threads and GPU memory flat (MEM-01)
- [ ] **[C]** 60 min recording: same criteria (MEM-02)
- [ ] **[C]** 4 h OT-length soak (S4) passes with no crash, freeze or leak flag (MEM-03)
- [ ] **[C]** Live latency flat for the whole soak: `lat_avg_ms` < 100, `inflight` ≤ 2 (LAT-02)
- [ ] **[C]** Glass-to-glass latency at 60 min is within 50 ms of the value at 0 min (LAT-01)

### Capture
- [ ] **[C]** SDI and HDMI inputs both show live video (CAP-01/02)
- [ ] **[C]** Every supported source mode tested (CAP-03 matrix)
- [ ] **[C]** Cable pull shows a visible **NO SIGNAL** state, and the live view recovers after replug (CAP-06)
- [ ] **[C]** Only the selected input is displayed; there is no mixing between inputs or cards (CAP-08)

### Recording
- [ ] **[C]** 50 start/stop cycles pass; every file plays (REC-02)
- [ ] **[C]** Recorded duration equals wall-clock ±1 s for a 30 min recording (REC-03)
- [ ] **[C]** Kill -9 or power cut mid-recording: the footage up to the cut is recoverable (REC-06)
- [ ] **[C]** Disk full: the user is alerted and no false "recording" state is shown (REC-07)
- [ ] **[C]** Encoder init failure: the user is alerted (REC-05)

### Data
- [ ] **[C]** Migrations are idempotent: `settings` has exactly 1 row after 10 launches (DB-01)
- [ ] **[C]** Patient/surgery/recording links are correct in the DB after an end-to-end flow (DB-04)
- [ ] Tests never touch the production `sqlite.db` (they use temp copies)

### Security
- [ ] No hard-coded credentials remain (login, sudo password, shutdown key, RTSP password) (SEC-01)
- [ ] Flask API is bound to 127.0.0.1, or is authenticated (SEC-02)
- [ ] The repo or package contains no patient data (sqlite.db, recordings, reports) (SEC-03)

---

## B. Pre-surgery check (every theatre day, by the operator, ~5 min)

- [ ] **[C]** Machine was freshly booted, or the app was restarted since the last case
- [ ] **[C]** Free disk space ≥ 50 GB. At 8 Mbps, 1 h is about 3.6 GB; allow for the longest case ×3
- [ ] **[C]** Camera source is connected and the **correct input (SDI/HDMI)** is selected in Settings
- [ ] **[C]** Live image is visible on the Dashboard, with orientation and flip correct
- [ ] **[C]** Latency spot-check: wave a hand in front of the camera; the movement looks immediate (< ~0.2 s)
- [ ] **[C]** Make a 10 s test recording, then play it back from the surgery page: the file plays and the duration is right
- [ ] Take a test snapshot; it appears in the gallery
- [ ] Patient and surgery are selected correctly **before** pressing Record
- [ ] USB export drive is present, if the case needs export
- [ ] Zoom control works, if a camera with zoom is used

## C. Post-surgery

- [ ] Stop recording **before** leaving the page or closing the app
- [ ] Recording plays back in full
- [ ] Export or backup is done; the USB drive is safely ejected
- [ ] Clean shutdown from the app (not a power switch)
