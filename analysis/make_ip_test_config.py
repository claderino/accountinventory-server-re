import io, os
root = r'C:\serverside_re\accountinventory'
src = root + r'\run\test\accountinventoryd.config.xml'
dst_dir = root + r'\run\test_ip'
os.makedirs(dst_dir + r'\config', exist_ok=True)
os.makedirs(dst_dir + r'\log', exist_ok=True)
s = io.open(src, encoding='utf-8').read()
cert = root + r'\analysis\oracle\cert_ip.cfg'
s = s.replace(r'C:\serverside_re\accountinventory\run\test\cert.cfg', cert)
s = s.replace('C:\\serverside_re\\accountinventory\\run\\test\\', 'C:\\serverside_re\\accountinventory\\run\\test_ip\\')
io.open(dst_dir + r'\accountinventoryd.config.xml', 'w', encoding='utf-8').write(s)
print([l.strip() for l in s.splitlines() if 'cert-file' in l or 'root-path' in l])
