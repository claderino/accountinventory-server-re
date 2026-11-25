import socket, struct, uuid

def frame(cmd, payload): return struct.pack('<HH',4+len(payload),cmd)+payload
def recv(s, t=1.0):
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
H=frame(1,struct.pack('<H',7)+struct.pack('<HH',7801,20100))
TID=0x1122334455
for acc in ['7144F687-BD7A-4CA7-B94A-C3F19644CDDF','00000000-0000-0000-0000-000000000000']:
    a=uuid.UUID(acc).bytes_le
    print(acc, probe(23700,4,struct.pack('<Q',TID)+a,H))
