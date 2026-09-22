# tests/unit_gpu — VideoRecorder (CUDA NV12 + NVENC + fragmented MP4) test suite

QtTest suite for `ui/Recording/VideoRecorder.cpp` and `cuda/frame_convert.cu`. Both are compiled
**unmodified** from the repo. Only the public API is used: `pushFrame()`, `startRecording()`,
`stopRecording()`, `setFlipStep()` and the signals. The suite measures and reports; it does not fix
production code. A failing check names the production behaviour it exposes.

Requirements: NVIDIA GPU with NVENC, CUDA 13 at `/usr/local/cuda` (nvcc, sm_86), FFmpeg dev libs
(pkg-config libavformat/libavcodec/libavutil with `h264_nvenc` and the software h264 decoder),
Qt5 Core/Test, and `nvidia-smi` for per-process GPU memory. No ffmpeg/ffprobe CLI is needed:
every file is verified with libavformat/libavcodec in C++.

## How the harness works
- `CaptureSim` pushes synthetic frames from its own thread, like the DeckLink capture thread, at the
  stream rate (60 fps, or 59.94 with `frameDuration` 2002 at `timeScale` 120000). Each frame carries
  its stream slot number as 24 bit-blocks in the picture, so the decoded file can be checked frame by
  frame: content, order, duplicates and exact pts against the capture clock.
- Signals go through queued connections to the main (GUI) thread, as in the application.
- Exact accounting ("fed"): the feeder is paused while `startRecording()` runs. Frames are
  counted only after `recordingStarted` has arrived, and only until `stopRecording()` is called.
  Pausing during init does not hide anything, because the signal check happens synchronously
  inside `startRecording()`. `startStopCycles` runs the feeder freely, concurrently with start/stop.
- Child processes (the binary re-executes itself with `--child <mode>`) cover SIGKILL, `RLIMIT_FSIZE`,
  `CUDA_VISIBLE_DEVICES=""`, and a private tmpfs in an unprivileged user+mount namespace
  (`unshare -rm`) for the low-disk cases. Children die if the runner dies (`PR_SET_PDEATHSIG`).
- The harness calls `cudaMemGetInfo()` only after a recording has started in the process, because an
  earlier CUDA runtime init breaks the recorder's GPU init (see `cudaRuntimeFirst`).
- Temporary media go to `$VR_OUT_DIR` (default `/tmp/claude-1000/vr_tests`) and are deleted after
  analysis. The only exception is `evidence/diskfull.mp4` (at most 3 MB).

## Build (from the repo root; at most `-j4`)
```sh
cmake -S tests/unit_gpu -B build-tests-gpu                          && make -C build-tests-gpu -j4
cmake -S tests/unit_gpu -B build-tests-gpu-asan -DSANITIZE=address  && make -C build-tests-gpu-asan -j4
CUDAHOSTCXX=g++-12 CXX=g++-12 CC=gcc-12 cmake -S tests/unit_gpu -B build-tests-gpu-tsan -DSANITIZE=thread && make -C build-tests-gpu-tsan -j4
gcc-12 -shared -fPIC -O1 -Wl,--version-script=tests/unit_gpu/tsan_shim/shim.map \
       -o build-tests-gpu-tsan/libtsan_condshim.so tests/unit_gpu/tsan_shim/tsan_condshim.c
```

## Tests
| test | what / pass criteria |
|---|---|
| `basic1080p60` | 10 s (`BASIC_SECONDS`) UYVY 1080p60. encoded == fed, 0 dropped, every decoded frame is the fed frame in order, pts exact, duration within 1 frame, 60 fps, h264 1920x1080, keyframe every 60 frames, `startRecording()` < 50 ms, `recordingStarted` < 2 s, `stopRecording()` < 50 ms, finalisation < 2 s |
| `pixelExact(UYVY/ARGB/BGRA)` | 16 flat patches. Every interior pixel of frames 0/15/29 is compared: UYVY ±1, RGB ±3 against BT.709 limited range computed from the definition. Random alpha must be ignored. Stream must signal range/colourspace |
| `flipSteps(fmt_stepN)` | UYVY and BGRA, flip 0..3. The bright block's bounding box must land at the mirrored position (±1 px), with its pixel count and chroma preserved |
| `durationAccuracy` | 60 s (`DURATION_SECONDS`) at 59.94 fps (2002/120000). Duration == fed stream time ±1 frame, pts exact, avg_frame_rate 59.94 |
| `streamGaps` | 5 stream slots skipped after 90 frames. dropped == 5, exactly one 6-frame pts step, `framesDropped(5)` emitted |
| `poolOverflow(burst/gpuStall)` | `burst`: 200 frames (`POOL_BURST`) pushed as fast as possible. `gpuStall`: capture at 60 fps while a 1.5 s spin kernel (`gpu_stall.cu`, default stream of the shared primary context) stalls the worker's `cuCtxSynchronize()`. Overflow must happen, encoded + dropped == pushed, `framesDropped` emitted, surviving frames keep capture-clock pts |
| `noSignal` | no frame ever / last frame 600 ms old / only `hasSignal=false` frames are refused with a message and no file. A frame 300 ms old is accepted. After the signal returns, start works |
| `startFailures` | 10 rounds (`FAIL_ROUNDS`) of: missing folder, 0555 folder, unsupported pixel format, odd size (synchronous false); existing 0444 file and path-is-a-directory (asynchronous `errorOccurred` + `recordingStopped(path,0,0)`, no `recordingStarted`). RSS/fd/thread/GPU checked from round 1 to round 10; then a valid start. A `CUDA_VISIBLE_DEVICES=""` child makes 10 attempts |
| `startStopCycles` | 50 (`CYCLES`) × 2 s (`CYCLE_SECONDS`) with a free-running feeder. Each file is decoded completely. Cycle 5 → last: GPU/pid ≤ +16 MiB, fds/threads flat. RSS ≤ +20 MB is checked on the envelope, max(cycles 5..10) vs max(last 5), because RSS is bimodal by one frame pool (about 124 MB). The raw cycle-5→50 delta is also printed. Writes `results/cycles.csv` |
| `crashMidRecording(lowDetail/realisticNoise)` | child SIGKILLed 6 s (`CRASH_AFTER_MS`) after `recordingStarted`. File opens, ≥ 4 s playable, frames consecutive; only the final truncated packet may be corrupt |
| `diskFull` | child with `RLIMIT_FSIZE` 3 MB (`DISKFULL_BYTES`), SIGXFSZ ignored, noisy content. `errorOccurred` then `recordingStopped`, file playable up to the error minus at most one fragment |
| `lowDiskStart` | `unshare -rm` + tmpfs: 1 GiB tmpfs → start refused (< 2 GB). 2056 MiB tmpfs, 1040 MiB filler written 1 s into recording → `errorOccurred("almost full")` and self-stop within 3 s, file playable. The second case is skipped if MemAvailable < 2.2 GiB (the tmpfs filler uses RAM) |
| `formatChangeSegment(...)` | 1080p→720p UYVY (spec), 720p→1080p UYVY, 1080p UYVY→720p BGRA: `segmentStarted(<name>_part2.mp4)`, both files complete with the right size and an exact timeline |
| `restartWhileFinalising` | start right after stop → false "still in progress or being saved"; succeeds after `recordingStopped` |
| `pushFrameCost` | per-call `pushFrame()` cost on the feeding thread, 1080p UYVY while recording (median < 2 ms, p99 < 5 ms, max < 16 ms) and while idle |
| `longRun` | skipped unless `LONG_RUN_MIN=N`. Samples RSS/GPU/fds/threads every 10 s to `results/longrun.csv` (`LONG_RUN_CSV`) |
| `realisticBitrate` | 10 s of noisy moving 1080p60. 0 drops, average bitrate within 75..125 % of the configured target (0.13 bpp = 16.17 Mbps) |
| `streamClockReset` (extra) | capture clock restarts at 0 with the same layout (DeckLink Stop/StartStreams). Frames after the reset must still be recorded |
| `cudaRuntimeFirst` (extra) | fresh child calls `cudaFree(0)` (CUDA runtime init, as any CUDA preview path would), then records. Recording must start |
| `stopDuringInit` (extra) | stop before `recordingStarted` gives exactly one `recordingStopped`, and the next start works |
| `destroyWhileRecording` (extra) | destroying the recorder while it records (app shutdown) finalises a complete file |

`VR_RELAX=1` makes timing and real-time checks informational (use it for sanitizer runs).
`VR_KEEP=1` keeps the media files.

## Run
```sh
cd build-tests-gpu
./tst_videorecorder                         > ../tests/unit_gpu/results/suite_full.log 2>&1
./tst_videorecorder basic1080p60            # single test
./tst_videorecorder "formatChangeSegment:1080p_UYVY_to_720p_BGRA"   # single data row
CYCLES=10 ./tst_videorecorder startStopCycles
LONG_RUN_MIN=10 ./tst_videorecorder longRun > ../tests/unit_gpu/results/longrun.log 2>&1
```

## Sanitizers
compute-sanitizer (CUDA memcheck + device leak check) on a shortened `basic1080p60`:
```sh
cd build-tests-gpu
BASIC_SECONDS=3 VR_RELAX=1 /usr/local/cuda/bin/compute-sanitizer --tool memcheck --leak-check full \
  --log-file ../tests/unit_gpu/results/compute_sanitizer_memcheck.txt ./tst_videorecorder basic1080p60 \
  > ../tests/unit_gpu/results/compute_sanitizer_run.log 2>&1
```

ASan + LSan (`protect_shadow_gap=0` is required with the CUDA driver):
```sh
cd build-tests-gpu-asan
ASAN_OPTIONS=protect_shadow_gap=0:detect_leaks=1:fast_unwind_on_malloc=0:malloc_context_size=30 \
LSAN_OPTIONS=suppressions=$PWD/../tests/unit_gpu/lsan.supp:print_suppressions=0 \
CYCLES=5 VR_RELAX=1 ./tst_videorecorder startStopCycles startFailures > ../tests/unit_gpu/results/asan_run.log 2>&1
python3 ../tests/unit_gpu/filter_lsan.py ../tests/unit_gpu/results/asan_run.log > ../tests/unit_gpu/results/asan_app_leaks.txt
```
The `nocuda` child inherits the ASan options and prints its own LSan report into the same log.

TSan (g++-12 / libtsan.so.2 plus the harness shim; see below):
```sh
cd build-tests-gpu-tsan
TSAN_OPTIONS=suppressions=$PWD/../tests/unit_gpu/tsan.supp:history_size=4:second_deadlock_stack=1 \
BASIC_SECONDS=5 VR_RELAX=1 setarch -R env LD_PRELOAD="/lib/x86_64-linux-gnu/libtsan.so.2 $PWD/libtsan_condshim.so" \
  ./tst_videorecorder basic1080p60 poolOverflow > ../tests/unit_gpu/results/tsan_run.log 2>&1
```
Why the shim is needed (test harness only; no application code is touched):
- Without it the process SEGVs inside the TSan allocator as soon as NVENC opens. libnvcuvid obtains
  `pthread_cond_*` via `dlvsym(..., "GLIBC_2.2.5")`, which returns the legacy compat versions that TSan
  does not intercept, and glibc's compat wrapper calls `calloc()` while TSan considers the thread
  blocked. The shim exports `dlvsym@GLIBC_2.2.5` and returns the current `pthread_cond_*` (the TSan
  interceptors).
- Qt 5 is not TSan-instrumented. `QBasicMutex`/`QMutex`/`QWaitCondition` out-of-line paths are wrapped
  to add acquire/release edges. Otherwise Qt's queued-signal machinery is reported as racy.
- `setarch -R` (no ASLR) avoids "unexpected memory mapping" aborts. `libtsan.so.2` must come before the
  shim in `LD_PRELOAD`. `called_from_lib` must not name libraries that are `dlclose()`d (TSan aborts).
Timing checks are not meaningful under TSan; only the race reports are. Under TSan the instrumented
4 MB `memcpy` makes `pushFrame()` about 7 ms, so `poolOverflow/burst` cannot overflow the pool there.
`poolOverflow/gpuStall` still exercises the concurrent drop path.
The TSan build needs `CUDAHOSTCXX=g++-12`. Otherwise nvcc's implicit link directory
(`/usr/lib/gcc/x86_64-linux-gnu/11`) makes `-fsanitize=thread` link gcc-11's `libtsan.so.0`, and the
binary aborts with "unexpected memory mapping".

## Files
- `tst_videorecorder.cpp`: the suite (custom `main`, child modes).
- `gpu_stall.cu`: test-only spin kernel used to stall the encoder (`poolOverflow/gpuStall`).
- `CMakeLists.txt`: standalone project; `-DSANITIZE=address|thread`.
- `lsan.supp`, `tsan.supp`: driver/runtime noise suppressions only.
- `tsan_shim/`: LD_PRELOAD shim for TSan runs.
- `filter_lsan.py`: keeps only sanitizer report blocks whose stack mentions `VideoRecorder`.
- `results/`: logs and CSVs from the runs.
