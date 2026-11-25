#!/usr/bin/env python3
"""Find RIP-relative xrefs to packet-format strings and dump surrounding disasm."""
import struct, sys
from capstone import *
from capstone.x86 import *

PE = sys.argv[1]
data = open(PE,'rb').read()
pe = struct.unpack_from('<I', data, 0x3C)[0]
nsec = struct.unpack_from('<H', data, pe+6)[0]
optsz = struct.unpack_from('<H', data, pe+20)[0]
imagebase = struct.unpack_from('<Q', data, pe+24+24)[0]
secs=[]
for i in range(nsec):
    s = pe+24+optsz+i*40
    name = data[s:s+8].rstrip(b'\0').decode('latin1')
    vsize,vaddr,rsz,rptr = struct.unpack_from('<IIII', data, s+8)
    secs.append((name,vaddr,vsize,rptr,rsz))

def rva2off(rva):
    for name,va,vs,rp,rs in secs:
        if va <= rva < va+max(vs,rs):
            return rp+(rva-va)
    return None

def off2rva(off):
    for name,va,vs,rp,rs in secs:
        if rp <= off < rp+rs:
            return va+(off-rp)
    return None

# target RVAs from argv
targets = [int(x,16) for x in sys.argv[2:]]
# disassemble .text
text = [s for s in secs if s[0]=='.text'][0]
_,va,vs,rp,rs = text
code = data[rp:rp+rs]
md = Cs(CS_ARCH_X86, CS_MODE_64)
md.detail = True

# find references: for each instruction with RIP-relative memory operand resolving to target
hits = {}
for ins in md.disasm(code, imagebase+va):
    for op in ins.operands:
        if op.type == X86_OP_MEM and op.mem.base == X86_REG_RIP:
            addr = ins.address + ins.size + op.mem.disp
            rva = addr - imagebase
            if rva in targets:
                hits.setdefault(rva, []).append(ins.address-imagebase)

for rva in targets:
    off = rva2off(rva)
    s = data[off:off+80].split(b'\0')[0].decode('ascii','replace')
    print(f"\n=== fmt @{rva:08X} '{s}' xrefs: {[hex(x) for x in hits.get(rva,[])]}")
    for h in hits.get(rva,[]):
        # dump 60 instructions before and 12 after h
        start = h-240
        if start < va: start = va
        snippet = data[rp+(start-va):rp+(h-va+64)]
        print(f"--- around {h:#x} ---")
        for ins in md.disasm(snippet, imagebase+start):
            mark = ' <<<' if ins.address-imagebase==h else ''
            print(f"  {ins.address-imagebase:08X}  {ins.mnemonic:8s} {ins.op_str}{mark}")
