#!/usr/bin/env python3
"""Send selected client commands to the NP-enabled oracle while the proxy logs NP traffic."""
import socket, struct, uuid, time

def frame(cmd, payload): return struct.pack('<HH',4+len(payload),cmd)+payload
def recv(s,t=1.5):
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
    s.sendall(hello); recv(s,0.3)
    s.sendall(frame(cmd,payload)); r=recv(s,2.5); s.close()
    print(label,'->',r[:3])
ACC=uuid.UUID('7144F687-BD7A-4CA7-B94A-C3F19644CDDF').bytes_le
TID=0x1122334455
HL=frame(1,struct.pack('<H',7)+struct.pack('<HH',7801,20100))
cases=[
 ('cmd4 get-goods-count',4,struct.pack('<Q',TID)+ACC),
 ('cmd7 list-goods',7,struct.pack('<Q',TID)+ACC+struct.pack('<BQ',1,0)),
 ('cmd10 take-goods',10,struct.pack('<Qi',TID,80407)+ACC+struct.pack('<i',5)),
]
for label,cmd,p in cases:
    try: probe(23700,cmd,p,HL,label)
    except Exception as e: print(label,'ERR',e)
    time.sleep(0.5)
