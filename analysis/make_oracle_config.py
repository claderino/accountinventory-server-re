import os, shutil
root = r'C:\serverside_re\accountinventory'
run = os.path.join(root, 'analysis', 'oracle')
p = os.path.join(run, 'accountInventoryd.config.xml')
s = open(p, encoding='utf-8').read()
s = s.replace('D:\\Service\\AccountInventoryDaemon\\', run + '\\')
s = s.replace('D:\\service\\AccountInventoryDaemon\\', run + '\\')
s = s.replace('D:\\log\\AccountInventoryDaemon\\', os.path.join(run, 'log') + '\\')
s = s.replace('21700', '23700').replace('21701', '23701').replace('31700', '33700')
s = s.replace('D:\\service\\DBConfig\\Accinventoryd.cfg',
              os.path.join(root, 'run', 'test', 'cert.cfg'))
open(p, 'w', encoding='utf-8').write(s)
os.makedirs(os.path.join(run, 'config'), exist_ok=True)
shutil.copy(os.path.join(root, 'AccountInventoryDaemon', 'config', 'gamed-list.config.xml'),
            os.path.join(run, 'config', 'gamed-list.config.xml'))
for line in s.splitlines():
    if 'listen-port' in line or 'cert-file' in line or 'root-path' in line:
        print(line.strip())
