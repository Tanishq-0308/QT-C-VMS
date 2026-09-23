# Soak-test scenarios - medical_qt_app live-preview latency drift

Symptom under investigation: the live preview latency grows to ~2 s after some minutes.
Suspects: memory leak, or a GUI-thread backlog (frames queued faster than the GUI consumes
them). These scenarios tell those apart, and each one has a pass/fail result.

Tools (all in `tests/soak/`):

| file | purpose |
|---|---|
| `run_soak.sh <binary> <minutes> <scenario> [-- args]` | launch, monitor, stop gracefully, analyze -> `results/<scenario>_<ts>/` |
| `monitor.sh <pid\|name> [interval=5] [out.csv]` | sample RSS/PSS/anon/threads/fds/CPU/ctxt/GPU into a CSV |
| `analyze.py soak.csv [--diag app.log] [--warmup-min 5] [--out report.html]` | slopes, flags, HTML charts |

Environment overrides for `run_soak.sh`: `INTERVAL` (default 5 s), `WARMUP_MIN` (default 5),
`STOP_TIMEOUT` (default 30 s), `RESULTS_DIR`.

---------------------------------------------------------------------------------------------

## 0. Before every run

```bash
cd /home/brainwave/QT-C-VMS
SOAK=/home/brainwave/QT-C-VMS/tests/soak
DIAGBIN=/home/brainwave/QT-C-VMS/build-diag/medical_qt_app

pgrep -a medical_qt_app          # must be empty: close any running instance first
                                 # (a second instance cannot open the DeckLink input and skews GPU/system numbers)
nvidia-smi --query-gpu=memory.used,utilization.gpu,utilization.encoder --format=csv
```

* Launch the **diagnostic build** (`build-diag/`). It prints one `[DIAG]` line per second on stderr.
  `run_soak.sh` does the `cd build-diag` for you. The app needs this because it opens
  `applicationDirPath()/../sqlite.db`, `../database/migrations/init.sql` and `../flask_zoom_api`.
  If you launch the app by hand, `cd` into `build-diag` first.
* The app starts `flask_zoom_api/app.py` detached (`QProcess::startDetached`). The Flask process
  stays alive after the app stops. That is normal app behaviour. It is not part of the
  measurement, and `run_soak.sh` does not kill it.
* **Stopping:** `run_soak.sh` sends SIGTERM when the time is up. main.cpp has **no SIGTERM
  handler**, so Qt exits at once without running destructors. For scenarios that record
  (S3, S4), **stop the recording from the UI before the timer runs out** so the file is
  finalised. Or quit the app from the UI: `run_soak.sh` notices the exit and still analyzes.
* Log events as they happen. They go in the same results directory, and you will need them
  to read the charts:
  `echo "$(date -Is) settings-save #17" >> $SOAK/results/<dir>/events.txt`
* Glass-to-glass (G2G) latency check: put a millisecond clock (phone stopwatch, or a second
  monitor showing a ms clock) in front of the camera or source. Take **one photo** that shows
  both the real clock and the preview of that clock on the app's screen. Latency = real clock
  minus previewed clock. Take the photos at **0, 5, 15, 30, 60 min** and write the values
  into `events.txt`.

### Pass criteria (common to all scenarios; slopes are computed after the warm-up)

| metric | pass |
|---|---|
| RSS / PSS / RssAnon slope | **< 5 MB/h** (analyze flags `LEAK SUSPECT`) |
| threads, fds | **flat**: back to baseline after transient work (flag `RESOURCE LEAK SUSPECT`) |
| gpu_proc_mem_mib | **flat**, slope < 5 MiB/h (flag `GPU LEAK SUSPECT`) |
| `inflight` | **<= 2** throughout; no growth (flag `BACKLOG`) |
| `lat_avg_ms` | **< 100 ms and flat**; never "first > 100 ms" (flag `BACKLOG`) |
| `paint_avg_ms` / `gui_lag_max_ms` | <= 33 ms / <= 100 ms except isolated spikes (flag `GUI SATURATED`) |
| G2G photos at 0/5/15/30/60 min | all within **50 ms** of each other |
| recording runs | `rec_drops` stays constant, `rec_queue` bounded (flag `REC DROPS`) |

`analyze.py` exits 0 when nothing is flagged and 1 when a flag is raised.
`run_info.txt` records the result as `analyze_rc`.

### How to read the result

* `lat_avg_ms` and `inflight` climb together while RSS stays flat -> **GUI-thread backlog**.
  Queued `frameArrived` signals pile up faster than the GUI thread consumes them. Check
  `paint_avg_ms` and `gui_lag_max_ms` to see what is saturating the GUI thread.
* `lat_avg_ms` climbs while `inflight` stays at 0-2 -> the delay is **not in the signal queue**.
  Look at buffering in DeckLink or the GL upload, or at the display/compositor. Compare with
  the G2G photos.
* RSS/anon climbs in step with latency -> the queued frames are being kept alive (a leak
  caused by the backlog). RSS climbs while latency stays flat -> an independent leak. Find it
  with heaptrack/ASan in a separate run.
* threads or fds rise in steps -> find which UI action matches each step in `events.txt`
  (S5 is designed to show this).

---------------------------------------------------------------------------------------------

## S1 - 10-min baseline, live preview only

**Purpose.** Get reference numbers: RSS, threads, fds, CPU, GPU memory and the `lat_avg_ms`
level. Also checks that the tooling works with the real DeckLink.

```bash
WARMUP_MIN=2 INTERVAL=2 $SOAK/run_soak.sh $DIAGBIN 10 S1_baseline
```
Steps: log in -> open the live page -> leave it alone. G2G photos at 0 and 5 and 10 min.
**Duration:** 10 min.
**Observe:** steady-state `lat_avg_ms` (expect about 1-2 frame times), `emits` = `recv` =
source fps, `inflight` 0-1, `paint_avg_ms`, baseline RSS/threads/fds, gpu_proc_mem.
**Pass:** the common criteria. Slopes over 8 min are low-confidence; the report says so. Use
the absolute values as the reference for later runs.

## S2 - 60-min live preview

**Purpose.** Reproduce the drift under the simplest condition.

```bash
$SOAK/run_soak.sh $DIAGBIN 60 S2_live60
```
Steps: open the live page and do not touch the UI. G2G photos at 0/5/15/30/60 min.
**Duration:** 60 min (warm-up 5).
**Observe:** the "first lat_avg > 100/500/1000 ms" times in the summary; whether `inflight`
grows; RSS/anon slope; `recv` < `emits` (the GUI falling behind); CPU and involuntary
context switches.
**Pass:** the common criteria. In this scenario the G2G drift and the `lat_avg_ms` drift are
the key results.

## S3 - 60-min recording

**Purpose.** Recording (encoder, frame grab, writer queue) running together with the live
preview.

```bash
$SOAK/run_soak.sh $DIAGBIN 62 S3_rec60
```
Steps: start recording at about 1 min. **Stop the recording from the UI at about 61 min**
(before the SIGTERM). G2G photos at 0/5/15/30/60 min.
**Duration:** 62 min.
**Observe:** `rec_queue` (bounded?), `rec_drops` (constant?), `rec_grab_avg_ms/max_ms`,
`enc_util_pct` steady, gpu_proc_mem (no NVENC/CUDA surface leak), RSS slope, and whether the
live `lat_avg_ms` behaves worse than in S2.
**Pass:** the common criteria plus `rec_drops` constant and `rec_queue` bounded. Also check
that the recorded file plays back for its full length (`ffprobe` the file if ffmpeg is
installed).

## S4 - 4-hour operating-theatre-length session

**Purpose.** A realistic full-length case: live preview all the time, recording in 30-min
segments, and periodic snapshots.

```bash
$SOAK/run_soak.sh $DIAGBIN 245 S4_ot4h
```
Steps (log each one in `events.txt`):
* t = 2 min: start recording. Every 30 min: stop the recording, wait 30 s, start a new one.
  That gives 8 segments.
* Every 5 min: take 1 snapshot. Every 30 min: take a burst of 5 snapshots.
* Stop the last recording at about 243 min.
* G2G photos at 0/5/15/30/60/120/180/240 min.

**Duration:** 245 min.
**Observe:** whether each segment start or stop leaves a permanent step in threads, fds, RSS
or GPU memory (it should not); latency after each segment change; `sys_mem_available_kb`
trend; snapshots and segments are all written.
**Pass:** the common criteria over the whole 4 h. The fitted RSS growth over 4 h must be
< 20 MB. Threads and fds must be the same at the end of each segment.

## S5 - UI stress

**Purpose.** Find leaks tied to UI actions: page objects, capture restarts, dialogs and
snapshots.

```bash
INTERVAL=2 WARMUP_MIN=3 $SOAK/run_soak.sh $DIAGBIN 50 S5_uistress
```
Steps (write the start and end of each block in `events.txt`):
1. min 3-13: **navigation loop**. Dashboard -> patient -> surgery -> live page -> back,
   about 100 cycles.
2. min 13-28: **Settings save x50**. Each save restarts capture. Wait until the preview is
   back before the next save.
3. min 28-38: **dialogs x100**. Open and close the heaviest dialogs, for example add patient,
   comments and confirmation boxes.
4. min 38-45: **snapshot bursts**. 10 bursts of 10 snapshots, as fast as the UI allows.
5. min 45-50: idle on the live page.

**Duration:** 50 min.
**Observe:** per block, the net change in threads, fds, RSS and gpu_proc_mem between the idle
phase before the block and the idle phase after it. Also: after 50 capture restarts, does
`lat_avg_ms` go back to the S1 level? Does `inflight` go back to 0-1? (A restart that leaves
an extra `frameArrived` connection doubles the emits x receivers: `emits` stays the same,
`inflight` grows.)
**Pass:** threads and fds back to their baseline within +/-2 after each block. RSS after the
final idle within +20 MB of the pre-stress idle (allocator caching). gpu_proc_mem back within
+/-5 MiB. Latency back to the S1 level.

## S6 - signal unplug / replug every 5 min

**Purpose.** Loss and recovery of the input signal (DeckLink format-change and no-signal
paths).

```bash
$SOAK/run_soak.sh $DIAGBIN 62 S6_unplug
```
Steps: every 5 min (at 5, 10, ... 60 min) unplug the SDI/HDMI input, wait about 20 s, then
plug it back in. That is 12 cycles. Log each unplug and replug in `events.txt`. G2G photo
about 1 min after each replug at 5/15/30/60.
**Duration:** 62 min.
**Observe:** while unplugged, `emits`/`recv` should drop to 0 (lat is 0 then) and must
recover after the replug. `inflight` must return to 0-1 and must not carry over stale
counts. threads, fds and GPU memory must not step up per cycle.
**Pass:** the common criteria. Preview recovers within 3 s of each replug with no app
restart. No per-cycle steps.

## S7 - GUI load during live preview (PDF report, USB copy)

**Purpose.** Heavy work on or near the GUI thread while the live preview is running.

```bash
INTERVAL=2 $SOAK/run_soak.sh $DIAGBIN 30 S7_guiload
```
Steps: live page running. At 5, 10, 15, 20 min generate a PDF report and copy one recording
(>= 1 GB) to a USB stick through the app. Log the start and end of each action. After
25 min, stay idle.
**Duration:** 30 min.
**Observe:** `gui_lag_max_ms` and `paint_max_ms` spikes during each action; `inflight`
during the action (a backlog builds up if the work runs on the GUI thread); whether
`lat_avg_ms` returns to baseline within a few seconds after each action or stays elevated
(stays elevated = the backlog is never drained). Also fds during the USB copy.
**Pass:** after each action, `inflight` <= 2 and `lat_avg_ms` < 100 ms within 5 s.
Latency, fds and threads at the end equal to the S1 level. `GUI SATURATED` may appear only
for the action windows. Check the chart times against `events.txt`.

---------------------------------------------------------------------------------------------

## Profiling: nsys timeline at the start vs after the drift

Verified with Nsight Systems 2025.3.2 (`nsys profile --help`, `nsys start --help`). The
`launch` -> `start` -> `stop` flow was tested on this machine: it takes two 120-s captures
from **the same process**, and the app keeps running after the first `stop`.

```bash
cd /home/brainwave/QT-C-VMS/build-diag
OUT=/home/brainwave/QT-C-VMS/tests/soak/results/nsys_$(date +%Y%m%d_%H%M%S); mkdir -p $OUT

# 1) launch the app under nsys (trace options are fixed at launch time)
nsys launch --session-new=medsoak -t cuda,nvtx,osrt,opengl ./medical_qt_app 2> $OUT/app.log &
sleep 20; $SOAK/monitor.sh medical_qt_app 5 $OUT/soak.csv &          # optional, in parallel

# 2) capture 120 s near the start (after about 1 min of live preview)
nsys start --session=medsoak -o $OUT/early -f true  -s none --cpuctxsw=none
sleep 120; nsys stop --session=medsoak

# 3) wait until the drift is visible (lat_avg_ms in app.log > 500 ms, e.g. 30-45 min), then
nsys start --session=medsoak -o $OUT/drift -f true  -s none --cpuctxsw=none
sleep 120; nsys stop --session=medsoak

# 4) quit the app from the UI (or: nsys shutdown --session=medsoak  -> sends SIGTERM)
nsys stats --report osrt_sum,cuda_api_sum,opengl_khr_range_sum $OUT/early.nsys-rep
nsys stats --report osrt_sum,cuda_api_sum,opengl_khr_range_sum $OUT/drift.nsys-rep
python3 $SOAK/analyze.py $OUT/soak.csv --diag $OUT/app.log --out $OUT/report.html
```

* `-s none --cpuctxsw=none` is **required while perf_event_paranoid = 4**, the current value.
  `nsys status -e` reports "CPU Profiling Environment (process-tree): Fail". OS-runtime,
  OpenGL and CUDA API tracing still work at paranoid 4 (tested).
* With paranoid lowered to <= 2 (see below), use
  `-s process-tree --cpuctxsw=process-tree` in the `nsys start` commands instead. This adds
  CPU IP sampling with backtraces and thread scheduling. It shows the GUI (main) thread's
  busy/blocked periods in the timeline, which is what the backlog question needs.
* Single-shot alternative (a fresh run, capture only the first 120 s after 60 s):
  `nsys profile -t cuda,nvtx,osrt,opengl -s none --cpuctxsw=none -y 60 -d 120 --kill=none -o $OUT/start -f true ./medical_qt_app`
  (`--kill=none` stops nsys from sending SIGTERM to the app when the capture ends).
* Compare the early and drift timelines: time between the DeckLink callback thread's activity
  and the GUI thread's `paintGL`/`glTexSubImage`/swap calls; GUI thread utilisation;
  `pthread_cond_wait`/`poll` gaps on the GUI thread; how long `glFinish`/swap blocks.

## Profiling: perf (CPU hot spots on the GUI thread)

`/proc/sys/kernel/perf_event_paranoid` is **4** on this machine (Ubuntu's extra level: no
unprivileged perf at all). Checked: `perf stat -p <pid>` fails with
"Disallow kernel profiling". Lower it for the profiling session only, then restore it:

```bash
sudo sysctl kernel.perf_event_paranoid=1     # 2 = user-space only; 1 = also kernel call chains
# ... profile ...
sudo sysctl kernel.perf_event_paranoid=4     # restore (not persistent unless put in /etc/sysctl.d)
```

```bash
PID=$(pgrep -n -x medical_qt_app)
# per-thread CPU usage (find the GUI thread = TID == PID, and the DeckLink callback thread)
top -H -b -n 1 -p $PID | head -30
# 60-s sample with DWARF call graphs (the build needs -g; frame pointers are usually omitted)
perf record -F 199 --call-graph dwarf,16384 -p $PID -o /home/brainwave/QT-C-VMS/tests/soak/results/perf_early.data -- sleep 60
# ... repeat after the drift with perf_drift.data, then:
perf report -i /home/brainwave/QT-C-VMS/tests/soak/results/perf_drift.data --no-children --sort tid,dso,sym
perf report -i ... --tid $PID --no-children          # GUI thread only
# cheaper: live counters per thread
perf stat -e task-clock,context-switches,cpu-migrations,page-faults --per-thread -p $PID -- sleep 30
```

A leak, rather than CPU load, needs heaptrack or an ASan/LSan build, not perf.

---------------------------------------------------------------------------------------------

## Notes on the measurements

* **Per-process GPU memory:** on driver 580.82.07 `nvidia-smi --query-compute-apps` lists only
  CUDA (C) contexts. The app's OpenGL widget context (type G) is missing there.
  `nvidia-smi pmon -c 1 -s m` lists G, C and C+G processes and is what `monitor.sh` uses.
  It falls back to `nvidia-smi -q -d PIDS`, then to `--query-compute-apps`. On this machine
  all methods agree with the plain `nvidia-smi` process table (see
  `selftest/gpu_query_validation.txt`). A process that uses CUDA and GL shows as one C+G row.
* `cpu_pct` is 100 = one core, computed from the utime+stime delta over the measured wall
  interval.
* `enc_util_pct` / `gpu_util_pct` / `gpu_total_used_mib` are whole-GPU values: the desktop,
  Firefox and others are included. Use them for trends only.
* The `[DIAG]` probe itself costs about nothing (atomic counters, 1 line/s), but only the
  diag build has it. Production builds show only the monitor.sh side.
