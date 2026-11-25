import subprocess, os
SRV = 'localhost\\SQLEXPRESS'; DB = 'GameWarehouseDB'
OUT = os.path.dirname(os.path.abspath(__file__))
sqlcmd = r'C:\Program Files\Microsoft SQL Server\Client SDK\ODBC\170\Tools\Binn\SQLCMD.EXE'

def run(q):
    r = subprocess.run([sqlcmd, '-S', SRV, '-E', '-d', DB, '-Q', q, '-W', '-h', '-1', '-s', '|'],
                       capture_output=True, encoding='cp1252', errors='replace', timeout=60)
    return r.stdout or ''

# proc | param | type | maxlen | is_output
q = """
SELECT p.name, pr.name,
  CASE WHEN pr.system_type_id=231 THEN 'nvarchar' WHEN pr.system_type_id=167 THEN 'varchar'
       WHEN pr.system_type_id=104 THEN 'bit' WHEN pr.system_type_id=56 THEN 'int'
       WHEN pr.system_type_id=127 THEN 'bigint' WHEN pr.system_type_id=61 THEN 'datetime'
       WHEN pr.system_type_id=62 THEN 'float' WHEN pr.system_type_id=48 THEN 'tinyint'
       WHEN pr.system_type_id=52 THEN 'smallint' WHEN pr.system_type_id=36 THEN 'uniqueidentifier'
       WHEN pr.system_type_id=165 THEN 'varbinary' ELSE CAST(pr.system_type_id AS varchar(10)) END,
  pr.max_length, pr.is_output
FROM sys.procedures p JOIN sys.parameters pr ON pr.object_id=p.object_id
ORDER BY p.name, pr.parameter_id
"""
lines = [l.strip() for l in run(q).splitlines() if '|' in l]
out = {}
for line in lines:
    parts = [x.strip() for x in line.split('|')]
    if len(parts) < 5: continue
    name, param, typ, maxlen, isout = parts[:5]
    out.setdefault(name, []).append((param, typ, maxlen, isout))
with open(os.path.join(OUT, 'proc_signatures.txt'), 'w', encoding='utf-8') as f:
    for name in sorted(out):
        f.write('dbo.%s(\n' % name)
        for param, typ, maxlen, isout in out[name]:
            f.write('    %s %s%s%s\n' % (param, typ,
                    '(%s)' % maxlen if typ in ('nvarchar', 'varchar', 'varbinary') else '',
                    ' OUTPUT' if isout == '1' else ''))
        f.write(')\n')
print(open(os.path.join(OUT, 'proc_signatures.txt'), encoding='utf-8').read()[:12000])
