p = r'C:\serverside_re\accountinventory\AccountInventoryDaemon\src\BloodLib\PacketFormat.cpp'
lines = open(p, encoding='utf-8').read().splitlines()
for i, l in enumerate(lines):
    if "case 'o':" in l and i > 100:
        for j in range(max(0, i-4), min(i+8, len(lines))):
            print(j+1, lines[j])
        print('---')
