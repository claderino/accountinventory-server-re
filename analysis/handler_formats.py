import re, struct, sys
decomp = sys.argv[1]
image = sys.argv[2]
data = open(image, 'rb').read()
IB = 0x7ff63b280000
lines = open(decomp, encoding='utf-8', errors='replace').read().splitlines()
hdr = ''
funcs = {}
buf = []
for l in lines:
    m = re.match(r'// ===== sub_([0-9A-F]+)', l)
    if m:
        if hdr: funcs[hdr] = buf
        hdr = m.group(1); buf = [l]
    elif hdr: buf.append(l)
if hdr: funcs[hdr] = buf

handlers = {
 '3c990':'lobby cmd4 get-goods-count', '3d430':'lobby cmd7 list-goods',
 '3e030':'lobby cmd10 take-goods', '3eab0':'lobby cmd13 service-item-list',
 '3f370':'lobby cmd16 item-info', '3f9c0':'lobby cmd19 try-consume',
 '40050':'lobby cmd22 finish-consume', '406a0':'lobby cmd25 cancel-consume',
 '40bd0':'lobby cmd28 consume', '41100':'lobby cmd32 retry-take',
 '41800':'lobby cmd33 logout',
 '49ae0':'world cmd4 get-goods-count', '4a4e0':'world cmd7 list-goods',
 '4b070':'world cmd10 take-goods', '4bca0':'world cmd13', '4ca40':'world cmd16',
 '4d030':'world cmd19', '4da80':'world cmd22', '4e390':'world cmd25',
 '4f2b0':'world cmd28', '4fa70':'world cmd31', '50570':'world cmd34',
 '1c200':'manage cmd4 restore',
}
for rva, name in handlers.items():
    b = funcs.get(rva.upper(), [])
    fmts = []
    for l in b:
        # decompose calls with a string literal or DAT pointer as 2nd arg
        for m in re.finditer(r'FUN_\w+\(param_\w+,\s*(&?DAT_([0-9A-F]{16})|"([^"]+)")', l):
            if m.group(3):
                fmts.append(m.group(3))
            elif m.group(2):
                va = int(m.group(2), 16); off = va - IB
                if 0 <= off < len(data):
                    s = data[off:off+80].split(b'\x00')[0]
                    if s and all(32 <= c < 127 for c in s): fmts.append(s.decode())
        for m in re.finditer(r'FUN_\w+\([^,]+,\s*"([^"]+)"', l):
            if re.fullmatch(r'[cQhEUuidfqosbKPC;]{2,}', m.group(1)):
                fmts.append(m.group(1))
    print('%-36s %s' % (name, ', '.join(dict.fromkeys(fmts))))
