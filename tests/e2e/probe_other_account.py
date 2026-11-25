#!/usr/bin/env python3
import socket, struct, uuid, re
def frame(cmd,p): return struct.pack('<HH',4+len(p),cmd)+p
ACC=uuid.UUID('83D16F73-B105-4948-A49A-3808E61D72B7').bytes_le
TID=0x1122334455
s=socket.create_connection(('127.0.0.1',23700),timeout=5)
s.sendall(frame(1,struct.pack('<H',7)+struct.pack('<HH',7801,20100))); s.settimeout(0.5)
try: s.recv(4096)
except: pass
s.sendall(frame(7,struct.pack('<Q',TID)+ACC+struct.pack('<B',1)))
h=s.recv(4); L,cmd=struct.unpack('<HH',h); b=b''
while len(b)<L-4: b+=s.recv(L-4-len(b))
s.close()
print('cmd',cmd,'len',len(b))
print('first i',struct.unpack_from('<I',b,0)[0],'goods count',struct.unpack_from('<H',b,12)[0])
