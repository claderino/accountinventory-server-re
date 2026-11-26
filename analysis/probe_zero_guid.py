import socket, struct, uuid, time

def frame(cmd, p): return struct.pack('<HH', 4 + len(p), cmd) + p

def recv(sk, t=2):
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

Z = uuid.UUID('00000000-0000-0000-0000-000000000000').bytes_le
T = 0x1122334455
HL = frame(1, struct.pack('<H', 7) + struct.pack('<HH', 7801, 20100))
HW = frame(1, struct.pack('<H', 8) + struct.pack('<HH', 7801, 20100))
HM = frame(1, struct.pack('<H', 1))

cases = [
    ('L cmd4', 23700, 22700, 4, struct.pack('<Q', T) + Z, HL),
    ('L cmd7', 23700, 22700, 7, struct.pack('<Q', T) + Z + struct.pack('<BQ', 1, 0), HL),
    ('L cmd10', 23700, 22700, 10, struct.pack('<QQ', T, 80407) + Z + struct.pack('<i', 2), HL),
    ('L cmd13', 23700, 22700, 13, struct.pack('<Q', T) + Z + struct.pack('<BQ', 1, 0), HL),
    ('L cmd16', 23700, 22700, 16, struct.pack('<QQ', T, 2834453015476308139) + Z + struct.pack('<Q', 0), HL),
    ('L cmd19', 23700, 22700, 19, struct.pack('<QQQ', 2834453015476308139, 946, 0) + Z, HL),
    ('L cmd22', 23700, 22700, 22, struct.pack('<QQQH', 2834453015476308139, 946, 0, 1) + Z, HL),
    ('L cmd25', 23700, 22700, 25, struct.pack('<QQQ', 2834453015476308139, 946, 0) + Z, HL),
    ('L cmd28', 23700, 22700, 28, struct.pack('<QQQ', 2834453015476308139, 946, 0) + Z, HL),
    ('W cmd4', 23701, 22701, 4, struct.pack('<Q', T) + Z, HW),
    ('W cmd7', 23701, 22701, 7, struct.pack('<Q', T) + Z + struct.pack('<B', 1), HW),
    ('W cmd13', 23701, 22701, 13, struct.pack('<Qh', T, 7801) + Z + struct.pack('<BQ', 1, 0), HW),
    ('W cmd16', 23701, 22701, 16, struct.pack('<QQ', T, 2834453015476308139), HW),
    ('W cmd19', 23701, 22701, 19, struct.pack('<QQQ', 2834453015476308139, 946, 0) + Z, HW),
    ('W cmd22', 23701, 22701, 22, struct.pack('<QQQH', 2834453015476308139, 946, 0, 1) + Z, HW),
    ('W cmd34', 23701, 22701, 34, struct.pack('<QQ', T, 0), HW),
    ('M cmd4', 33700, 32700, 4, struct.pack('<QQ', T, 99990001) + Z + struct.pack('<Qi', 12345, 2), HM),
]
for name, op, mp, cmd, p, hello in cases:
    try: o = probe(op, cmd, p, hello)
    except Exception as e: o = [('ERR', repr(e))]
    try: m = probe(mp, cmd, p, hello)
    except Exception as e: m = [('ERR', repr(e))]
    print(name, 'MATCH' if o == m else 'DIFF', 'oracle', o, 'ours', m)
