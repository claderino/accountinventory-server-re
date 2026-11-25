#!/usr/bin/env python3
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
def probe(port,cmd,payload,hello,label):
    s=socket.create_connection(('127.0.0.1',port),timeout=5)
    s.sendall(hello); recv(s,0.3); s.sendall(frame(cmd,payload)); r=recv(s); s.close()
    print('%-28s -> %s'%(label,[(c,b[:100]) for c,b in r[:3]]))
ACC=uuid.UUID('7144F687-BD7A-4CA7-B94A-C3F19644CDDF').bytes_le
TID=0x1122334455
HL=frame(1,struct.pack('<H',7)+struct.pack('<HH',7801,20100))
cases=[
 ('L cmd10 take-goods',23700,10,struct.pack('<QQ',TID,80407)+ACC,HL),
 ('L cmd32 retry-take',23700,32,ACC,HL),
 ('L cmd33 logout',23700,33,ACC,HL),
]
for label,port,cmd,p,hello in cases:
    try: probe(port,cmd,p,hello,label)
    except Exception as e: print(label,'ERR',repr(e))
