import io
p = r'C:\serverside_re\accountinventory\AccountInventoryDaemon\src\AccountInventory\Handlers.cpp'
s = io.open(p, encoding='utf-8').read()
s = s.replace('%%I64d, handle start, cmd:%d", s ? s->id : 0, cmd)',
              '%I64d, handle start, cmd:%d", s ? s->id : 0, (long long)0, cmd)')
s = s.replace('%%I64d, handle end, cmd:%d", s ? s->id : 0, cmd)',
              '%I64d, handle end, cmd:%d", s ? s->id : 0, (long long)0, cmd)')
s = s.replace('%%I64d, handle fail, no such cmd, cmd:%d",\n                        s ? s->id : 0, cmd)',
              '%I64d, handle fail, no such cmd, cmd:%d",\n                        s ? s->id : 0, (long long)0, cmd)')
io.open(p, 'w', encoding='utf-8').write(s)
for line in s.splitlines():
    if 'I64d, handle' in line:
        print(line.strip()[:140])
