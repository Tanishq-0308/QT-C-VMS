#!/usr/bin/env python3
"""Summarise LeakSanitizer output: separate leaks whose allocation stack passes
through application source (APP_DIR, excluding tests/) from pure Qt/system
noise.  Usage: summarize_lsan.py APP_DIR log1 [log2 ...]"""
import re
import sys
from collections import defaultdict

app_dir = sys.argv[1].rstrip('/') + '/'
hdr = re.compile(r'^(Direct|Indirect) leak of (\d+) byte\(s\) in (\d+) object\(s\) allocated from:')
frm = re.compile(r'^\s+#(\d+) 0x[0-9a-f]+ in (.*?) (\S+)$|^\s+#(\d+) 0x[0-9a-f]+ +\((\S+)\)$')


def is_app(loc):
    if not loc.startswith(app_dir):
        return False
    rel = loc[len(app_dir):]
    return not (rel.startswith('tests/') or rel.startswith('build'))


for log in sys.argv[2:]:
    blocks = []
    cur = None
    with open(log, errors='replace') as f:
        for line in f:
            m = hdr.match(line)
            if m:
                cur = {'kind': m.group(1), 'bytes': int(m.group(2)), 'objs': int(m.group(3)), 'frames': []}
                blocks.append(cur)
                continue
            if cur is not None:
                fm = frm.match(line.rstrip('\n'))
                if fm:
                    if fm.group(2) is not None:
                        cur['frames'].append((fm.group(2), fm.group(3)))
                    else:
                        cur['frames'].append(('?', fm.group(5)))
                elif line.strip() == '':
                    cur = None
    app = defaultdict(lambda: [0, 0, 0])
    other = defaultdict(lambda: [0, 0, 0])
    for b in blocks:
        app_frames = [(fn, loc) for fn, loc in b['frames'] if is_app(loc)]
        if app_frames:
            key = ' <- '.join(f"{fn.split('(')[0]} {loc[len(app_dir):]}" for fn, loc in app_frames[:3])
            e = app[(b['kind'], key)]
            e[0] += b['bytes']; e[1] += b['objs']; e[2] += 1
        else:
            top = next((loc for fn, loc in b['frames']
                        if 'asan' not in loc and 'libstdc++' not in loc and 'operator new' not in fn
                        and 'malloc' not in fn and 'calloc' not in fn and 'realloc' not in fn), '?')
            top = re.sub(r'\+0x[0-9a-f]+', '', top.split('/')[-1])
            e = other[(b['kind'], top)]
            e[0] += b['bytes']; e[1] += b['objs']; e[2] += 1
    print(f"==== {log}")
    print(f"total leak reports: {len(blocks)}; bytes: {sum(b['bytes'] for b in blocks)}")
    print("-- leaks with application frames (top 3 app frames, innermost first):")
    if not app:
        print("   (none)")
    for (kind, key), (by, ob, n) in sorted(app.items(), key=lambda kv: -kv[1][0]):
        print(f"   {kind:8s} {by:8d} B {ob:6d} obj  [{n} stacks]  {key}")
    print("-- leaks without application frames (grouped by first non-allocator frame module):")
    for (kind, top), (by, ob, n) in sorted(other.items(), key=lambda kv: -kv[1][0])[:25]:
        print(f"   {kind:8s} {by:8d} B {ob:6d} obj  [{n} stacks]  {top}")
