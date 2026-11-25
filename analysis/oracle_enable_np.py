import io
p = r'C:\serverside_re\accountinventory\analysis\oracle\accountInventoryd.config.xml'
s = io.open(p, encoding='utf-8').read()
s = s.replace("name='np-enabled' value='no'", "name='np-enabled' value='yes'")
s = s.replace("name='enable-instant-payment' value='no'", "name='enable-instant-payment' value='yes'")
io.open(p, 'w', encoding='utf-8').write(s)
print([l.strip() for l in s.splitlines() if 'np-enabled' in l or 'enable-instant-payment' in l or 'np-port' in l])
