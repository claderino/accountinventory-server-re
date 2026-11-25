import re
p = r'C:\Users\Administrator\.local\share\opencode\shell\09d316790ad6d1065a53594da8cc677f99ffde52\sh_214a90273001U8O6wOWH2hVl99.out'
s = open(p, encoding='utf-8', errors='replace').read()
for line in s.splitlines():
    if 'cmd7' not in line: continue
    hexes = re.findall(r"'([0-9a-f]{20,})'", line)
    if len(hexes) >= 2:
        print('oracle len', len(hexes[0])//2, 'ours len', len(hexes[1])//2)
        print('BYTE-EXACT MATCH' if hexes[0] == hexes[1] else 'DIFFER')
        if hexes[0] != hexes[1]:
            for i,(a,b) in enumerate(zip(hexes[0], hexes[1])):
                if a != b:
                    print('first hex diff at', i)
                    break
    else:
        print('found', len(hexes), 'hex strings')
