#!/usr/bin/env bash
# monitor.sh - sample memory / fd / thread / CPU / GPU stats of one process into a CSV.
#
# Usage: monitor.sh <pid|process-name> [interval_s=5] [out.csv]
#
#   Samples every <interval_s> seconds until the process exits or Ctrl-C / SIGTERM.
#   If a name is given, the newest process whose exact comm matches is used (pgrep -n -x),
#   falling back to a full-command-line match (pgrep -n -f).
#   out.csv defaults to ./soak_<name>_<pid>_<YYYYmmdd_HHMMSS>.csv
#
# Columns:
#   timestamp_iso, elapsed_s, rss_kb, vmhwm_kb, vmsize_kb, pss_kb, anon_kb, threads, fds,
#   cpu_pct, voluntary_ctxt, nonvoluntary_ctxt, gpu_proc_mem_mib, gpu_total_used_mib,
#   gpu_util_pct, enc_util_pct, sys_mem_available_kb
#
#   cpu_pct  = (utime+stime delta in ticks / CLK_TCK) / measured wall delta * 100
#              (100 = one full core; can exceed 100 for multi-threaded load).
#              First row is empty (no delta yet).
#   pss_kb   = Pss from /proc/PID/smaps_rollup (needs same uid or ptrace access).
#   anon_kb  = RssAnon from /proc/PID/status.
#
# Per-process GPU memory (verified on driver 580.82.07, RTX A400):
#   * `nvidia-smi --query-compute-apps=pid,used_memory` lists ONLY compute (CUDA) contexts.
#     An OpenGL-only process ("G" type) does NOT appear there -> useless for a GL widget.
#   * `nvidia-smi pmon -c 1 -s m` lists graphics AND compute processes with their FB usage
#     in MB (same numbers as the plain nvidia-smi table, i.e. MiB). One row per PID, type
#     G, C or C+G. Cost ~50 ms.  -> PRIMARY method used here.
#   * `nvidia-smi -q -d PIDS` also lists G and C processes ("Used GPU Memory : N MiB") ->
#     FALLBACK 1 (largest entry for the PID is taken, to avoid double counting C+G).
#   * `--query-compute-apps` -> FALLBACK 2 (compute-only).
#   If nothing reports the PID, the field is left empty (process has no GPU context).
#
# Missing values are written as empty fields; the script never aborts on a failed probe.

set -u

TARGET="${1:-}"
INTERVAL="${2:-5}"
OUT="${3:-}"

if [[ -z "$TARGET" || "$TARGET" == "-h" || "$TARGET" == "--help" ]]; then
    sed -n '2,12p' "$0" | sed 's/^# \{0,1\}//'
    exit 1
fi

# ---------- resolve PID ----------
if [[ "$TARGET" =~ ^[0-9]+$ ]]; then
    PID="$TARGET"
else
    PID="$(pgrep -n -x "$TARGET" 2>/dev/null || true)"
    if [[ -z "$PID" ]]; then
        PID="$(pgrep -n -f "$TARGET" 2>/dev/null | grep -v -e "^$$\$" -e "^$PPID\$" | tail -1 || true)"
    fi
    n_match="$(pgrep -x "$TARGET" 2>/dev/null | wc -l)"
    if [[ "$n_match" -gt 1 ]]; then
        echo "monitor.sh: WARNING: $n_match processes named '$TARGET'; using newest PID $PID" >&2
    fi
fi
if [[ -z "${PID:-}" || ! -d "/proc/$PID" ]]; then
    echo "monitor.sh: process '$TARGET' not found" >&2
    exit 2
fi

COMM="$(cat /proc/$PID/comm 2>/dev/null || echo proc)"
if [[ -z "$OUT" ]]; then
    OUT="./soak_${COMM}_${PID}_$(date +%Y%m%d_%H%M%S).csv"
fi
mkdir -p "$(dirname "$OUT")" 2>/dev/null || true

CLK_TCK="$(getconf CLK_TCK 2>/dev/null || echo 100)"
HAVE_NVSMI=0
command -v nvidia-smi >/dev/null 2>&1 && HAVE_NVSMI=1

HEADER="timestamp_iso,elapsed_s,rss_kb,vmhwm_kb,vmsize_kb,pss_kb,anon_kb,threads,fds,cpu_pct,voluntary_ctxt,nonvoluntary_ctxt,gpu_proc_mem_mib,gpu_total_used_mib,gpu_util_pct,enc_util_pct,sys_mem_available_kb"
echo "$HEADER" > "$OUT"
echo "monitor.sh: pid=$PID comm=$COMM interval=${INTERVAL}s -> $OUT" >&2

STOP=0
trap 'STOP=1' INT TERM HUP

# ---------- probes ----------
gpu_proc_mem() {  # $1 = pid ; prints MiB or nothing
    local pid="$1" v=""
    [[ $HAVE_NVSMI -eq 1 ]] || return 0
    # primary: pmon (graphics + compute)
    v="$(timeout 5 nvidia-smi pmon -c 1 -s m 2>/dev/null |
         awk -v p="$pid" '$1 !~ /^#/ && $2==p && $4 ~ /^[0-9]+$/ {s+=$4; f=1} END{if(f) print s}')"
    if [[ -z "$v" ]]; then
        # fallback 1: -q -d PIDS
        v="$(timeout 5 nvidia-smi -q -d PIDS 2>/dev/null |
             awk -v p="$pid" '
               /Process ID/ {cur=$NF}
               /Used GPU Memory/ && cur==p { if ($(NF-1) ~ /^[0-9]+$/ && $(NF-1)+0 > m) {m=$(NF-1)+0}; f=1 }
               END{ if(f) print m }')"
    fi
    if [[ -z "$v" ]]; then
        # fallback 2: compute apps only
        v="$(timeout 5 nvidia-smi --query-compute-apps=pid,used_memory --format=csv,noheader,nounits 2>/dev/null |
             awk -F', *' -v p="$pid" '$1==p && $2 ~ /^[0-9]+$/ {s+=$2; f=1} END{if(f) print s}')"
    fi
    echo "$v"
}

gpu_global() {  # prints "used,util,enc" (fields may be empty)
    [[ $HAVE_NVSMI -eq 1 ]] || { echo ",,"; return 0; }
    local line
    line="$(timeout 5 nvidia-smi --query-gpu=memory.used,utilization.gpu,utilization.encoder \
            --format=csv,noheader,nounits -i 0 2>/dev/null | head -1)"
    echo "$line" | awk -F', *' '{
        for (i=1;i<=3;i++) { if ($i !~ /^[0-9.]+$/) $i="" }
        printf "%s,%s,%s\n", $1, $2, $3 }'
}


START_NS="$(date +%s%N)"
PREV_TICKS=""
PREV_NS=""

while [[ $STOP -eq 0 && -d "/proc/$PID" ]]; do
    NOW_NS="$(date +%s%N)"
    TS="$(date +%Y-%m-%dT%H:%M:%S.%3N%:z)"
    ELAPSED="$(awk -v a="$NOW_NS" -v b="$START_NS" 'BEGIN{printf "%.1f", (a-b)/1e9}')"

    STATUS="$(cat /proc/$PID/status 2>/dev/null)"
    sf() { echo "$STATUS" | awk -v k="$1:" '$1==k {print $2; exit}'; }
    RSS="$(sf VmRSS)"; HWM="$(sf VmHWM)"; VSZ="$(sf VmSize)"; ANON="$(sf RssAnon)"
    THR="$(sf Threads)"; VCS="$(sf voluntary_ctxt_switches)"; NVCS="$(sf nonvoluntary_ctxt_switches)"

    PSS="$(awk '$1=="Pss:" {print $2; exit}' /proc/$PID/smaps_rollup 2>/dev/null)"

    FDS=""
    if [[ -r "/proc/$PID/fd" ]]; then
        FDS="$(ls -1 /proc/$PID/fd 2>/dev/null | wc -l)"
    fi

    # CPU: fields after the ")" of comm; utime=14, stime=15 (1-based in full stat line)
    CPU=""
    STAT="$(cat /proc/$PID/stat 2>/dev/null)"
    if [[ -n "$STAT" ]]; then
        REST="${STAT##*) }"
        # REST starts at field 3 (state); utime = field 14 -> index 12 in REST (1-based)
        TICKS="$(echo "$REST" | awk '{print $12 + $13}')"
        if [[ -n "$PREV_TICKS" && -n "$TICKS" ]]; then
            CPU="$(awk -v t1="$TICKS" -v t0="$PREV_TICKS" -v n1="$NOW_NS" -v n0="$PREV_NS" -v hz="$CLK_TCK" \
                   'BEGIN{dt=(n1-n0)/1e9; if (dt>0) printf "%.1f", (t1-t0)/hz/dt*100}')"
        fi
        PREV_TICKS="$TICKS"; PREV_NS="$NOW_NS"
    fi

    GPM="$(gpu_proc_mem "$PID")"
    GG="$(gpu_global)"

    MEMAVAIL="$(awk '$1=="MemAvailable:" {print $2; exit}' /proc/meminfo 2>/dev/null)"

    # process may have vanished mid-sample; still write whatever we have
    echo "$TS,$ELAPSED,$RSS,$HWM,$VSZ,$PSS,$ANON,$THR,$FDS,$CPU,$VCS,$NVCS,$GPM,$GG,$MEMAVAIL" >> "$OUT"

    # sleep the remainder of the interval (sleep in 1-s chunks so Ctrl-C is responsive)
    END_NS=$(( NOW_NS + $(awk -v i="$INTERVAL" 'BEGIN{printf "%d", i*1e9}') ))
    while [[ $STOP -eq 0 && -d "/proc/$PID" ]]; do
        REM_NS=$(( END_NS - $(date +%s%N) ))
        (( REM_NS <= 0 )) && break
        if (( REM_NS > 1000000000 )); then sleep 1; else sleep "$(awk -v r="$REM_NS" 'BEGIN{printf "%.3f", r/1e9}')"; fi
    done
done

if [[ -d "/proc/$PID" ]]; then
    echo "monitor.sh: stopped by signal; $(($(wc -l < "$OUT") - 1)) samples in $OUT" >&2
else
    echo "monitor.sh: process $PID exited; $(($(wc -l < "$OUT") - 1)) samples in $OUT" >&2
fi
exit 0
