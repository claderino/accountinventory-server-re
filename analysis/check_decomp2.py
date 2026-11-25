p = r'C:\serverside_re\accountinventory\AccountInventoryDaemon\src\BloodLib\Packet.cpp'
lines = open(p, encoding='utf-8').read().splitlines()
for i, l in enumerate(lines):
    if 'Packet_Decompose' in l:
        for j in range(max(0, i-2), min(i+30, len(lines))):
            print(j+1, lines[j])
        print('---')
