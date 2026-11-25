import re, sys
decomp = r'C:\serverside_re\accountinventory\analysis\decomp\unknown.c'
image = r'C:\serverside_re\accountinventory\analysis\mem\AccountInventoryDaemon.exe.mem.exe'
data = open(image,'rb').read(); IB = 0x7ff63b280000
lines = open(decomp, encoding='utf-8', errors='replace').read().splitlines()
hdr=''; funcs={}; buf=[]
for l in lines:
    m=re.match(r'// ===== sub_([0-9A-F]+)',l)
    if m:
        if hdr: funcs[hdr]=buf
        hdr=m.group(1); buf=[l]
    elif hdr: buf.append(l)
if hdr: funcs[hdr]=buf
names={'49ae0':'cmd4','4a4e0':'cmd7','4b070':'cmd10','4bca0':'cmd13','4ca40':'cmd16',
       '4d030':'cmd19','4da80':'cmd22','4e390':'cmd25','4f2b0':'cmd28','4fa70':'cmd31','50570':'cmd34'}
for rva,name in names.items():
    b=funcs.get(rva.upper(),[])
    fmts=[]
    for l in b:
        for m in re.finditer(r'FUN_\w+\([^,]+,\s*(&?DAT_([0-9A-F]{16})|"([^"]+)")',l):
            if m.group(3): fmts.append(m.group(3))
            elif m.group(2):
                off=int(m.group(2),16)-IB
                if 0<=off<len(data):
                    s=data[off:off+80].split(b'\0')[0]
                    if s and all(32<=c<127 for c in s): fmts.append(s.decode())
        for m in re.finditer(r'FUN_\w+\([^,]+,\s*"([cQhEUuidfqosbKPC;]{2,})"',l):
            fmts.append(m.group(1))
    print('%-6s %s'%(name,', '.join(dict.fromkeys(fmts))))
