import sys, re
p = r'C:\serverside_re\accountinventory\analysis\decomp\unknown.c'
lines = open(p, encoding='utf-8', errors='replace').read().splitlines()
fmts = sys.argv[1:]
hdr = ''
for i, l in enumerate(lines):
    if '===== sub_' in l:
        parts = l.strip().split(); hdr = parts[1] if len(parts) > 1 else ''
    for f in fmts:
        if f in l:
            print(i+1, hdr, '|', l.strip()[:200])
