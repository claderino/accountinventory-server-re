#!/usr/bin/env python3
"""Compare all three ports with NP enabled."""
import socket, struct, uuid, sys

def frame(cmd, payload): return struct.pack('<HH',4+len(payload),cmd)+payload
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
def probe(port,cmd,payload,hello):
    s=socket.create_connection(('127.0.0.1',port),timeout=5)
    s.sendall(hello); recv(s,0.3); s.sendall(frame(cmd,payload)); r=recv(s); s.close(); return r
ACC=uuid.UUID('7144F687-BD7A-4CA7-B94A-C3F19644CDDF').bytes_le
TID=0x1122334455
HL=frame(1,struct.pack('<H',7)+struct.pack('<HH',7801,20100))
HW=frame(1,struct.pack('<H',8)+struct.pack('<HH',10070,20100))
HM=frame(1,struct.pack('<H',1))
cases=[
 ('L cmd4',22700,23700,4,struct.pack('<Q',TID)+ACC,HL),
 ('L cmd7',22700,23700,7,struct.pack('<Q',TID)+ACC+struct.pack('<B',1),HL),
 ('W cmd4',22701,23701,4,struct.pack('<Q',TID)+ACC,HW),
 ('W cmd7',22701,23701,7,struct.pack('<Q',TID)+ACC+struct.pack('<B',1),HW),
 ('M cmd4',32700,33700,4,struct.pack('<QQ',TID,0x123)+ACC+struct.pack('<Qi',0,2),HM),
]
def main():
    fails=0
    for name,mp,op,cmd,p,hello in cases:
        try: o=probe(op,cmd,p,hello)
        except Exception as e: o=[('ERR',str(e).encode())]
        try: m=probe(mp,cmd,p,hello)
        except Exception as e: m=[('ERR',str(e).encode())]
        oFmt=[(c, b.hex() if isinstance(b,bytes) else str(b)) for c,b in o]
        mFmt=[(c, b.hex() if isinstance(b,bytes) else str(b)) for c,b in m]
        same = oFmt==mFmt
        print('%-10s %s oracle=%s ours=%s'%(name,'MATCH' if same else 'DIFF',[(c,b[:50]) for c,b in oFmt],[(c,b[:50]) for c,b in mFmt]))
        fails+=0 if same else 1
    print('RESULT:','PASS' if fails==0 else 'FAIL(%d)'%fails)
if __name__=='__main__': main()
