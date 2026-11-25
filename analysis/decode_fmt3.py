#!/usr/bin/env python3
"""Correct recursive decoder for BloodLib packet format strings with nested C blocks."""
import struct, sys

WIDTH = {'c':1,'Q':1,'h':2,'E':2,'U':2,'C':2,'u':2,'i':4,'d':4,'f':4,'q':8,'o':16}

def read(data, off, t):
    if t in WIDTH:
        n = WIDTH[t]
        v = data[off:off+n]
        off += n
        if t in ('c','Q'): return v[0], off, v[0]
        if t in ('h','E','U','C','u'): return struct.unpack('<H',v)[0], off, struct.unpack('<H',v)[0]
        if t in ('i','d'): return struct.unpack('<I',v)[0], off, struct.unpack('<I',v)[0]
        if t == 'f': return struct.unpack('<f',v)[0], off, struct.unpack('<f',v)[0]
        if t == 'q': return struct.unpack('<Q',v)[0], off, struct.unpack('<Q',v)[0]
        if t == 'o': return v.hex(), off, v.hex()
    if t == 's':
        n = struct.unpack_from('<H',data,off)[0]; off += 2
        v = data[off:off+n*2].decode('utf-16-le','replace'); off += n*2
        return v, off, v
    if t in ('b','K'):
        n = struct.unpack_from('<H',data,off)[0]; off += 2
        v = data[off:off+n]; off += n
        return v, off, v.hex()
    raise ValueError('unknown type '+t)

def matching_semicolon(fmt, start):
    depth = 0
    i = start
    while i < len(fmt):
        if fmt[i] == 'C': depth += 1
        elif fmt[i] == ';':
            if depth == 0: return i
            depth -= 1
        i += 1
    return len(fmt)

def parse(data, off, fmt, prefix, out):
    i = 0
    while i < len(fmt):
        t = fmt[i]
        if t == ';':
            return off
        if t == 'C':
            cnt, off, _ = read(data, off, 'h')
            out.append((prefix+'C', 'count', cnt))
            end = matching_semicolon(fmt, i+1)
            sub = fmt[i+1:end]
            for k in range(cnt):
                off = parse(data, off, sub, '%s[%d].' % (prefix[:-1], k), out)
            i = end + 1
            continue
        v, off, disp = read(data, off, t)
        out.append((prefix+t, t, disp))
        i += 1
    return off

if __name__ == '__main__':
    body = bytes.fromhex(sys.argv[1])
    fmt = sys.argv[2]
    out = []
    used = parse(body, 0, fmt, '', out)
    for i,(path,t,v) in enumerate(out):
        print('%3d %-30s %-3s %s' % (i, path, t, repr(v)[:100]))
    print('used %d of %d bytes' % (used, len(body)))
