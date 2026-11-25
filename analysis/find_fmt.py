import re, sys
p = sys.argv[1]
wanted = set(sys.argv[2:])
lines = open(p, encoding='utf-8', errors='replace').read().splitlines()
hdr = ''
for i, l in enumerate(lines):
    if '===== sub_' in l:
        hdr = l.strip().split()[1]
    if hdr.lower() not in {w.lower() for w in wanted}:
        continue
    if 'FUN_7ff63b2d4960(' in l or 'FUN_7ff63b2d4a00(' in l or 'FUN_7ff63b2841b0(' in l:
        m = re.search(r'"([^"]+)"', l)
        if m:
            print(hdr, '|', l.strip()[:160])
