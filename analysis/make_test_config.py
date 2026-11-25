import os, shutil
root = r'C:\serverside_re\accountinventory'
run = os.path.join(root, 'run', 'test')
for d in ['config', 'log', 'transaction']:
    os.makedirs(os.path.join(run, d), exist_ok=True)
src = open(os.path.join(root, 'AccountInventoryDaemon', 'config', 'accountinventoryd.config.xml'), encoding='utf-8').read()
src = src.replace('D:\\Service\\AccountInventoryDaemon\\', 'C:\\serverside_re\\accountinventory\\run\\test\\')
src = src.replace('D:\\service\\AccountInventoryDaemon\\', 'C:\\serverside_re\\accountinventory\\run\\test\\')
src = src.replace('D:\\log\\AccountInventoryDaemon\\', 'C:\\serverside_re\\accountinventory\\run\\test\\log\\')
src = src.replace('21700', '22700').replace('21701', '22701').replace('31700', '32700')
src = src.replace('D:\\service\\DBConfig\\Accinventoryd.cfg', 'C:\\serverside_re\\accountinventory\\run\\test\\cert.cfg')
open(os.path.join(run, 'accountinventoryd.config.xml'), 'w', encoding='utf-8').write(src)
shutil.copy(os.path.join(root, 'AccountInventoryDaemon', 'config', 'gamed-list.config.xml'),
            os.path.join(run, 'config', 'gamed-list.config.xml'))
cert = 'DRIVER={ODBC Driver 17 for SQL Server};SERVER=localhost\\SQLEXPRESS;DATABASE=GameWarehouseDB;Trusted_Connection=yes;'
open(os.path.join(run, 'cert.cfg'), 'w', encoding='ascii').write(cert)
print('cert:', cert)
for line in src.splitlines():
    if any(k in line for k in ('path', 'port', 'cert')):
        print(line.strip())
