#!/usr/bin/env python3
"""Logging TCP proxy between the oracle daemon and the real NP gateway.
Listens on 127.0.0.1:16602, forwards to 127.0.0.1:6601, logs all bytes both ways."""
import socket, threading, sys, time, os

OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), 'np_capture.log')
logf = open(OUT, 'w', encoding='ascii')
lock = threading.Lock()

def log(tag, data):
    with lock:
        logf.write('%s %s %d %s\n' % (time.strftime('%H:%M:%S'), tag, len(data), data.hex()))
        logf.flush()

def pump(a, b, tag):
    try:
        while True:
            d = a.recv(65536)
            if not d: break
            log(tag, d)
            b.sendall(d)
    except Exception:
        pass
    try: b.shutdown(socket.SHUT_WR)
    except Exception: pass

def main():
    s = socket.socket(); s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    s.bind(('127.0.0.1', 16602)); s.listen(5)
    print('proxy listening 16602 -> 6601', flush=True)
    while True:
        c, addr = s.accept()
        log('ACCEPT', addr[1].to_bytes(2,'little'))
        g = socket.create_connection(('127.0.0.1', 6601), timeout=10)
        threading.Thread(target=pump, args=(c, g, 'C2G'), daemon=True).start()
        threading.Thread(target=pump, args=(g, c, 'G2C'), daemon=True).start()

if __name__ == '__main__': main()
