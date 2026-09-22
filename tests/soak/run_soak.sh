#!/usr/bin/env bash
# run_soak.sh - launch an app, monitor it for N minutes, stop it gracefully, analyze.
#
# Usage: run_soak.sh <binary> <minutes> <scenario> [-- app args...]
#
#   <binary>    path to the executable. The script cd's into the binary's directory before
#               launching it (medical_qt_app resolves applicationDirPath()/../sqlite.db).
#   <minutes>   run length; fractional allowed (e.g. 1.5). Ctrl-C ends the run early
#               (the app is still stopped gracefully and the analysis still runs).
#   <scenario>  label, e.g. S2_live60 -> results/S2_live60_<YYYYmmdd_HHMMSS>/
#
# Environment overrides:
#   INTERVAL=5        monitor sample interval (s)
#   WARMUP_MIN=5      warm-up minutes excluded from slopes / flags
#   STOP_TIMEOUT=30   seconds to wait after SIGTERM before SIGKILL
#   RESULTS_DIR=...   default: <this dir>/results
#
# Output directory contents:
#   app.log      stdout+stderr of the app (contains the [DIAG] lines of the diag build)
#   soak.csv     monitor.sh samples          monitor.log  monitor.sh messages
#   report.html  charts + tables             summary.txt  analyze.py text summary
#   run_info.txt command line, times, PIDs, exit status, GPU / system snapshot
#   events.txt   (optional) append your own notes during the run, e.g.
#                echo "$(date -Is) unplugged SDI" >> <dir>/events.txt

set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

if [[ $# -lt 3 ]]; then
    sed -n '2,26p' "$0" | sed 's/^# \{0,1\}//'
    exit 1
fi
BIN_IN="$1"; MINUTES="$2"; SCEN="$3"; shift 3
[[ "${1:-}" == "--" ]] && shift
APP_ARGS=("$@")

INTERVAL="${INTERVAL:-5}"
WARMUP_MIN="${WARMUP_MIN:-5}"
STOP_TIMEOUT="${STOP_TIMEOUT:-30}"
RESULTS_DIR="${RESULTS_DIR:-$HERE/results}"

if [[ ! -x "$BIN_IN" ]]; then echo "run_soak.sh: '$BIN_IN' is not an executable file" >&2; exit 2; fi
if ! [[ "$MINUTES" =~ ^[0-9]+([.][0-9]+)?$ ]]; then echo "run_soak.sh: minutes must be a number" >&2; exit 2; fi
BIN="$(cd "$(dirname "$BIN_IN")" && pwd)/$(basename "$BIN_IN")"
BIN_DIR="$(dirname "$BIN")"
BIN_NAME="$(basename "$BIN")"

TS="$(date +%Y%m%d_%H%M%S)"
OUT="$RESULTS_DIR/${SCEN}_${TS}"
mkdir -p "$OUT" || exit 2
DUR_S="$(awk -v m="$MINUTES" 'BEGIN{printf "%d", m*60}')"

others="$(pgrep -x "${BIN_NAME:0:15}" 2>/dev/null | tr '\n' ' ')"
if [[ -n "$others" ]]; then
    echo "run_soak.sh: WARNING: '$BIN_NAME' already running (PID $others). A second instance may not" >&2
    echo "             get the DeckLink device and will skew system/GPU numbers. Close it first." >&2
fi

{
    echo "scenario      : $SCEN"
    echo "binary        : $BIN ${APP_ARGS[*]:-}"
    echo "cwd           : $BIN_DIR"
    echo "planned       : $MINUTES min  interval=${INTERVAL}s  warmup=${WARMUP_MIN}min"
    echo "start         : $(date -Is)"
    echo "host          : $(uname -n) $(uname -r)"
    echo "perf_paranoid : $(cat /proc/sys/kernel/perf_event_paranoid 2>/dev/null)"
    echo "mem_total     : $(awk '/MemTotal/ {print $2 " kB"}' /proc/meminfo)"
    if command -v nvidia-smi >/dev/null; then
        echo "gpu           : $(nvidia-smi --query-gpu=name,driver_version,memory.used,memory.total --format=csv,noheader 2>/dev/null)"
    fi
    [[ -n "$others" ]] && echo "WARNING       : other instances running: $others"
} > "$OUT/run_info.txt"

# ---------- launch ----------
( cd "$BIN_DIR" && exec "$BIN" "${APP_ARGS[@]}" ) > "$OUT/app.log" 2>&1 &
APP_PID=$!
echo "app_pid       : $APP_PID" >> "$OUT/run_info.txt"
sleep 0.5
if ! kill -0 "$APP_PID" 2>/dev/null; then
    echo "run_soak.sh: app exited immediately - see $OUT/app.log" >&2
    tail -20 "$OUT/app.log" >&2
fi

"$HERE/monitor.sh" "$APP_PID" "$INTERVAL" "$OUT/soak.csv" 2> "$OUT/monitor.log" &
MON_PID=$!

echo "run_soak.sh: $SCEN  app PID $APP_PID  monitor PID $MON_PID  for $MINUTES min"
echo "run_soak.sh: results -> $OUT   (Ctrl-C to stop early)"

INTERRUPTED=0
trap 'INTERRUPTED=1' INT TERM

START=$(date +%s)
last_report=$START
while :; do
    now=$(date +%s)
    (( now - START >= DUR_S )) && break
    (( INTERRUPTED )) && { echo; echo "run_soak.sh: interrupted - stopping app early"; break; }
    if ! kill -0 "$APP_PID" 2>/dev/null; then
        echo "run_soak.sh: app exited on its own after $((now - START)) s"
        break
    fi
    if (( now - last_report >= 60 )); then
        rss="$(awk '/VmRSS/ {print int($2/1024)}' /proc/$APP_PID/status 2>/dev/null)"
        echo "run_soak.sh: $(( (now - START) / 60 ))/${MINUTES} min  RSS ${rss:-?} MB"
        last_report=$now
    fi
    sleep 1
done
trap - INT TERM

# ---------- graceful stop ----------
STOP_HOW="exited-by-itself"
if kill -0 "$APP_PID" 2>/dev/null; then
    STOP_HOW="SIGTERM"
    kill -TERM "$APP_PID" 2>/dev/null
    for ((i = 0; i < STOP_TIMEOUT * 10; i++)); do
        kill -0 "$APP_PID" 2>/dev/null || break
        sleep 0.1
    done
    if kill -0 "$APP_PID" 2>/dev/null; then
        echo "run_soak.sh: app ignored SIGTERM for ${STOP_TIMEOUT}s - sending SIGKILL" >&2
        kill -KILL "$APP_PID" 2>/dev/null
        STOP_HOW="SIGKILL after ${STOP_TIMEOUT}s"
    fi
fi
wait "$APP_PID" 2>/dev/null
APP_RC=$?
# monitor exits by itself once the PID is gone; make sure
for ((i = 0; i < 100; i++)); do kill -0 "$MON_PID" 2>/dev/null || break; sleep 0.1; done
kill -TERM "$MON_PID" 2>/dev/null
wait "$MON_PID" 2>/dev/null

{
    echo "end           : $(date -Is)"
    echo "actual_s      : $(( $(date +%s) - START ))"
    echo "stopped_by    : $STOP_HOW"
    echo "app_exit_code : $APP_RC"
} >> "$OUT/run_info.txt"

# ---------- analyze ----------
DIAG_ARGS=()
grep -q '\[DIAG\]' "$OUT/app.log" 2>/dev/null && DIAG_ARGS=(--diag "$OUT/app.log")
python3 "$HERE/analyze.py" "$OUT/soak.csv" "${DIAG_ARGS[@]}" --warmup-min "$WARMUP_MIN" \
    --out "$OUT/report.html" | tee "$OUT/summary.txt"
AN_RC=${PIPESTATUS[0]}
echo "analyze_rc    : $AN_RC (0=pass 1=flags 2=error)" >> "$OUT/run_info.txt"
echo
echo "run_soak.sh: done. Report: $OUT/report.html"
exit 0
