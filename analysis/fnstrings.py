#!/usr/bin/env python3
"""Print string literals referenced inside named decompiled functions."""
import re, sys
p = sys.argv[1]
lines = open(p, encoding='utf-8', errors='replace').read().splitlines()
want = set(x.lower() for x in sys.argv[2:])
hdr = ''
buf = []
funcs = {}
for l in lines:
    m = re.match(r'// ===== sub_([0-9A-F]+)', l)
    if m:
        if hdr: funcs[hdr] = buf
        hdr = m.group(1).lower()
        buf = [l]
    elif hdr:
        buf.append(l)
if hdr: funcs[hdr] = buf
for rva in sorted(want):
    b = funcs.get(rva, [])
    print(f"\n===== sub_{rva.upper()} ({len(b)} lines) =====")
    seen = set()
    for l in b:
        for s in re.findall(r'L"((?:[^"\\]|\\.)*)"', l):
            if s not in seen and s:
                seen.add(s)
                print('  ', s[:180])
        for s in re.findall(r'"([cQhEUuidfqosbKPC;]{3,})"', l):
            if s not in seen:
                seen.add(s)
                print('  FMT', s)
