#!/usr/bin/env python3
"""Probe remaining lobby/world/manage commands on the NP-enabled oracle."""
import socket, struct, uuid, time

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
def probe(port,cmd,payload,hello,label):
    s=socket.create_connection(('127.0.0.1',port),timeout=5)
    s.sendall(hello); recv(s,0.3); s.sendall(frame(cmd,payload)); r=recv(s); s.close()
    print('%-28s -> %s'%(label,[(c,b[:80]) for c,b in r[:3]]))

ACC=uuid.UUID('7144F687-BD7A-4CA7-B94A-C3F19644CDDF').bytes_le
TID=0x1122334455
HL=frame(1,struct.pack('<H',7)+struct.pack('<HH',7801,20100))
HW=frame(1,struct.pack('<H',8)+struct.pack('<HH',10070,20100))
HM=frame(1,struct.pack('<H',1))
cases=[
 ('L cmd10 take-goods',23700,10,struct.pack('<Qi',TID,80407)+ACC+struct.pack('<i',5),HL),
 ('L cmd28 consume',23700,28,struct.pack('<QQQ',2834453015476308139,946,0)+ACC,HL),
 ('L cmd32 retry-take',23700,32,struct.pack('<Q',TID)+ACC,HL),
 ('L cmd33 logout',23700,33,struct.pack('<Q',TID)+ACC,HL),
 ('M cmd4 restore',33700,4,struct.pack('<QQ',TID,0x123)+ACC+struct.pack('<Qi',0,2),HM),
 ('W cmd4',23701,4,struct.pack('<Q',TID)+ACC,HW),
 ('W cmd10',23701,10,struct.pack('<Qi',TID,80407)+ACC+struct.pack('<i',5),HW),
]
for label,port,cmd,p,hello in cases:
    try: probe(port,cmd,p,hello,label)
    except Exception as e: print(label,'ERR',repr(e))
