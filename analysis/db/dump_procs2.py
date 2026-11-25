import subprocess, os, tempfile
SRV = 'localhost\\SQLEXPRESS'; DB = 'GameWarehouseDB'
OUT = os.path.dirname(os.path.abspath(__file__))
sqlcmd = r'C:\Program Files\Microsoft SQL Server\Client SDK\ODBC\170\Tools\Binn\SQLCMD.EXE'

def run_def(name):
    tmp = os.path.join(OUT, '_def.tmp')
    q = "SELECT definition FROM sys.sql_modules WHERE object_id=OBJECT_ID('dbo.%s')" % name
    subprocess.run([sqlcmd, '-S', SRV, '-E', '-d', DB, '-Q', q, '-y', '0', '-o', tmp],
                   capture_output=True, timeout=60)
    data = open(tmp, 'rb').read().decode('cp1252', errors='replace')
    os.remove(tmp)
    return data

names = [x.strip() for x in open(os.path.join(OUT, 'proc_names.txt'), encoding='utf-8')
         if x.strip().startswith(('umsp_', 'usp_'))]
procdir = os.path.join(OUT, 'procs'); os.makedirs(procdir, exist_ok=True)
for name in names:
    text = run_def(name)
    open(os.path.join(procdir, 'dbo.' + name + '.sql'), 'w',
         encoding='utf-8', errors='replace').write(text)
print('dumped', len(names), 'avg bytes', sum(os.path.getsize(os.path.join(procdir, 'dbo.'+n+'.sql')) for n in names)//len(names))
