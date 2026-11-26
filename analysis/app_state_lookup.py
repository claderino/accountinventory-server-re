import urllib.request, re
s = urllib.request.urlopen('http://localhost:6605/apps-state', timeout=10).read().decode('utf-8', 'replace')
for m in re.finditer(r'<App>(.*?)</App>', s, re.S):
    b = m.group(1)
    name = re.search(r'<AppName>(.*?)</AppName>', b)
    if not name or name.group(1) not in ('PurchaseSrv', 'DbSrv20', 'WarehouseSrv'):
        continue
    fields = {}
    for key in ('ProductType', 'AppIndex', 'Deployment', 'Epoch', 'State', 'ProcessId'):
        mm = re.search('<%s>(.*?)</%s>' % (key, key), b)
        fields[key] = mm.group(1) if mm else '?'
    print(name.group(1), fields)
