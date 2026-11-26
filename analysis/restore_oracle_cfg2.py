import io
p = r'C:\serverside_re\accountinventory\analysis\oracle\accountInventoryd.config.xml'
s = io.open(p, encoding='utf-8').read()
s = s.replace("name='np-port' value='16602'", "name='np-port' value='6601'")
io.open(p, 'w', encoding='utf-8').write(s)
print([l.strip() for l in s.splitlines() if 'np-port' in l])
