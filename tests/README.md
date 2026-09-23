# Test suite — medical_qt_app

| Path | What |
|---|---|
| [CHECKLIST.md](CHECKLIST.md) | Release gate + daily pre-surgery checklist |
| [TEST_CASES.md](TEST_CASES.md) | Full test-case catalogue (CAP, LAT, MEM, REC, GPU, SNAP, PLAY, DB, UI, NET, SEC) |
| `unit/` | QtTest: com_ptr / DeckLink refcounts, database & migrations, widget/dialog lifetime (see `unit/README.md`) |
| `unit_gpu/` | QtTest: VideoRecorder + NVENC, start/stop leak cycles, crash / disk-full, sanitizers (see `unit_gpu/README.md`) |
| `soak/` | `monitor.sh`, `analyze.py`, `run_soak.sh`, `scenarios.md` — long-run memory/latency soak tooling |
| `../diag/DiagProbe.h` | Opt-in latency/backlog probes (`-DMEDAPP_DIAG`), no-op in normal builds |

## Build variants
All are out-of-source; the normal `build/` is untouched.

```bash
cd /home/brainwave/QT-C-VMS
# realistic baseline (optimised, no probes)
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build build-release -j4
# probes: prints one "[DIAG] ..." line per second on stderr
cmake -S . -B build-diag -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_CXX_FLAGS=-DMEDAPP_DIAG && cmake --build build-diag -j4
# ASan + UBSan (+ probes) for whole-app leak run (MEM-05)
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer -DMEDAPP_DIAG" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined" && cmake --build build-asan -j3
```

## Running the app variants
The app opens `applicationDirPath()/../sqlite.db`, so run it from its build dir (it uses the real repo DB, as the normal build does).

```bash
# Diagnostic live run (LAT-02): keep the log
cd build-diag && ./medical_qt_app 2> diag.log &
../tests/soak/monitor.sh medical_qt_app 5 ../tests/soak/results/live.csv
python3 ../tests/soak/analyze.py ../tests/soak/results/live.csv --diag diag.log --out report.html

# ASan run (MEM-05): exercise every page/dialog, record, then close the app normally
cd build-asan && ASAN_OPTIONS=protect_shadow_gap=0:detect_leaks=1:log_path=asan ./medical_qt_app
#   -> asan.<pid> contains the leak report after exit
```

### Reading `[DIAG]` lines
| Field | Meaning | Healthy |
|---|---|---|
| `inflight` | frames posted to GUI thread not yet consumed (all receivers) | ≤ 2, flat |
| `emits` / `recv` | frames/s out of DeckLink callback / into widgets | recv = emits × receivers |
| `lat_avg_ms`, `lat_max_ms` | DeckLink callback → widget `setFrame` | < 50 ms, flat |
| `paint_avg_ms`, `paint_max_ms` | `paintGL` duration (incl. framebuffer readback) | < 16 ms |
| `gui_lag_max_ms` | lateness of a 50 ms GUI timer (event-loop saturation) | < 100 ms |
| `rec_queue`, `rec_drops` | recorder queue depth, cumulative dropped frames | queue small, drops 0 |
| `rec_grab_avg_ms` | recording-timer grab + `recordFrame` on GUI thread | < 10 ms |

A climbing `inflight` together with climbing `lat_avg_ms` confirms that the GUI thread is building a backlog, which would explain the latency drift.

## Removing the probes
The `DIAG_*` calls are in `decklink/DeckLinkOpenGLWidget.cpp` (DrawFrame, deliverLatestFrame, paintGL), `ui/Recording/RecordingPage.cpp` and `ui/Recording/VideoRecorder.cpp`, together with the `#include "diag/DiagProbe.h"` lines. They compile to nothing unless `MEDAPP_DIAG` is defined.

`diag/orig/` contains copies of `DeckLinkOpenGLWidget.cpp`, `RecordingPage.cpp`, `VideoRecorder.cpp`, `HomePage.cpp/.hpp` and `CMakeLists.txt` from before any change. Use git as the source of truth for all changes; install git with `sudo apt install git`, then run `git diff`.
