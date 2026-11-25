#!/usr/bin/env python3
"""Recursive decoder for BloodLib packet format strings."""
import struct, sys

def read_field(data, off, t):
    if t in ('c','Q'):
        return data[off], off+1, data[off]
    if t in ('h','E','U','C','u'):
        v=struct.unpack_from('<H',data,off)[0]; return v,off+2,v
    if t in ('i','d'):
        v=struct.unpack_from('<I',data,off)[0]; return v,off+4,v
    if t=='f':
        v=struct.unpack_from('<f',data,off)[0]; return v,off+4,v
    if t=='q':
        v=struct.unpack_from('<Q',data,off)[0]; return v,off+8,v
    if t=='o':
        return data[off:off+16].hex(),off+16,data[off:off+16].hex()
    if t=='s':
        n=struct.unpack_from('<H',data,off)[0]; off+=2
        v=data[off:off+n*2].decode('utf-16-le','replace'); return v,off+n*2,v
    if t in ('b','K'):
        n=struct.unpack_from('<H',data,off)[0]; off+=2
        v=data[off:off+n]; return v,off+n,v.hex()
    return None,off,None

def parse_seg(data, off, fmt, prefix, out, limit=None):
    i=0
    while i < len(fmt):
        t=fmt[i]
        if t==';':
            return off
        if t=='C':
            cnt,off,_=read_field(data,off,'h')
            out.append((prefix+'C','count',cnt))
            # find matching ;
            depth=1; j=i+1
            while j<len(fmt) and depth:
                if fmt[j]=='C': depth+=1
                elif fmt[j]==';': depth-=1
                j+=1
            sub=fmt[i+1:j-1]
            for k in range(cnt):
                off=parse_seg(data,off,sub,prefix+'[%d]'%k,out)
            i=j
            continue
        v,off,disp=read_field(data,off,t)
        out.append((prefix+t,t,disp))
        i+=1
    return off

if __name__=='__main__':
    body=bytes.fromhex(sys.argv[1]); fmt=sys.argv[2]
    out=[]; used=parse_seg(body,0,fmt,'',out)
    for i,(p,t,v) in enumerate(out):
        print('%3d %-24s %-4s %s'%(i,p,t,repr(v)[:120]))
    print('used',used,'of',len(body))
