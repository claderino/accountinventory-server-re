#!/usr/bin/env python3
"""Wire-level e2e checks for the reconstructed AccountInventoryDaemon.

Frame: [u16 length including itself][u16 cmd][fields...] little-endian.
The frame command word is consumed by the first 'h' of the packet format:
  manage hello "hh"   -> frame cmd=1, payload [u16 version=1]
  lobby  hello "hhhh" -> frame cmd=1, payload [u16 version=7][u16 wid][u16 port]
  world  hello "hhhh" -> frame cmd=1, payload [u16 version=8][u16 wid][u16 port]
Success ack: [len=4][cmd=2]; failure ack: [len=6][cmd=3][reason].
"""
import socket
import struct
import sys

HOST = '127.0.0.1'
PORTS = {'manage': 32700, 'lobby': 22700, 'world': 22701}


def frame(cmd, payload):
    return struct.pack('<HH', 4 + len(payload), cmd) + payload


def hello(port, version, extra=(), expect=2):
    payload = struct.pack('<H', version) + b''.join(struct.pack('<H', v) for v in extra)
    s = socket.create_connection((HOST, port), timeout=5)
    s.sendall(frame(1, payload))
    hdr = b''
    while len(hdr) < 4:
        hdr += s.recv(4 - len(hdr))
    length, cmd = struct.unpack('<HH', hdr)
    body = b''
    while len(body) < max(0, length - 4):
        body += s.recv(length - 4 - len(body))
    s.close()
    return cmd == expect, 'len=%d cmd=%d body=%s' % (length, cmd, body.hex())


def main():
    cases = [
        ('manage bad-version', 32700, 99, (), 3),
        ('lobby  bad-version', 22700, 99, (7801, 20100), 3),
        ('world  bad-version', 22701, 99, (7801, 20100), 3),
        ('manage hello',       32700, 1,  (), 2),
        ('lobby  hello',       22700, 7,  (7801, 20100), 2),
        ('world  hello',       22701, 8,  (7801, 20100), 2),
    ]
    failed = 0
    for name, port, ver, extra, expect in cases:
        try:
            ok, info = hello(port, ver, extra, expect)
        except Exception as e:
            ok, info = False, repr(e)
        print('%-22s %s  %s' % (name, 'PASS' if ok else 'FAIL', info))
        failed += 0 if ok else 1
    print('E2E RESULT:', 'PASS' if failed == 0 else 'FAIL (%d)' % failed)
    return 0 if failed == 0 else 1


if __name__ == '__main__':
    sys.exit(main())
