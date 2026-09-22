#!/usr/bin/env python3
"""Keep only LeakSanitizer / ASan report blocks whose stack mentions application code.
usage: filter_lsan.py <log> [pattern ...]   (default pattern: VideoRecorder)"""
import re, sys
log = open(sys.argv[1], errors="replace").read().splitlines()
pats = sys.argv[2:] or ["VideoRecorder"]
blocks, cur = [], None
for line in log:
    if re.match(r"^(Direct|Indirect) leak of", line) or "ERROR: AddressSanitizer" in line:
        if cur: blocks.append(cur)
        cur = [line]
    elif cur is not None:
        if line.strip() == "" and len(cur) > 1:
            blocks.append(cur); cur = None
        else:
            cur.append(line)
if cur: blocks.append(cur)
keep = [b for b in blocks if any(p in l for l in b for p in pats)]
tot = 0
for b in keep:
    m = re.match(r"^(Direct|Indirect) leak of (\d+) byte", b[0])
    if m: tot += int(m.group(2))
    print("\n".join(b)); print()
print(f"# {len(keep)} of {len(blocks)} report blocks reference {pats}; leaked bytes in kept blocks: {tot}")
