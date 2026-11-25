#!/usr/bin/env python3
"""Replay captured NP handshake + GetGoodsCount against the real gateway."""
import socket, re
p = r'C:\serverside_re\accountinventory\tests\e2e\np_capture.log'
reqs = []
for line in open(p, encoding='ascii'):
    parts = line.strip().split()
    if len(parts) < 4 or parts[1] != 'C2G': continue
    try: d = bytes.fromhex(parts[3])
    except: continue
    t = d.decode('utf-8', 'replace')
    if ('/Sts/Connect' in t or '/Gate/RegisterService' in t or
        '/Warehouse.bnsgrnTH/GetGoodsCount' in t):
        reqs.append(d)

s = socket.create_connection(('127.0.0.1', 6601), timeout=5)
s.settimeout(2)
for i, req in enumerate(reqs):
    # update sequence numbers to 1,2,3 in both header and XML for the later ones
    text = req.decode('utf-8', 'replace')
    text = re.sub(r'(?m)^s:\d+$', 's:%d' % (i+1), text, count=1)
    s.sendall(text.encode())
    data = b''
    try:
        while True:
            d = s.recv(65536)
            if not d: break
            data += d
            if b'</Reply>' in data or b'</Error' in data or b'STS/1.0 400' in data: break
    except socket.timeout:
        pass
    print('--- req', i, text.split('\r\n')[0])
    print('res', data[:300].decode('utf-8', 'replace'))
s.close()
