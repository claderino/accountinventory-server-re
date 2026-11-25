import struct
b=bytes.fromhex('19010000554433221100000001003b06000000000000490000000001fffffffffffffffff70a03590000000000000000000000001800440e2d0e400e170e470e210e2a0e070e010e230e320e190e150e4c0e410e1e0e470e040e20004300420054002000310000000000050000000000010500860000000a0000006039010000000000080000000000000000000600000000000100880000000a000000ba9f0c00000000000800000000000000000006000000000001008900000005000000b79f0c00000000000800000000000000000006000000000001008a000000010000005b940c00000000000800000000000000000006000000000001008b0000000f0000007aa90c0000000000080000000000000000000600000000000100')
off=0
def u16():
 global off; v=struct.unpack_from('<H',b,off)[0]; off+=2; return v
def u32():
 global off; v=struct.unpack_from('<I',b,off)[0]; off+=4; return v
def u64():
 global off; v=struct.unpack_from('<Q',b,off)[0]; off+=8; return v
def s():
 global off; n=u16(); v=b[off:off+n*2].decode('utf-16-le','replace'); off+=n*2; return v
def by():
 global off; n=u16(); v=b[off:off+n].hex(); off+=n; return v
print('i',u32(),'tid',hex(u64()),'C',u16())
print('q',u64(),'i',u32(),'Q',b[off],b[off+1]); off+=2
print('q',hex(u64()),'q',hex(u64()))
print('s',repr(s()),'s',repr(s()),'i',u32(),'s',repr(s()),'s',repr(s()),'b',by(),'b',by())
n=u16(); print('C2',n)
for k in range(n):
 print(' item',k,u32(),u32(),u32(),u32(),by(),by())
print('off',off,'len',len(b))
