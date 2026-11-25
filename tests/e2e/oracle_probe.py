#!/usr/bin/env python3
"""Probe the ORIGINAL daemon copy (oracle) on private ports 23700/23701/33700."""
import socket, struct, sys, uuid

def frame(cmd, payload):
    return struct.pack('<HH', 4 + len(payload), cmd) + payload

def recv(sock, timeout=2.0):
    sock.settimeout(timeout)
    out = []
    while True:
        try:
            hdr = sock.recv(4)
            if len(hdr) < 4: break
            length, cmd = struct.unpack('<HH', hdr)
            body = b''
            while len(body) < length - 4:
                body += sock.recv(length - 4 - len(body))
            out.append((cmd, body))
        except socket.timeout:
            break
    return out

def probe(port, cmd, payload, hello=None):
    s = socket.create_connection(('127.0.0.1', port), timeout=5)
    if hello:
        s.sendall(hello)
        r = recv(s, 0.5)
        print('  hello replies:', [(c, b.hex()) for c, b in r])
    s.sendall(frame(cmd, payload))
    r = recv(s, 2.0)
    s.close()
    return r

ACCOUNT = uuid.UUID('7144F687-BD7A-4CA7-B94A-C3F19644CDDF').bytes_le
PCDBID = 0x1122334455

def main():
    print('=== LOBBY (23700) ===')
    hello = frame(1, struct.pack('<H', 7) + struct.pack('<HH', 7801, 20100))
    # cmd13 service item list: h q o Q q  -> tid(u64), account(16), type(u8), key(u64)
    for stype in (1, 2):
        p = struct.pack('<Q', PCDBID) + ACCOUNT + struct.pack('<BQ', stype, 0)
        print(' cmd13 type', stype, '->', [(c, b.hex()) for c, b in probe(23700, 13, p, hello)])
    # cmd16 single item info: h q q o q  -> ? tune later
    print('=== WORLD (23701) ===')
    hello_w = frame(1, struct.pack('<H', 8) + struct.pack('<HH', 10070, 20100))
    print(' hello ->', [(c, b.hex()) for c, b in probe(23701, 1, struct.pack('<H', 8) + struct.pack('<HH', 10070, 20100))])

if __name__ == '__main__':
    sys.exit(main())
