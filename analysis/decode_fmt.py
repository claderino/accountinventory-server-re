#!/usr/bin/env python3
import struct, sys

def parse(data, fmt, depth=0, path=''):
    off = 0
    idx = 0
    stack = []
    out = []
    counts = []
    while idx < len(fmt):
        t = fmt[idx]
        if t == ';':
            break
            if not stack: break
            block_start, count = stack.pop()
            # repeat the block (count-1) more times
            sub = fmt[block_start+1:idx]
            for _ in range(max(0, count-1)):
                for tt in sub:
                    if tt in ('h','i','q','Q','c','C','u','E','U','d','f','o','s','b','K'):
                        v, off, name = read(data, off, tt)
                        out.append((path+'repeat', tt, name))
            continue
        if t == 'C':
            cnt = struct.unpack_from('<H', data, off)[0]; off += 2
            stack.append((idx, cnt))
            out.append((path+'C', 'u16', cnt))
            idx += 1
            continue
        if t in ('h','i','q','Q','c','u','E','U','d','f','o','s','b','K'):
            v, off, name = read(data, off, t)
            out.append((path+t, t, name))
        idx += 1
    return out, off

def read(data, off, t):
    if t in ('c','Q'):
        v=data[off]; off+=1; name=v
    elif t in ('h','E','U','C','u'):
        v=struct.unpack_from('<H',data,off)[0]; off+=2; name=v
    elif t in ('i','d'):
        v=struct.unpack_from('<I',data,off)[0]; off+=4; name=v
    elif t=='f':
        v=struct.unpack_from('<f',data,off)[0]; off+=4; name=v
    elif t=='q':
        v=struct.unpack_from('<Q',data,off)[0]; off+=8; name=v
    elif t=='o':
        v=data[off:off+16].hex(); off+=16; name=v
    elif t=='s':
        n=struct.unpack_from('<H',data,off)[0]; off+=2
        v=data[off:off+n*2].decode('utf-16-le','replace'); off+=n*2; name=v
    elif t in ('b','K'):
        n=struct.unpack_from('<H',data,off)[0]; off+=2
        v=data[off:off+n].hex(); off+=n; name=v
    return v, off, name

if __name__ == '__main__':
    body = bytes.fromhex(sys.argv[1])
    fmt = sys.argv[2]
    fields, used = parse(body, fmt)
    for i,(p,t,v) in enumerate(fields):
        print('%3d %-10s %-4s %s' % (i,p,t,repr(v)))
    print('used',used,'of',len(body))
