import sys, re
p = r'C:\serverside_re\accountinventory\analysis\decomp\unknown.c'
lines = open(p, encoding='utf-8', errors='replace').read().splitlines()
target = sys.argv[1]
for i, l in enumerate(lines):
    if '===== ' + target in l:
        for j in range(i, i + 500):
            if j > i and '===== sub_' in lines[j]:
                break
            if any(k in lines[j] for k in ('FUN_7ff63b2841b0', 'FUN_7ff63b2966a0',
                                           'FUN_7ff63b2d6620', 'FUN_7ff63b2b7770')):
                print(j+1, lines[j].strip()[:190])
        break
