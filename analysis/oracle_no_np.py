import io
p = r'C:\serverside_re\accountinventory\analysis\oracle\accountInventoryd.config.xml'
s = io.open(p, encoding='utf-8').read()
s = s.replace("name='np-enabled' value='yes'", "name='np-enabled' value='no'")
s = s.replace("name='enable-instant-payment' value='yes'", "name='enable-instant-payment' value='no'")
io.open(p, 'w', encoding='utf-8').write(s)
print([l.strip() for l in s.splitlines() if 'np-enabled' in l or 'enable-instant-payment' in l][:2])
