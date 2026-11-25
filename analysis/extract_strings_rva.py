#!/usr/bin/env python3
"""Extract ASCII + UTF16LE strings from the unpacked PE with RVAs."""
import re, struct, sys
path = sys.argv[1]
out = sys.argv[2]
data = open(path,'rb').read()
pe = struct.unpack_from('<I', data, 0x3C)[0]
nsec = struct.unpack_from('<H', data, pe+6)[0]
optsz = struct.unpack_from('<H', data, pe+20)[0]
secs = []
for i in range(nsec):
    s = pe+24+optsz+i*40
    name = data[s:s+8].rstrip(b'\0').decode('latin1')
    vsize,vaddr,rsz,rptr = struct.unpack_from('<IIII', data, s+8)
    secs.append((name,vaddr,vsize,rptr,rsz))

def rva2off(rva):
    for name,va,vs,rp,rs in secs:
        if va <= rva < va+max(vs,rs):
            return rp + (rva-va)
    return None

with open(out,'w',encoding='utf-8',errors='replace') as f:
    # ASCII strings
    for m in re.finditer(rb'[\x20-\x7e]{3,}', data):
        off = m.start()
        rva = None
        for name,va,vs,rp,rs in secs:
            if rp <= off < rp+rs:
                rva = va + (off-rp); sec=name; break
        if rva is None: continue
        s = m.group().decode('ascii','replace')
        f.write(f"ASCII\t{rva:08X}\t{sec}\t{s}\n")
    # UTF16
    for m in re.finditer(rb'(?:[\x20-\x7e]\x00){3,}', data):
        off = m.start()
        rva = None
        for name,va,vs,rp,rs in secs:
            if rp <= off < rp+rs:
                rva = va + (off-rp); sec=name; break
        if rva is None: continue
        s = m.group().decode('utf-16-le','replace')
        f.write(f"U16\t{rva:08X}\t{sec}\t{s}\n")
print("wrote", out)
