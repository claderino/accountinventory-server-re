import struct
b=bytes.fromhex(open(r'C:\serverside_re\accountinventory\analysis\cmd8_body.hex').read())
off=115
n=5  # C2 count already read at offset 113
def u16():
 global off; v=struct.unpack_from('<H',b,off)[0]; off+=2; return v
def u32():
 global off; v=struct.unpack_from('<I',b,off)[0]; off+=4; return v
def byt():
 global off; n=u16(); v=b[off:off+n].hex(); off+=n; return v
print('C2 count',n,'off',off)
for k in range(n):
 print('item',k,'i',u32(),'i',u32(),'i',u32(),'b',byt(),'b',byt(),'off',off)
print('remaining',len(b)-off, b[off:].hex()[:100])
