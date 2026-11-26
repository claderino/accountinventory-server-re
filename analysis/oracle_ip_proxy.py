import io
base = r'C:\serverside_re\accountinventory\analysis\oracle'
p = base + r'\accountInventoryd.config.xml'
s = io.open(p, encoding='utf-8').read()
s = s.replace(r'C:\serverside_re\accountinventory\run\test\cert.cfg',
              r'C:\serverside_re\accountinventory\analysis\oracle\cert_ip.cfg')
s = s.replace("name='np-port' value='6601'", "name='np-port' value='16602'")
io.open(p, 'w', encoding='utf-8').write(s)
print([l.strip() for l in s.splitlines() if 'np-port' in l or 'cert-file' in l])
