#!/usr/bin/env python3
"""Try every format-like string against a captured response body to find an exact fit."""
import re, struct, sys
sys.path.insert(0, r'C:\serverside_re\accountinventory\analysis')
from decode_fmt3 import parse

body = bytes.fromhex(open(r'C:\serverside_re\accountinventory\analysis\cmd8_body.hex').read())
lines = open(r'C:\serverside_re\accountinventory\analysis\strings_rva.txt', encoding='utf-8', errors='replace').read().splitlines()
cands = []
for l in lines:
    parts = l.split('\t')
    if len(parts) < 4: continue
    if parts[0] != 'ASCII': continue
    s = parts[3]
    if len(s) < 3 or len(s) > 60: continue
    if not re.fullmatch(r'[cQhEUuidfqosbKPC;]+', s): continue
    if 'C' not in s: continue
    cands.append(s)
print('candidates', len(cands))
results = []
for fmt in cands:
    try:
        out = []
        used = parse(body, 0, fmt, '', out)
        if used == len(body):
            results.append((fmt, len(out), [v for _,_,v in out][:12]))
    except Exception:
        pass
for fmt, n, vals in results:
    print(n, fmt, vals)
print('exact fits', len(results))
