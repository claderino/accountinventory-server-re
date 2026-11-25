import re, struct, sys
decomp = sys.argv[1]
image = sys.argv[2]
data = open(image, 'rb').read()
IB = 0x7ff63b280000
def rva_off(rva):
    return rva  # rebuilt PE: rva == file offset
lines = open(decomp, encoding='utf-8', errors='replace').read().splitlines()
wanted = set(x.lower() for x in sys.argv[3:])
hdr = ''
buf = []
funcs = {}
for l in lines:
    m = re.match(r'// ===== sub_([0-9A-F]+)', l)
    if m:
        if hdr: funcs[hdr] = buf
        hdr = m.group(1).lower(); buf = [l]
    elif hdr:
        buf.append(l)
if hdr: funcs[hdr] = buf
for rva in sorted(wanted):
    b = funcs.get(rva, [])
    print('\n===== sub_%s =====' % rva.upper())
    seen = set()
    for l in b:
        for addr in re.findall(r'&DAT_([0-9A-F]{16})', l):
            va = int(addr, 16)
            off = va - IB
            if not (0 <= off < len(data)): continue
            raw = data[off:off+120]
            # try ASCII format-like string
            s = raw.split(b'\x00')[0]
            if s and all(32 <= c < 127 for c in s) and s not in seen:
                seen.add(s)
                print('  %s -> %r' % (addr, s[:100].decode()))
