#!/usr/bin/env bash
# Build + run the unit tests (plain and ASan/UBSan) and store raw outputs in
# tests/unit/results/.  Usage: tests/unit/run_tests.sh [plain|asan|all]
set -u
HERE="$(cd "$(dirname "$0")" && pwd)"
APP="$(cd "$HERE/../.." && pwd)"
RES="$HERE/results"
MODE="${1:-all}"
JOBS="${JOBS:-3}"
mkdir -p "$RES"
export QT_QPA_PLATFORM=offscreen

run_variant() {   # $1 = build dir name, $2 = label, $3.. = extra cmake args
    local bdir="$APP/$1" label="$2"; shift 2
    cmake -S "$HERE" -B "$bdir" "$@" > "$RES/cmake_$label.txt" 2>&1
    make -C "$bdir" -j"$JOBS" > "$RES/build_$label.txt" 2>&1 || { echo "build $label failed (see $RES/build_$label.txt)"; return 1; }
    for t in tst_comptr tst_database tst_widget_lifetime; do
        ( cd "$bdir" && \
          ASAN_OPTIONS=detect_leaks=1:fast_unwind_on_malloc=0:malloc_context_size=40 \
          LSAN_OPTIONS="suppressions=$HERE/lsan.supp" \
          UBSAN_OPTIONS=print_stacktrace=1 \
          timeout 900 "./$t" > "$RES/${t}.$label.txt" 2>&1 ); echo "$t ($label) exit=$?" | tee -a "$RES/exitcodes_$label.txt"
    done
    ( cd "$bdir" && ctest --output-on-failure -j2 ) > "$RES/ctest_$label.txt" 2>&1
    tail -n 8 "$RES/ctest_$label.txt"
}

rm -f "$RES"/exitcodes_*.txt
if [[ "$MODE" == plain || "$MODE" == all ]]; then run_variant build-tests plain; fi
if [[ "$MODE" == asan || "$MODE" == all ]]; then
    run_variant build-tests-asan asan -DUNIT_SANITIZE=ON
    python3 "$HERE/summarize_lsan.py" "$APP" "$RES"/tst_*.asan.txt > "$RES/lsan_summary.txt" 2>&1
    grep -h "runtime error" "$RES"/tst_*.asan.txt > "$RES/ubsan_runtime_errors.txt" 2>/dev/null || true
    cat "$RES/lsan_summary.txt"
fi
