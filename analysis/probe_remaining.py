import socket, struct, uuid

def frame(cmd, p): return struct.pack('<HH', 4 + len(p), cmd) + p

def recv(sk, t=1.5):
    sk.settimeout(t); out = []
    while True:
        try:
            h = sk.recv(4)
            if len(h) < 4: break
            L, c = struct.unpack('<HH', h); b = b''
            while len(b) < L - 4: b += sk.recv(L - 4 - len(b))
            out.append((c, b.hex()))
        except socket.timeout: break
    return out

def probe(port, cmd, payload, hello):
    sk = socket.create_connection(('127.0.0.1', port), timeout=5)
    sk.sendall(hello); recv(sk, 0.3)
    sk.sendall(frame(cmd, payload)); r = recv(sk); sk.close(); return r

A = uuid.UUID('7144F687-BD7A-4CA7-B94A-C3F19644CDDF').bytes_le
T = 0x1122334455
HL = frame(1, struct.pack('<H', 7) + struct.pack('<HH', 7801, 20100))
HW = frame(1, struct.pack('<H', 8) + struct.pack('<HH', 7801, 20100))

# payload shapes: bare tid, tid + account, consume-like, take-goods-like
shapes = {
    'tid': struct.pack('<Q', T),
    'tid+acc': struct.pack('<Q', T) + A,
    'consume': struct.pack('<QQQ', 2834453015476308139, 946, 0) + A,
    'take': struct.pack('<QQ', T, 80407) + A + struct.pack('<i', 2),
    'empty': b'',
}
cases = []
for cmd in (32, 33):
    for shape, p in shapes.items():
        cases.append(('L cmd%d/%s' % (cmd, shape), 23700, 22700, cmd, p, HL))
for cmd in (10, 25, 28, 31):
    for shape, p in shapes.items():
        cases.append(('W cmd%d/%s' % (cmd, shape), 23701, 22701, cmd, p, HW))

for name, op, mp, cmd, p, hello in cases:
    try: o = probe(op, cmd, p, hello)
    except Exception as e: o = [('ERR', repr(e))]
    try: m = probe(mp, cmd, p, hello)
    except Exception as e: m = [('ERR', repr(e))]
    status = 'MATCH' if o == m else 'DIFF'
    print(status, name, 'oracle', o, 'ours', m)
