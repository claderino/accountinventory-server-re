import io
base = r'C:\serverside_re\accountinventory\analysis\oracle'
p = base + r'\accountInventoryd.config.xml'
s = io.open(p, encoding='utf-8').read()
s = s.replace(r'C:\serverside_re\accountinventory\analysis\oracle\cert_ip.cfg',
              r'C:\serverside_re\accountinventory\run\test\cert.cfg')
io.open(p, 'w', encoding='utf-8').write(s)
print([l.strip() for l in s.splitlines() if 'cert-file' in l or 'np-port' in l])
