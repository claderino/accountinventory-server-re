#!/usr/bin/env python3
"""Fetch a fresh cmd8 goods-list reply from the NP-enabled oracle and save its body."""
import socket, struct, uuid, sys

def frame(cmd, payload): return struct.pack('<HH',4+len(payload),cmd)+payload
ACC = uuid.UUID('7144F687-BD7A-4CA7-B94A-C3F19644CDDF').bytes_le
TID = 0x1122334455
s = socket.create_connection(('127.0.0.1', 23700), timeout=5)
s.sendall(frame(1, struct.pack('<H',7)+struct.pack('<HH',7801,20100)))
s.settimeout(0.5)
try: s.recv(4096)
except Exception: pass
s.sendall(frame(7, struct.pack('<Q',TID)+ACC+struct.pack('<B',1)))
hdr = s.recv(4)
L, cmd = struct.unpack('<HH', hdr)
body = b''
while len(body) < L-4:
    body += s.recv(L-4-len(body))
s.close()
print('cmd', cmd, 'bodylen', len(body))
open(r'C:\serverside_re\accountinventory\analysis\cmd8_body.hex','w').write(body.hex())
print(body.hex())
