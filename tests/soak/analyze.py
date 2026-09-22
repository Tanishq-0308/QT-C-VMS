#!/usr/bin/env python3
"""analyze.py - soak-test analysis for monitor.sh CSVs and [DIAG] logs (stdlib only).

Usage:
    analyze.py soak.csv [--diag diag.log] [--warmup-min 5] [--out report.html]

* Per numeric column: start, end, min, max, least-squares slope per hour (after warm-up).
* Flags:
    LEAK SUSPECT           rss_kb / pss_kb / anon_kb slope > 5 MB/h
    RESOURCE LEAK SUSPECT  fds / threads increasing (slope > 0 and last-20% median > first-20% median)
    GPU LEAK SUSPECT       gpu_proc_mem_mib slope > 5 MiB/h
    BACKLOG                inflight grows, inflight > 2 sustained, or lat_avg_ms grows
    GUI SATURATED          paint_avg_ms > 33 or gui_lag_max_ms > 100 sustained
                           (>= 10 % of post-warm-up samples or >= 10 consecutive samples)
    REC DROPS              rec_drops (cumulative counter) increased after warm-up
* Diag log: any line containing "[DIAG]" (Qt prefixes allowed); every key=value token after
  the tag is parsed generically; t=<epoch_ms> is the timestamp.
* Writes a text summary to stdout and a self-contained HTML report (inline SVG charts).
Exit code: 0 = no flags, 1 = at least one flag raised, 2 = usage / input error.
"""
import argparse
import csv
import datetime as dt
import html
import json
import math
import os
import re
import statistics
import sys

MB_PER_H_LIMIT = 5.0          # RSS/PSS/anon
GPU_MIB_PER_H_LIMIT = 5.0
LAT_GROWTH_MS = 10.0          # lat_avg_ms: median(last 20%) - median(first 20%)
LAT_SLOPE_MS_PER_H = 10.0
PAINT_LIMIT_MS = 33.0
GUI_LAG_LIMIT_MS = 100.0
INFLIGHT_LIMIT = 2
SUSTAIN_FRAC = 0.10
SUSTAIN_RUN = 10
MIN_GROWTH_MB = 0.5           # LEAK needs slope > limit AND this much fitted growth (filters PSS jitter)
SHORT_RUN_MIN = 10.0          # post-warm-up span below this -> slopes are low-confidence

KNOWN_DIAG = ["inflight", "emits", "recv", "lat_avg_ms", "lat_max_ms", "paint_n", "paint_avg_ms",
              "paint_max_ms", "gui_lag_max_ms", "rec_queue", "rec_drops", "rec_grab_n",
              "rec_grab_avg_ms", "rec_grab_max_ms"]

# ----------------------------------------------------------------------------- parsing


def to_float(s):
    if s is None:
        return None
    s = s.strip()
    if s == "":
        return None
    try:
        v = float(s)
    except ValueError:
        return None
    return v if math.isfinite(v) else None


def parse_iso(s):
    s = (s or "").strip()
    if not s:
        return None
    if s.endswith("Z"):
        s = s[:-1] + "+00:00"
    m = re.match(r"(.*[T ]\d\d:\d\d:\d\d(?:\.\d+)?)([+-]\d\d)(\d\d)$", s)  # +0530 -> +05:30
    if m:
        s = m.group(1) + m.group(2) + ":" + m.group(3)
    try:
        return dt.datetime.fromisoformat(s).timestamp()
    except ValueError:
        pass
    try:
        base, frac_tz = s.split(".", 1)
        fm = re.match(r"(\d+)(.*)", frac_tz)
        frac = (fm.group(1) + "000000")[:6]
        return dt.datetime.fromisoformat(base + "." + frac + fm.group(2)).timestamp()
    except Exception:
        return None


def load_csv(path):
    """Return (times[], columns{name: [float|None]}, column order)."""
    times, cols, order = [], {}, []
    with open(path, newline="") as f:
        rd = csv.DictReader(f)
        order = [c for c in (rd.fieldnames or []) if c not in ("timestamp_iso",)]
        for c in order:
            cols[c] = []
        t_first = None
        for row in rd:
            t = parse_iso(row.get("timestamp_iso"))
            if t is None:
                el = to_float(row.get("elapsed_s"))
                if el is None:
                    continue
                t = (t_first or 0.0) + el
            if t_first is None:
                t_first = t
            times.append(t)
            for c in order:
                cols[c].append(to_float(row.get(c)))
    return times, cols, order


DIAG_RE = re.compile(r"\[DIAG\](.*)")
KV_RE = re.compile(r"([A-Za-z_][\w.]*)=(\S+)")


def load_diag(path):
    """Return (times[], metrics{key: [float|None]}, key order, n_lines, n_skipped)."""
    rows, keys, n, skipped = [], [], 0, 0
    with open(path, errors="replace") as f:
        for line in f:
            m = DIAG_RE.search(line)
            if not m:
                continue
            n += 1
            kv = {}
            for k, v in KV_RE.findall(m.group(1)):
                fv = to_float(v.rstrip(",;"))
                if fv is not None:
                    kv[k] = fv
            if "t" not in kv:
                skipped += 1
                continue
            rows.append(kv)
            for k in kv:
                if k != "t" and k not in keys:
                    keys.append(k)
    rows.sort(key=lambda r: r["t"])
    times = [r["t"] / 1000.0 for r in rows]
    metrics = {k: [r.get(k) for r in rows] for k in keys}
    known = [k for k in KNOWN_DIAG if k in keys]
    order = known + [k for k in keys if k not in known]
    return times, metrics, order, n, skipped

# ----------------------------------------------------------------------------- stats


def pairs(times, vals, t_from=None):
    return [(t, v) for t, v in zip(times, vals) if v is not None and (t_from is None or t >= t_from)]


def slope_per_hour(pts):
    if len(pts) < 3:
        return None
    xs = [p[0] / 3600.0 for p in pts]
    ys = [p[1] for p in pts]
    mx, my = sum(xs) / len(xs), sum(ys) / len(ys)
    sxx = sum((x - mx) ** 2 for x in xs)
    if sxx == 0:
        return None
    return sum((x - mx) * (y - my) for x, y in zip(xs, ys)) / sxx


def window_medians(pts, frac=0.2):
    if len(pts) < 5:
        return None, None
    k = max(1, int(len(pts) * frac))
    return statistics.median(p[1] for p in pts[:k]), statistics.median(p[1] for p in pts[-k:])


def describe(times, vals, t_warm):
    allp = pairs(times, vals)
    if not allp:
        return None
    post = pairs(times, vals, t_warm)
    used_all = False
    if len(post) < 3:
        post, used_all = allp, True
    a, b = window_medians(post)
    return {
        "n": len(allp), "start": allp[0][1], "end": allp[-1][1],
        "min": min(p[1] for p in allp), "max": max(p[1] for p in allp),
        "mean_post": sum(p[1] for p in post) / len(post),
        "slope_h": slope_per_hour(post), "med_first": a, "med_last": b,
        "post_n": len(post), "used_all": used_all, "post": post,
    }


def sustained(flags):
    """flags: list of bools -> (fraction, longest run)."""
    if not flags:
        return 0.0, 0
    run = best = 0
    for f in flags:
        run = run + 1 if f else 0
        best = max(best, run)
    return sum(flags) / len(flags), best


def fmt(v, nd=2):
    if v is None:
        return "-"
    if abs(v) >= 1000 or float(v).is_integer():
        return f"{v:,.0f}"
    return f"{v:.{nd}f}"


def fmt_s(v, nd=2):
    if v is None:
        return "-"
    if abs(v) < 0.5 * 10 ** -nd:
        return "0"
    return ("+" if v > 0 else "") + fmt(v, nd)


def local_iso(t):
    return dt.datetime.fromtimestamp(t).astimezone().strftime("%Y-%m-%d %H:%M:%S")

# ----------------------------------------------------------------------------- analysis


def analyze(args):
    findings = []   # (flag, detail)
    notes = []
    lines = []

    times, cols, order = ([], {}, [])
    if args.csv:
        times, cols, order = load_csv(args.csv)
    d_times, d_metrics, d_order, d_lines, d_skipped = ([], {}, [], 0, 0)
    if args.diag:
        if os.path.exists(args.diag):
            d_times, d_metrics, d_order, d_lines, d_skipped = load_diag(args.diag)
        else:
            notes.append(f"diag log {args.diag} not found")

    starts = [x[0] for x in (times, d_times) if x]
    if not starts:
        raise SystemExit("analyze.py: no samples in CSV and no [DIAG] lines - nothing to analyze")
    t0 = times[0] if times else d_times[0]
    t_end = max(x[-1] for x in (times, d_times) if x)
    t_warm = t0 + args.warmup_min * 60.0
    if times and d_times and abs(times[0] - d_times[0]) > 600:
        notes.append("CSV and diag log start more than 10 min apart - are they from the same run?")

    post_span_min = (t_end - t_warm) / 60.0
    if post_span_min < SHORT_RUN_MIN:
        notes.append(f"only {post_span_min:.1f} min of data after warm-up: per-hour slopes are "
                     "extrapolated and low-confidence (use >= 30 min for a verdict)")

    # ---- process / system columns
    proc_stats = {}
    for c in order:
        if c == "elapsed_s":
            continue
        s = describe(times, cols[c], t_warm)
        if s:
            proc_stats[c] = s
            if s["used_all"]:
                notes.append(f"{c}: fewer than 3 samples after warm-up, slope uses all samples")

    for c, label in (("rss_kb", "RSS"), ("pss_kb", "PSS"), ("anon_kb", "RssAnon")):
        s = proc_stats.get(c)
        if s and s["slope_h"] is not None:
            mbh = s["slope_h"] / 1024.0
            span_h = (s["post"][-1][0] - s["post"][0][0]) / 3600.0
            growth_mb = mbh * span_h            # fitted growth over the analysed window
            if mbh > MB_PER_H_LIMIT and growth_mb >= MIN_GROWTH_MB:
                findings.append(("LEAK SUSPECT", f"{label} slope {mbh:+.1f} MB/h (> {MB_PER_H_LIMIT} MB/h), "
                                                 f"fitted growth {growth_mb:+.1f} MB over {span_h*60:.0f} min"))
            elif mbh > MB_PER_H_LIMIT:
                notes.append(f"{label} slope {mbh:+.1f} MB/h but fitted growth only {growth_mb:+.2f} MB "
                             f"(< {MIN_GROWTH_MB} MB) - noise on a short window, not flagged")
    for c, label in (("fds", "open fds"), ("threads", "threads")):
        s = proc_stats.get(c)
        if s and s["slope_h"] is not None and s["med_first"] is not None:
            if s["slope_h"] > 0 and s["med_last"] > s["med_first"]:
                findings.append(("RESOURCE LEAK SUSPECT",
                                 f"{label} increasing: {fmt(s['med_first'])} -> {fmt(s['med_last'])} "
                                 f"(median first/last 20 %), slope {s['slope_h']:+.1f}/h"))
    s = proc_stats.get("gpu_proc_mem_mib")
    if s and s["slope_h"] is not None and s["slope_h"] > GPU_MIB_PER_H_LIMIT and \
            s["med_first"] is not None and s["med_last"] > s["med_first"]:
        findings.append(("GPU LEAK SUSPECT", f"process GPU memory slope {s['slope_h']:+.1f} MiB/h "
                                             f"(> {GPU_MIB_PER_H_LIMIT} MiB/h)"))
    if times and "gpu_proc_mem_mib" in cols and "gpu_proc_mem_mib" not in proc_stats:
        notes.append("gpu_proc_mem_mib empty: nvidia-smi did not list the PID (no GPU context?)")
    s = proc_stats.get("gpu_total_used_mib")
    if s and s["slope_h"] is not None and s["slope_h"] > GPU_MIB_PER_H_LIMIT and \
            not any(f[0] == "GPU LEAK SUSPECT" for f in findings):
        notes.append(f"total GPU memory grows {s['slope_h']:+.1f} MiB/h (other processes or "
                     "driver-internal allocations not attributed to the PID)")

    # ---- diag
    diag_stats = {}
    diag_info = {}
    if d_times:
        for k in d_order:
            s = describe(d_times, d_metrics[k], t_warm)
            if s:
                diag_stats[k] = s
        lat = d_metrics.get("lat_avg_ms")
        if lat:
            cross = {}
            for thr in (100, 500, 1000):
                cross[thr] = next((t for t, v in zip(d_times, lat) if v is not None and v > thr), None)
            diag_info["cross"] = cross
        reasons = []
        s = diag_stats.get("inflight")
        if s:
            if s["slope_h"] is not None and s["slope_h"] > 0 and s["med_first"] is not None \
                    and s["med_last"] - s["med_first"] >= 1:
                reasons.append(f"inflight grows {fmt(s['med_first'])} -> {fmt(s['med_last'])} "
                               f"(slope {s['slope_h']:+.2f}/h)")
            frac, run = sustained([v > INFLIGHT_LIMIT for _, v in s["post"]])
            diag_info["inflight_over"] = (frac, run)
            if frac >= SUSTAIN_FRAC or run >= SUSTAIN_RUN:
                reasons.append(f"inflight > {INFLIGHT_LIMIT} in {frac*100:.0f} % of samples "
                               f"(longest run {run})")
        s = diag_stats.get("lat_avg_ms")
        if s and s["slope_h"] is not None and s["med_first"] is not None:
            growth = s["med_last"] - s["med_first"]
            if s["slope_h"] > LAT_SLOPE_MS_PER_H and growth > LAT_GROWTH_MS:
                reasons.append(f"lat_avg_ms grows {s['med_first']:.1f} -> {s['med_last']:.1f} ms "
                               f"(slope {s['slope_h']:+.1f} ms/h)")
        for r in reasons:
            findings.append(("BACKLOG", r))

        sat = []
        pa, gl = d_metrics.get("paint_avg_ms"), d_metrics.get("gui_lag_max_ms")
        pn = d_metrics.get("paint_n")
        if pa or gl:
            flags = []
            for i, t in enumerate(d_times):
                if t < t_warm and len(d_times) > 3 and d_times[-1] >= t_warm:
                    continue
                p = pa[i] if pa else None
                if pn and pn[i] == 0:
                    p = None
                g = gl[i] if gl else None
                flags.append((p is not None and p > PAINT_LIMIT_MS) or (g is not None and g > GUI_LAG_LIMIT_MS))
            frac, run = sustained(flags)
            diag_info["gui_sat"] = (frac, run)
            if frac >= SUSTAIN_FRAC or run >= SUSTAIN_RUN:
                sat.append(f"paint_avg_ms > {PAINT_LIMIT_MS:.0f} or gui_lag_max_ms > {GUI_LAG_LIMIT_MS:.0f} "
                           f"in {frac*100:.0f} % of samples (longest run {run} s)")
        for r in sat:
            findings.append(("GUI SATURATED", r))
        s = diag_stats.get("rec_drops")
        if s and s["post"] and s["post"][-1][1] > s["post"][0][1]:
            findings.append(("REC DROPS", f"rec_drops {fmt(s['post'][0][1])} -> {fmt(s['post'][-1][1])} "
                                          "after warm-up"))
        if d_skipped:
            notes.append(f"{d_skipped} [DIAG] lines without t= skipped")

    # ---------------------------------------------------------------- text summary
    W = lines.append
    W("=" * 78)
    W("SOAK ANALYSIS")
    W("=" * 78)
    if args.csv:
        W(f"CSV       : {args.csv}  ({len(times)} samples)")
    if args.diag:
        W(f"DIAG      : {args.diag}  ({d_lines} [DIAG] lines, {len(d_times)} parsed)")
    W(f"Start     : {local_iso(t0)}   End: {local_iso(t_end)}   Duration: {(t_end-t0)/60:.1f} min")
    W(f"Warm-up   : first {args.warmup_min:g} min excluded from slopes / flags")
    W("")
    W("VERDICT   : " + ("PASS - no flags" if not findings else
                        "FLAGGED: " + ", ".join(sorted({f[0] for f in findings}))))
    for f, d in findings:
        W(f"  [{f}] {d}")
    for n_ in notes:
        W(f"  note: {n_}")
    W("")

    def table(title, stats, conv):
        W(title)
        W(f"  {'metric':<22}{'start':>12}{'end':>12}{'min':>12}{'max':>12}{'slope/h':>14}")
        for k, s in stats.items():
            f_, unit = conv(k)
            sl = s["slope_h"] * f_ if s["slope_h"] is not None else None
            W(f"  {k + unit:<22}{fmt(s['start']*f_):>12}{fmt(s['end']*f_):>12}{fmt(s['min']*f_):>12}"
              f"{fmt(s['max']*f_):>12}{fmt_s(sl):>14}")
        W("")

    def conv_proc(k):
        if k.endswith("_kb"):
            return 1 / 1024.0, " (MB)"
        return 1.0, ""
    if proc_stats:
        table("PROCESS / SYSTEM (memory columns shown in MB = kB/1024)", proc_stats, conv_proc)
    if diag_stats:
        table("DIAG METRICS", diag_stats, lambda k: (1.0, ""))
        s = diag_stats.get("lat_avg_ms")
        if s:
            W(f"  lat_avg_ms : slope {fmt_s(s['slope_h'])} ms/h, max {fmt(s['max'])} ms, "
              f"median first/last 20 % {fmt(s['med_first'])} / {fmt(s['med_last'])} ms")
        s = diag_stats.get("inflight")
        if s:
            W(f"  inflight   : slope {fmt_s(s['slope_h'])} /h, max {fmt(s['max'])}")
        for thr, t in (diag_info.get("cross") or {}).items():
            W(f"  lat_avg first > {thr:>4} ms : " +
              ("never" if t is None else f"{(t - t0)/60:.1f} min ({local_iso(t)})"))
        if "gui_sat" in diag_info:
            fr, rn = diag_info["gui_sat"]
            W(f"  GUI saturation samples: {fr*100:.1f} % (longest run {rn})")
        W("")
    text = "\n".join(lines)

    ctx = dict(t0=t0, t_end=t_end, t_warm=t_warm, times=times, cols=cols, d_times=d_times,
               d_metrics=d_metrics, d_order=d_order, findings=findings, notes=notes, text=text,
               proc_stats=proc_stats, diag_stats=diag_stats, diag_info=diag_info, args=args)
    return ctx

# ----------------------------------------------------------------------------- SVG charts


SERIES = ["var(--s1)", "var(--s2)", "var(--s3)"]


def downsample(pts, max_pts=1200):
    """min/max bucket downsampling (keeps spikes)."""
    if len(pts) <= max_pts:
        return pts
    nb = max_pts // 2
    t_lo, t_hi = pts[0][0], pts[-1][0]
    span = (t_hi - t_lo) or 1.0
    buckets = [[] for _ in range(nb)]
    for p in pts:
        buckets[min(nb - 1, int((p[0] - t_lo) / span * nb))].append(p)
    out = []
    for b in buckets:
        if not b:
            continue
        lo = min(b, key=lambda p: p[1])
        hi = max(b, key=lambda p: p[1])
        out.extend(sorted({lo, hi}, key=lambda p: p[0]))
    return out


def nice_ticks(lo, hi, n=5):
    if hi == lo:
        hi = lo + (abs(lo) * 0.1 or 1.0)
        lo = lo - (abs(lo) * 0.1 if lo else 0)
    raw = (hi - lo) / n
    mag = 10 ** math.floor(math.log10(raw))
    step = next(m * mag for m in (1, 2, 2.5, 5, 10) if m * mag >= raw)
    start = math.floor(lo / step) * step
    ticks, v = [], start
    while v <= hi + step * 0.5:
        ticks.append(round(v, 10))
        v += step
    return ticks


def tick_label(v):
    a = abs(v)
    if a >= 1e6:
        return f"{v/1e6:g}M"
    if a >= 1e4:
        return f"{v/1e3:g}k"
    return f"{v:g}"


def svg_chart(cid, title, unit, series, t0, t_warm, thresholds=(), note="", t_end=None):
    """series: list of (label, [(t, v)]).  Returns (html, json-data) or None."""
    series = [(l, downsample(p)) for l, p in series if p]
    if not series:
        return None
    W_, H_ = 760, 250
    ml, mr, mt, mb = 58, 110, 14, 34
    pw, ph = W_ - ml - mr, H_ - mt - mb
    xs = [p[0] for _, pts in series for p in pts]
    ys = [p[1] for _, pts in series for p in pts]
    x_lo, x_hi = (0.0, max(1e-9, (max(xs + ([t_end] if t_end else [])) - t0) / 60.0))
    x_lo = min(x_lo, (min(xs) - t0) / 60.0)
    y_lo, y_hi = min(ys), max(ys)
    thr_in = [(v, l) for v, l in thresholds if v is not None]
    # include thresholds in range only if near the data (avoid flattening data)
    for v, _ in thr_in:
        if y_lo <= v <= y_hi * 1.5 or (y_hi > 0 and v <= y_hi * 3):
            y_hi = max(y_hi, v)
    if y_lo >= 0 and y_lo < (y_hi - y_lo) * 0.6:
        y_lo = 0.0
    yt = nice_ticks(y_lo, y_hi)
    y_lo, y_hi = yt[0], yt[-1]
    xt = nice_ticks(x_lo, x_hi, 6)
    xt = [x for x in xt if x_lo - 1e-9 <= x <= x_hi + 1e-9]

    def X(tmin):
        return ml + (tmin - x_lo) / ((x_hi - x_lo) or 1) * pw

    def Y(v):
        return mt + ph - (v - y_lo) / ((y_hi - y_lo) or 1) * ph

    o = [f'<svg id="{cid}" class="chart" viewBox="0 0 {W_} {H_}" role="img" '
         f'aria-label="{html.escape(title)}" preserveAspectRatio="xMidYMid meet">']
    wx = X(min(x_hi, (t_warm - t0) / 60.0))
    if wx > ml + 0.5:
        o.append(f'<rect x="{ml}" y="{mt}" width="{wx-ml:.1f}" height="{ph}" class="warm"/>')
        o.append(f'<text x="{ml+4}" y="{mt+11}" class="warmlbl">warm-up</text>')
    for v in yt:
        y = Y(v)
        o.append(f'<line x1="{ml}" x2="{ml+pw}" y1="{y:.1f}" y2="{y:.1f}" class="grid"/>')
        o.append(f'<text x="{ml-6}" y="{y+4:.1f}" class="tick" text-anchor="end">{tick_label(v)}</text>')
    for x in xt:
        o.append(f'<text x="{X(x):.1f}" y="{mt+ph+18}" class="tick" text-anchor="middle">{x:g}</text>')
    o.append(f'<line x1="{ml}" x2="{ml+pw}" y1="{mt+ph}" y2="{mt+ph}" class="axis"/>')
    o.append(f'<text x="{ml+pw/2}" y="{H_-2}" class="tick" text-anchor="middle">elapsed (min)</text>')
    for v, l in thr_in:
        if y_lo <= v <= y_hi:
            y = Y(v)
            o.append(f'<line x1="{ml}" x2="{ml+pw}" y1="{y:.1f}" y2="{y:.1f}" class="thr"/>')
            o.append(f'<text x="{ml+pw-4}" y="{y-4:.1f}" class="thrlbl" text-anchor="end">{html.escape(l)}</text>')
    data = {"x0": ml, "pw": pw, "xlo": x_lo, "xhi": x_hi, "unit": unit, "series": []}
    label_ys = []
    for i, (l, pts) in enumerate(series):
        col = SERIES[i % len(SERIES)]
        d = " ".join(f"{'M' if j == 0 else 'L'}{X((t - t0)/60):.1f},{Y(v):.1f}" for j, (t, v) in enumerate(pts))
        o.append(f'<path d="{d}" fill="none" stroke="{col}" stroke-width="2" stroke-linejoin="round" '
                 f'stroke-linecap="round" vector-effect="non-scaling-stroke"/>')
        if len(pts) == 1:
            o.append(f'<circle cx="{X((pts[0][0]-t0)/60):.1f}" cy="{Y(pts[0][1]):.1f}" r="4" fill="{col}"/>')
        ly = Y(pts[-1][1])
        for prev in label_ys:
            if abs(ly - prev) < 13:
                ly = prev + 13
        label_ys.append(ly)
        o.append(f'<text x="{ml+pw+8}" y="{ly+4:.1f}" class="dlabel">{html.escape(l)}</text>')
        data["series"].append({"label": l, "color": col,
                               "pts": [[round((t - t0) / 60, 4), v] for t, v in pts]})
    o.append(f'<line class="xhair" x1="0" x2="0" y1="{mt}" y2="{mt+ph}" visibility="hidden"/>')
    o.append(f'<rect class="hit" x="{ml}" y="{mt}" width="{pw}" height="{ph}" fill="transparent"/>')
    o.append("</svg>")
    legend = ""
    if len(series) >= 2:
        legend = '<div class="legend">' + "".join(
            f'<span><i style="background:{SERIES[i % 3]}"></i>{html.escape(l)}</span>'
            for i, (l, _) in enumerate(series)) + "</div>"
    head = (f'<div class="card"><div class="ctitle">{html.escape(title)}'
            f'<span class="unit">{html.escape(unit)}</span></div>{legend}'
            f'<div class="plot" data-chart="{cid}">{"".join(o)}<div class="tip" hidden></div></div>'
            + (f'<div class="cnote">{html.escape(note)}</div>' if note else "") + "</div>")
    return head, (cid, data)


def rate_series(times, vals):
    out = []
    prev = None
    for t, v in zip(times, vals):
        if v is None:
            continue
        if prev and t > prev[0]:
            out.append((t, (v - prev[1]) / (t - prev[0])))
        prev = (t, v)
    return out


def build_html(ctx):
    t0, t_warm = ctx["t0"], ctx["t_warm"]
    times, cols = ctx["times"], ctx["cols"]
    dts, dm = ctx["d_times"], ctx["d_metrics"]

    def P(c, f=1.0):
        if c not in cols:
            return []
        return [(t, v * f) for t, v in pairs(times, cols[c])]

    def D(k):
        return pairs(dts, dm[k]) if k in dm else []

    def slope_note(stats, keys, factor=1.0, unit=""):
        parts = []
        for k, lbl in keys:
            s = stats.get(k)
            if s and s["slope_h"] is not None:
                parts.append(f"{lbl} slope {fmt_s(s['slope_h']*factor)} {unit}/h")
        return " | ".join(parts) + (" (after warm-up)" if parts else "")

    ps, dsx = ctx["proc_stats"], ctx["diag_stats"]
    sections = []
    charts = []
    MB = 1 / 1024.0
    proc_specs = [
        ("Resident memory", "MB", [("RSS", P("rss_kb", MB)), ("PSS", P("pss_kb", MB)), ("Anon", P("anon_kb", MB))],
         [], slope_note(ps, [("rss_kb", "RSS"), ("pss_kb", "PSS"), ("anon_kb", "Anon")], MB, "MB")),
        ("Virtual size / peak RSS", "MB", [("VmSize", P("vmsize_kb", MB)), ("VmHWM", P("vmhwm_kb", MB))], [], ""),
        ("GPU memory (this process)", "MiB", [("proc", P("gpu_proc_mem_mib"))], [],
         slope_note(ps, [("gpu_proc_mem_mib", "")], 1, "MiB")),
        ("GPU memory (whole GPU)", "MiB", [("total used", P("gpu_total_used_mib"))], [],
         slope_note(ps, [("gpu_total_used_mib", "")], 1, "MiB")),
        ("GPU / encoder utilization", "%", [("GPU", P("gpu_util_pct")), ("NVENC", P("enc_util_pct"))], [], ""),
        ("Threads", "count", [("threads", P("threads"))], [], slope_note(ps, [("threads", "")], 1, "")),
        ("Open file descriptors", "count", [("fds", P("fds"))], [], slope_note(ps, [("fds", "")], 1, "")),
        ("CPU", "% of one core", [("cpu", P("cpu_pct"))], [], ""),
        ("Context switches", "per s", [("voluntary", rate_series(times, cols.get("voluntary_ctxt", []))),
                                       ("involuntary", rate_series(times, cols.get("nonvoluntary_ctxt", [])))], [], ""),
        ("System MemAvailable", "MB", [("available", P("sys_mem_available_kb", MB))], [], ""),
    ]
    diag_specs = []
    used = set()

    def dspec(title, unit, keys, thr=(), note_keys=None):
        ser = [(k, D(k)) for k in keys if k in dm]
        if not ser:
            return
        used.update(keys)
        nk = note_keys if note_keys is not None else keys
        diag_specs.append((title, unit, ser, thr, slope_note(dsx, [(k, k) for k in nk], 1, unit)))
    dspec("Frame latency (DeckLink thread -> GUI setFrame)", "ms", ["lat_avg_ms", "lat_max_ms"],
          [(100, "100 ms")], ["lat_avg_ms"])
    dspec("Frames in flight (GUI backlog)", "frames", ["inflight"], [(INFLIGHT_LIMIT, f"limit {INFLIGHT_LIMIT}")])
    dspec("Frame rate", "per s", ["emits", "recv"], note_keys=[])
    dspec("Paint time", "ms", ["paint_avg_ms", "paint_max_ms"], [(PAINT_LIMIT_MS, "33 ms")], ["paint_avg_ms"])
    dspec("Paints", "per s", ["paint_n"], note_keys=[])
    dspec("GUI event-loop lag (50 ms timer lateness)", "ms", ["gui_lag_max_ms"], [(GUI_LAG_LIMIT_MS, "100 ms")])
    dspec("Recorder queue", "frames", ["rec_queue"])
    dspec("Recorder drops (cumulative)", "frames", ["rec_drops"])
    dspec("Recorder grab time", "ms", ["rec_grab_avg_ms", "rec_grab_max_ms"], note_keys=["rec_grab_avg_ms"])
    dspec("Recorder grabs", "per s", ["rec_grab_n"], note_keys=[])
    for k in ctx["d_order"]:
        if k not in used:
            dspec(k, "", [k])

    n = 0
    for group, specs in (("Process & GPU", proc_specs), ("Diagnostics ([DIAG])", diag_specs)):
        parts = []
        for title, unit, ser, thr, note in specs:
            n += 1
            r = svg_chart(f"c{n}", title, unit, ser, t0, t_warm, thr, note, ctx["t_end"])
            if r:
                parts.append(r[0])
                charts.append(r[1])
        if parts:
            sections.append(f'<h2>{html.escape(group)}</h2><div class="grid2">{"".join(parts)}</div>')

    findings = ctx["findings"]
    if findings:
        badge = '<div class="verdict bad"><b>&#9888; FLAGGED</b> ' + html.escape(
            ", ".join(sorted({f[0] for f in findings}))) + "</div>"
        flist = "<ul class='flags'>" + "".join(
            f"<li><span class='tag'>{html.escape(f)}</span> {html.escape(d)}</li>" for f, d in findings) + "</ul>"
    else:
        badge = '<div class="verdict good"><b>&#10003; PASS</b> no flags raised</div>'
        flist = ""
    notes = "".join(f"<li>{html.escape(x)}</li>" for x in ctx["notes"])
    notes = f"<ul class='notes'>{notes}</ul>" if notes else ""

    def stats_table(title, stats, mb_cols):
        if not stats:
            return ""
        rows = []
        for k, s in stats.items():
            f_ = MB if k in mb_cols else 1.0
            unit = " MB" if k in mb_cols else ""
            sl = s["slope_h"] * f_ if s["slope_h"] is not None else None
            rows.append(f"<tr><td>{html.escape(k)}{unit}</td><td>{fmt(s['start']*f_)}</td><td>{fmt(s['end']*f_)}</td>"
                        f"<td>{fmt(s['min']*f_)}</td><td>{fmt(s['max']*f_)}</td><td>{fmt_s(sl)}</td></tr>")
        return (f"<h3>{html.escape(title)}</h3><div class='tw'><table><thead><tr><th>metric</th><th>start</th>"
                "<th>end</th><th>min</th><th>max</th><th>slope / h</th></tr></thead><tbody>"
                + "".join(rows) + "</tbody></table></div>")
    mbc = {c for c in ps if c.endswith("_kb")}
    tables = stats_table("Process / system", ps, mbc) + stats_table("Diagnostics", dsx, set())

    a = ctx["args"]
    meta = (f"{html.escape(os.path.basename(a.csv or ''))}"
            + (f" + {html.escape(os.path.basename(a.diag))}" if a.diag else "")
            + f" &middot; {local_iso(t0)} &rarr; {local_iso(ctx['t_end'])} &middot; "
              f"{(ctx['t_end']-t0)/60:.1f} min &middot; warm-up {a.warmup_min:g} min")
    return HTML_TEMPLATE.format(
        title="Soak report", meta=meta, badge=badge, flist=flist, notes=notes,
        sections="".join(sections), tables=tables, text=html.escape(ctx["text"]),
        data=json.dumps(dict(charts)).replace("</", "<\\/"))


HTML_TEMPLATE = """<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>{title}</title>
<style>
:root {{ color-scheme: light; --bg:#f9f9f7; --surface:#fcfcfb; --ink:#0b0b0b; --ink2:#52514e; --muted:#898781;
  --grid:#e1e0d9; --axis:#c3c2b7; --ring:rgba(11,11,11,.10); --s1:#2a78d6; --s2:#eb6834; --s3:#1baf7a;
  --warm:rgba(137,135,129,.10); --thr:#d03b3b; --good:#0ca30c; --bad:#d03b3b; }}
@media (prefers-color-scheme: dark) {{ :root:not([data-theme="light"]) {{ color-scheme: dark; --bg:#0d0d0d;
  --surface:#1a1a19; --ink:#fff; --ink2:#c3c2b7; --grid:#2c2c2a; --axis:#383835; --ring:rgba(255,255,255,.10);
  --s1:#3987e5; --s2:#d95926; --s3:#199e70; --warm:rgba(137,135,129,.14); }} }}
:root[data-theme="dark"] {{ color-scheme: dark; --bg:#0d0d0d; --surface:#1a1a19; --ink:#fff; --ink2:#c3c2b7;
  --grid:#2c2c2a; --axis:#383835; --ring:rgba(255,255,255,.10); --s1:#3987e5; --s2:#d95926; --s3:#199e70;
  --warm:rgba(137,135,129,.14); }}
* {{ box-sizing: border-box; }}
body {{ margin:0; background:var(--bg); color:var(--ink); font:14px/1.45 system-ui,-apple-system,"Segoe UI",sans-serif; }}
main {{ max-width:1600px; margin:0 auto; padding:24px 16px 48px; }}
h1 {{ font-size:22px; margin:0 0 4px; }} h2 {{ font-size:16px; margin:28px 0 10px; }}
h3 {{ font-size:14px; margin:18px 0 6px; color:var(--ink2); }}
.meta {{ color:var(--ink2); margin-bottom:14px; }}
.verdict {{ display:inline-block; padding:8px 14px; border-radius:8px; border:1px solid var(--ring);
  background:var(--surface); font-size:15px; }}
.verdict.good b {{ color:var(--good); }} .verdict.bad b {{ color:var(--bad); }}
ul.flags, ul.notes {{ margin:10px 0 0; padding-left:20px; }} ul.notes {{ color:var(--ink2); }}
.tag {{ font-weight:600; font-size:12px; padding:1px 6px; border-radius:4px; border:1px solid var(--bad); color:var(--ink); }}
.grid2 {{ display:grid; grid-template-columns:repeat(auto-fill,minmax(min(100%,560px),1fr)); gap:12px; }}
.card {{ background:var(--surface); border:1px solid var(--ring); border-radius:10px; padding:12px 12px 8px; min-width:0; }}
.ctitle {{ font-weight:600; }} .unit {{ color:var(--muted); font-weight:400; margin-left:8px; font-size:12px; }}
.legend {{ display:flex; flex-wrap:wrap; gap:4px 14px; font-size:12px; color:var(--ink2); margin-top:4px; }}
.legend i {{ display:inline-block; width:12px; height:3px; border-radius:2px; vertical-align:middle; margin-right:5px; }}
.plot {{ position:relative; }}
svg.chart {{ width:100%; height:auto; display:block; }}
.grid {{ stroke:var(--grid); stroke-width:1; }} .axis {{ stroke:var(--axis); stroke-width:1; }}
.tick {{ fill:var(--muted); font-size:11px; font-variant-numeric:tabular-nums; }}
.dlabel {{ fill:var(--ink2); font-size:11px; }}
.warm {{ fill:var(--warm); }} .warmlbl {{ fill:var(--muted); font-size:10px; }}
.thr {{ stroke:var(--thr); stroke-width:1; stroke-dasharray:4 3; opacity:.8; }}
.thrlbl {{ fill:var(--ink2); font-size:10px; }}
.xhair {{ stroke:var(--muted); stroke-width:1; }}
.cnote {{ color:var(--muted); font-size:12px; margin-top:2px; }}
.tip {{ position:absolute; pointer-events:none; background:var(--surface); border:1px solid var(--ring);
  box-shadow:0 2px 8px rgba(0,0,0,.15); border-radius:6px; padding:6px 8px; font-size:12px; white-space:nowrap; }}
.tip i {{ display:inline-block; width:8px; height:8px; border-radius:50%; margin-right:5px; }}
.tw {{ overflow-x:auto; }}
table {{ border-collapse:collapse; font-size:12.5px; font-variant-numeric:tabular-nums; background:var(--surface); }}
th, td {{ padding:4px 12px; border-bottom:1px solid var(--grid); text-align:right; }}
th:first-child, td:first-child {{ text-align:left; }} th {{ color:var(--ink2); font-weight:600; }}
pre {{ background:var(--surface); border:1px solid var(--ring); border-radius:8px; padding:12px; overflow-x:auto; font-size:12px; }}
</style></head><body><main>
<h1>Soak report</h1>
<div class="meta">{meta}</div>
{badge}{flist}{notes}
{sections}
<h2>Statistics</h2>{tables}
<h2>Text summary</h2><pre>{text}</pre>
</main>
<script>
const DATA = {data};
document.querySelectorAll('.plot').forEach(function (plot) {{
  const d = DATA[plot.dataset.chart]; if (!d) return;
  const svg = plot.querySelector('svg'), hit = svg.querySelector('.hit'), xh = svg.querySelector('.xhair');
  const tip = plot.querySelector('.tip');
  function fmt(v) {{ return Math.abs(v) >= 100 ? v.toFixed(0) : Math.abs(v) >= 10 ? v.toFixed(1) : v.toFixed(2); }}
  function nearest(pts, x) {{ let lo = 0, hi = pts.length - 1;
    while (hi - lo > 1) {{ const m = (lo + hi) >> 1; if (pts[m][0] < x) lo = m; else hi = m; }}
    return Math.abs(pts[lo][0] - x) <= Math.abs(pts[hi][0] - x) ? pts[lo] : pts[hi]; }}
  hit.addEventListener('mousemove', function (e) {{
    const r = svg.getBoundingClientRect(), vb = svg.viewBox.baseVal, sx = r.width / vb.width;
    const px = (e.clientX - r.left) / sx;
    const x = d.xlo + (px - d.x0) / d.pw * (d.xhi - d.xlo);
    xh.setAttribute('x1', px); xh.setAttribute('x2', px); xh.setAttribute('visibility', 'visible');
    let h = '<b>' + x.toFixed(2) + ' min</b>';
    d.series.forEach(function (s) {{ if (!s.pts.length) return; const p = nearest(s.pts, x);
      h += '<br><i style="background:' + s.color + '"></i>' + s.label + ': ' + fmt(p[1]) + ' ' + d.unit; }});
    tip.innerHTML = h; tip.hidden = false;
    const left = e.clientX - plot.getBoundingClientRect().left;
    tip.style.left = Math.min(left + 12, plot.clientWidth - tip.offsetWidth - 4) + 'px'; tip.style.top = '8px';
  }});
  hit.addEventListener('mouseleave', function () {{ tip.hidden = true; xh.setAttribute('visibility', 'hidden'); }});
}});
</script></body></html>
"""


def main():
    ap = argparse.ArgumentParser(description="Analyze monitor.sh CSV (+ optional [DIAG] log).")
    ap.add_argument("csv", nargs="?", help="CSV from monitor.sh (may be omitted if --diag given)")
    ap.add_argument("--diag", help="log file containing [DIAG] lines")
    ap.add_argument("--warmup-min", type=float, default=5.0, help="minutes excluded from slopes (default 5)")
    ap.add_argument("--out", help="HTML report path (default: <csv>.html)")
    args = ap.parse_args()
    if not args.csv and not args.diag:
        ap.error("need a CSV and/or --diag")
    if args.csv and not os.path.exists(args.csv):
        print(f"analyze.py: {args.csv} not found", file=sys.stderr)
        return 2
    ctx = analyze(args)
    print(ctx["text"])
    out = args.out or (os.path.splitext(args.csv or args.diag)[0] + "_report.html")
    with open(out, "w") as f:
        f.write(build_html(ctx))
    print(f"HTML report: {out}")
    return 1 if ctx["findings"] else 0


if __name__ == "__main__":
    sys.exit(main())
