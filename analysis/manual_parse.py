import struct
b=bytes.fromhex(open(r'C:\serverside_re\accountinventory\analysis\cmd8_body.hex').read())
off=0
def u8():
 global off; v=b[off]; off+=1; return v
def u16():
 global off; v=struct.unpack_from('<H',b,off)[0]; off+=2; return v
def u32():
 global off; v=struct.unpack_from('<I',b,off)[0]; off+=4; return v
def u64():
 global off; v=struct.unpack_from('<Q',b,off)[0]; off+=8; return v
def s():
 global off; n=u16(); v=b[off:off+n*2].decode('utf-16-le','replace'); off+=n*2; return v
def byt():
 global off; n=u16(); v=b[off:off+n].hex(); off+=n; return v
print('i',u32(),'off',off)
print('q',hex(u64()),'off',off)
print('C',u16(),'off',off)
print('--- element 1 (format qiQQqqssissbbCiiiibb) ---')
print('q',u64(),'off',off)
print('i',u32(),'off',off)
print('Q',u8(),'Q',u8(),'off',off)
print('q',hex(u64()),'off',off)
print('q',hex(u64()),'off',off)
print('s',repr(s()),'off',off)
print('s',repr(s()),'off',off)
print('i',u32(),'off',off)
print('s',repr(s()),'off',off)
print('s',repr(s()),'off',off)
print('b',byt(),'off',off)
print('b',byt(),'off',off)
print('C2',u16(),'off',off)
