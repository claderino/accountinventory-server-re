#!/usr/bin/env python3
"""Probe more commands on the original oracle (private ports)."""
import socket, struct, uuid, sys

def frame(cmd, payload):
    return struct.pack('<HH', 4 + len(payload), cmd) + payload

def recv(sock, timeout=1.5):
    sock.settimeout(timeout); out=[]
    while True:
        try:
            hdr = sock.recv(4)
            if len(hdr) < 4: break
            length, cmd = struct.unpack('<HH', hdr)
            body=b''
            while len(body) < length-4: body += sock.recv(length-4-len(body))
            out.append((cmd, body.hex()))
        except socket.timeout: break
    return out

def probe(port, cmd, payload, hello=None):
    s=socket.create_connection(('127.0.0.1',port),timeout=5)
    if hello: s.sendall(hello); recv(s,0.3)
    s.sendall(frame(cmd,payload)); r=recv(s); s.close(); return r

ACC = uuid.UUID('7144F687-BD7A-4CA7-B94A-C3F19644CDDF').bytes_le
TID = 0x1122334455
HELLO_L = frame(1, struct.pack('<H',7)+struct.pack('<HH',7801,20100))
HELLO_W = frame(1, struct.pack('<H',8)+struct.pack('<HH',10070,20100))

def main():
    cases = [
      ('L cmd4 get-goods-count', 23700, 4, struct.pack('<Q',TID)+ACC, HELLO_L),
      ('L cmd7 list-goods',      23700, 7, struct.pack('<Q',TID)+ACC+struct.pack('<BQ',1,0), HELLO_L),
      ('L cmd16 item-info',      23700, 16, struct.pack('<QQ',TID,2834453015476308139)+ACC+struct.pack('<Q',0), HELLO_L),
      ('L cmd19 try-consume',    23700, 19, struct.pack('<QQQ',2834453015476308139,946,0)+ACC, HELLO_L),
      ('W cmd4 get-goods-count', 23701, 4, struct.pack('<Qh',TID,1)+ACC, HELLO_W),
    ]
    for name, port, cmd, payload, hello in cases:
        try: print('%-26s -> %s' % (name, probe(port,cmd,payload,hello)))
        except Exception as e: print('%-26s -> ERR %s' % (name,e))

if __name__=='__main__': main()
