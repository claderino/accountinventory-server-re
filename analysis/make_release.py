import os, shutil
root = r'C:\serverside_re\accountinventory'
rel = os.path.join(root, 'release')
bld = os.path.join(root, 'AccountInventoryDaemon', 'build')
orig = r'D:\service\AccountInventoryDaemon\bin'
for d in ['bin', 'config', 'log', 'data']:
    os.makedirs(os.path.join(rel, d), exist_ok=True)
# our rebuilt exe
shutil.copy(os.path.join(bld, 'AccountInventoryDaemon.exe'), os.path.join(rel, 'bin', 'AccountInventoryDaemon.exe'))
# byte-identical companion DLLs from the original install
for dll in ['XmlReader_sv64.dll', 'bsConsole_sv64.dll', 'dbghelp.dll', 'GeoData_sv64.dll',
            'StsMsgSrv64.dll']:
    shutil.copy(os.path.join(orig, dll), os.path.join(rel, 'bin', dll))
# byte-identical original config (absolute D:\ paths make the daemon self-locating)
shutil.copy(os.path.join(orig, 'accountInventoryd.config.xml'),
            os.path.join(rel, 'bin', 'accountInventoryd.config.xml'))
shutil.copy(os.path.join(root, 'AccountInventoryDaemon', 'config', 'AccountInventoryDaemon.man'),
            os.path.join(rel, 'bin', 'AccountInventoryDaemon.man'))
shutil.copy(os.path.join(root, 'AccountInventoryDaemon', 'config', 'gamed-list.config.xml'),
            os.path.join(rel, 'config', 'gamed-list.config.xml'))
shutil.copy(os.path.join(root, 'run', 'test', 'console_launch.exe'), os.path.join(rel, 'bin', 'console_launch.exe'))
with open(os.path.join(rel, 'bin', 'cert.example.cfg'), 'w') as f:
    f.write('DRIVER={ODBC Driver 17 for SQL Server};SERVER=localhost\\SQLEXPRESS;'
            'DATABASE=GameWarehouseDB;Trusted_Connection=yes;')
with open(os.path.join(rel, 'bin', 'start.bat'), 'w') as f:
    f.write('@echo off\r\n'
            'rem drop-in replacement for D:\\service\\AccountInventoryDaemon\\bin\\\r\n'
            'start "" "%~dp0AccountInventoryDaemon.exe" --text --conf="%~dp0"\r\n')
print('release built')
for dirpath, dirnames, filenames in os.walk(rel):
    for name in sorted(filenames):
        p = os.path.join(dirpath, name)
        print(os.path.relpath(p, rel), os.path.getsize(p))
