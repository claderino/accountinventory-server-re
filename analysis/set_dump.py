import io
p = r'C:\serverside_re\accountinventory\run\test\accountinventoryd.config.xml'
s = io.open(p, encoding='utf-8').read()
s = s.replace("name='dump-type' value='0'", "name='dump-type' value='2'")
io.open(p, 'w', encoding='utf-8').write(s)
print([l.strip() for l in s.splitlines() if 'dump-type' in l])
