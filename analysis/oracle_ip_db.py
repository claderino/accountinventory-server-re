import io
base = r'C:\serverside_re\accountinventory\analysis\oracle'
cert = r'C:\serverside_re\accountinventory\analysis\oracle\cert_ip.cfg'
io.open(cert,'w',encoding='ascii').write(
    'DRIVER={ODBC Driver 17 for SQL Server};SERVER=localhost\\SQLEXPRESS;'
    'DATABASE=GameWarehouseDB_RE;Trusted_Connection=yes;')
p = base + r'\accountInventoryd.config.xml'
s = io.open(p, encoding='utf-8').read()
s = s.replace(r'C:\serverside_re\accountinventory\run\test\cert.cfg', cert)
io.open(p, 'w', encoding='utf-8').write(s)
print([l.strip() for l in s.splitlines() if 'cert-file' in l or 'np-port' in l])
