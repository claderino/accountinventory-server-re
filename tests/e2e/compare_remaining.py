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
def probe(port,cmd,payload,hello):
    s=socket.create_connection(('127.0.0.1',port),timeout=5)
    s.sendall(hello); recv(s,0.3); s.sendall(frame(cmd,payload)); r=recv(s); s.close(); return r
ACC=uuid.UUID('7144F687-BD7A-4CA7-B94A-C3F19644CDDF').bytes_le
TID=0x1122334455
HL=frame(1,struct.pack('<H',7)+struct.pack('<HH',7801,20100))
HM=frame(1,struct.pack('<H',1))
cases=[
 ('L cmd10 take-goods',22700,23700,10,struct.pack('<QQ',TID,80407)+ACC,HL),
 ('M cmd4 restore',32700,33700,4,struct.pack('<QQ',TID,0x123)+ACC+struct.pack('<Qi',0,2),HM),
 ('L cmd22 finish-consume',22700,23700,22,struct.pack('<QQQH',2834453015476308139,946,0,1)+ACC,HL),
 ('L cmd28 consume',22700,23700,28,struct.pack('<QQQ',2834453015476308139,946,0)+ACC,HL),
]
def main():
    fails=0
    for name,mp,op,cmd,p,hello in cases:
        try: o=probe(op,cmd,p,hello)
        except Exception as e: o=[('ERR',str(e).encode())]
        try: m=probe(mp,cmd,p,hello)
        except Exception as e: m=[('ERR',str(e).encode())]
        oF=[(c,b.hex() if isinstance(b,bytes) else str(b)) for c,b in o]
        mF=[(c,b.hex() if isinstance(b,bytes) else str(b)) for c,b in m]
        same=oF==mF
        print('%-24s %s oracle=%s ours=%s'%(name,'MATCH' if same else 'DIFF',oF,mF))
        fails+=0 if same else 1
    print('RESULT:','PASS' if fails==0 else 'FAIL(%d)'%fails)
    return 0 if fails==0 else 1
if __name__=='__main__':
    import sys; sys.exit(main())
