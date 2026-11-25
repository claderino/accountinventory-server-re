#!/usr/bin/env python3
"""Compare our daemon's wire responses to the original oracle copy, byte for byte."""
import socket, struct, uuid, sys

def frame(cmd, payload): return struct.pack('<HH',4+len(payload),cmd)+payload
def recv(s, t=1.5):
    s.settimeout(t); out=[]
    while True:
        try:
            h=s.recv(4)
            if len(h)<4: break
            L,c=struct.unpack('<HH',h); b=b''
            while len(b)<L-4: b+=s.recv(L-4-len(b))
            out.append((c,b))
        except socket.timeout: break
    return out
def probe(port, cmd, payload, hello):
    s=socket.create_connection(('127.0.0.1',port),timeout=5)
    s.sendall(hello); recv(s,0.3); s.sendall(frame(cmd,payload)); r=recv(s); s.close(); return r

ACC=uuid.UUID('7144F687-BD7A-4CA7-B94A-C3F19644CDDF').bytes_le
TID=0x1122334455
HL=frame(1,struct.pack('<H',7)+struct.pack('<HH',7801,20100))
CASES=[
 ('cmd4 get-goods-count', 4, struct.pack('<Q',TID)+ACC),
 ('cmd7 list-goods',      7, struct.pack('<Q',TID)+ACC+struct.pack('<BQ',1,0)),
 ('cmd13 service-list',  13, struct.pack('<Q',TID)+ACC+struct.pack('<BQ',1,0)),
 ('cmd16 item-info',     16, struct.pack('<QQ',TID,2834453015476308139)+ACC+struct.pack('<Q',0)),
 ('cmd19 try-consume',   19, struct.pack('<QQQ',2834453015476308139,946,0)+ACC),
 ('cmd25 cancel-consume',25, struct.pack('<QQQ',2834453015476308139,946,0)+ACC),
]
def main():
    fails=0
    for name,cmd,payload in CASES:
        try: o=probe(23700,cmd,payload,HL)
        except Exception as e: o=[('ERR',str(e).encode())]
        try: m=probe(22700,cmd,payload,HL)
        except Exception as e: m=[('ERR',str(e).encode())]
        oFmt=[(c,b.hex()) for c,b in o]; mFmt=[(c,b.hex()) for c,b in m]
        same = oFmt==mFmt
        print('%-22s %s  oracle=%s  ours=%s' % (name,'MATCH' if same else 'DIFF ',oFmt,mFmt))
        fails += 0 if same else 1
    print('COMPARE RESULT:', 'PASS' if fails==0 else 'FAIL (%d)'%fails)
    return 0 if fails==0 else 1
if __name__=='__main__': sys.exit(main())
