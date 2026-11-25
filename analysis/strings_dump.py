import sys, re
p = sys.argv[1]
data = open(p,'rb').read()
print("size", len(data))
# ASCII strings >=5
asc = re.findall(rb'[\x20-\x7e]{5,}', data)
print("ASCII count", len(asc))
with open(sys.argv[2],'w',encoding='utf-8',errors='replace') as f:
    for i,s in enumerate(asc):
        f.write(f"{i}\t{s.decode('ascii','replace')}\n")
# UTF-16LE strings
u16 = re.findall(rb'(?:[\x20-\x7e]\x00){4,}', data)
with open(sys.argv[2]+'.u16','w',encoding='utf-8',errors='replace') as f:
    for i,s in enumerate(u16):
        f.write(f"{i}\t{s.decode('utf-16-le','replace')}\n")
print("UTF16 count", len(u16))
