#!/usr/bin/env python3
"""Focused byte-exact comparison of the NP-backed goods-list (cmd8) reply."""
import socket, struct, uuid, sys, time

def frame(cmd,p): return struct.pack('<HH',4+len(p),cmd)+p
ACC=uuid.UUID('7144F687-BD7A-4CA7-B94A-C3F19644CDDF').bytes_le
TID=0x1122334455
HELLO=frame(1,struct.pack('<H',7)+struct.pack('<HH',7801,20100))
PAYLOAD=struct.pack('<Q',TID)+ACC+struct.pack('<B',1)

def probe(port):
    s=socket.create_connection(('127.0.0.1',port),timeout=5)
    s.sendall(HELLO); s.settimeout(0.3)
    try: s.recv(4096)
    except Exception: pass
    s.sendall(frame(7,PAYLOAD))
    h=s.recv(4); L,cmd=struct.unpack('<HH',h); b=b''
    while len(b)<L-4: b+=s.recv(L-4-len(b))
    s.close(); return cmd,b

def main():
    o=probe(23700); m=probe(22700)
    print('oracle cmd',o[0],'len',len(o[1]))
    print('ours   cmd',m[0],'len',len(m[1]))
    print('MATCH' if o==m else 'DIFF')
    if o!=m:
        ob,mb=o[1],m[1]
        for i in range(min(len(ob),len(mb))):
            if ob[i]!=mb[i]:
                print('first diff at',i,'oracle',ob[max(0,i-8):i+16].hex(),'ours',mb[max(0,i-8):i+16].hex()); break
        print('oracle',ob.hex())
        print('ours  ',mb.hex())
    return 0 if o==m else 1
if __name__=='__main__': sys.exit(main())
