import subprocess, os, re
SRV = 'localhost\\SQLEXPRESS'
DB = 'GameWarehouseDB'
OUT = os.path.dirname(os.path.abspath(__file__))
sqlcmd = r'C:\Program Files\Microsoft SQL Server\Client SDK\ODBC\170\Tools\Binn\SQLCMD.EXE'

def run(q):
    r = subprocess.run([sqlcmd, '-S', SRV, '-E', '-d', DB, '-Q', q, '-W', '-h', '-1'],
                       capture_output=True, encoding='cp1252', errors='replace', timeout=60)
    return r.stdout or ''

raw = run("SELECT name FROM sys.procedures ORDER BY name")
names = [x.strip() for x in raw.splitlines()
         if x.strip() and x.strip().startswith(('umsp_', 'usp_'))]
print('procs:', len(names))
procdir = os.path.join(OUT, 'procs')
os.makedirs(procdir, exist_ok=True)
manifest = []
for name in names:
    text = run("SELECT OBJECT_DEFINITION(OBJECT_ID('dbo.%s'))" % name)
    open(os.path.join(procdir, 'dbo.' + name + '.sql'), 'w',
         encoding='utf-8', errors='replace').write(text)
    params = []
    m = re.search(r'\(([^)]*)\)\s*(?:WITH\s+\w+\s+)?AS\s+BEGIN', text, re.I | re.S)
    if m:
        for p in m.group(1).split(','):
            params.append(re.sub(r'\s+', ' ', p.replace('\r', ' ').replace('\n', ' ')).strip())
    manifest.append((name, params))
with open(os.path.join(OUT, 'proc_signatures.txt'), 'w', encoding='utf-8') as f:
    for name, params in manifest:
        f.write('dbo.%s(%s)\n' % (name, '; '.join(params)))
print(open(os.path.join(OUT, 'proc_signatures.txt'), encoding='utf-8').read())
