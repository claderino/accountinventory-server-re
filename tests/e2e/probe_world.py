#!/usr/bin/env python3
"""Probe world-side commands with a valid world id (7801 from gamed-list)."""
import socket, struct, uuid
def frame(cmd,p): return struct.pack('<HH',4+len(p),cmd)+p
def recv(s,t=2.5):
    s.settimeout(t); out=[]
    while True:
        try:
            h=s.recv(4)
            if len(h)<4: break
            L,c=struct.unpack('<HH',h); b=b''
            while len(b)<L-4: b+=s.recv(L-4-len(b))
            out.append((c,b.hex()))
        except socket.timeout: break
    return out
def probe(cmd,payload,label):
    s=socket.create_connection(('127.0.0.1',23701),timeout=5)
    s.sendall(HW); r=recv(s,0.4)
    if not r: print(label,'HELLO FAILED'); s.close(); return
    s.sendall(frame(cmd,payload)); r=recv(s); s.close()
    print('%-26s -> %s'%(label,[(c,b[:100]) for c,b in r[:3]]))
ACC=uuid.UUID('7144F687-BD7A-4CA7-B94A-C3F19644CDDF').bytes_le
TID=0x1122334455
HW=frame(1,struct.pack('<H',8)+struct.pack('<HH',7801,20100))
cases=[
 (4,struct.pack('<Q',TID)+ACC,'W cmd4 get-goods-count'),
 (7,struct.pack('<Q',TID)+ACC+struct.pack('<B',1),'W cmd7 list-goods'),
 (10,struct.pack('<QQ',TID,80407)+ACC,'W cmd10 take-goods'),
 (13,struct.pack('<Qh',TID,7801)+ACC+struct.pack('<BQ',1,0),'W cmd13'),
 (16,struct.pack('<QQ',TID,2834453015476308139)+ACC,'W cmd16'),
 (19,struct.pack('<QQQ',2834453015476308139,946,0)+ACC,'W cmd19'),
 (22,struct.pack('<QQQH',2834453015476308139,946,0,1)+ACC,'W cmd22'),
 (25,struct.pack('<QQQ',2834453015476308139,946,0)+ACC,'W cmd25'),
 (28,struct.pack('<QQQ',2834453015476308139,946,0)+ACC,'W cmd28'),
 (31,struct.pack('<Q',TID)+ACC,'W cmd31'),
 (34,struct.pack('<Q',TID)+ACC,'W cmd34'),
]
for cmd,p,label in cases:
    try: probe(cmd,p,label)
    except Exception as e: print(label,'ERR',repr(e))
