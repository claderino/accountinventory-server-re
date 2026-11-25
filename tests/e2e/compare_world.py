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
A=uuid.UUID('7144F687-BD7A-4CA7-B94A-C3F19644CDDF').bytes_le
T=0x1122334455
HW=frame(1,struct.pack('<H',8)+struct.pack('<HH',7801,20100))
cases=[
 ('W cmd4',4,struct.pack('<Q',T)+A),
 ('W cmd7',7,struct.pack('<Q',T)+A+struct.pack('<B',1)),
 ('W cmd13',13,struct.pack('<Qh',T,7801)+A+struct.pack('<BQ',1,0)),
 ('W cmd16',16,struct.pack('<QQ',T,2834453015476308139)),
 ('W cmd19',19,struct.pack('<QQQ',2834453015476308139,946,0)+A),
 ('W cmd22',22,struct.pack('<QQQH',2834453015476308139,946,0,1)+A),
 ('W cmd34',34,struct.pack('<QQ',T,0)),
]
def main():
    fails=0
    for name,cmd,p in cases:
        try: o=probe(23701,cmd,p,HW)
        except Exception as e: o=[('ERR',str(e).encode())]
        try: m=probe(22701,cmd,p,HW)
        except Exception as e: m=[('ERR',str(e).encode())]
        oF=[(c,b.hex() if isinstance(b,bytes) else str(b)) for c,b in o]
        mF=[(c,b.hex() if isinstance(b,bytes) else str(b)) for c,b in m]
        same=oF==mF
        print('%-10s %s oracle=%s ours=%s'%(name,'MATCH' if same else 'DIFF',[(c,b[:60]) for c,b in oF],[(c,b[:60]) for c,b in mF]))
        fails+=0 if same else 1
    print('RESULT:','PASS' if fails==0 else 'FAIL(%d)'%fails)
    return 0 if fails==0 else 1
if __name__=='__main__':
    import sys; sys.exit(main())
