import re, collections
for name, p in [('ORACLE', 'analysis/idle_oracle.log'), ('OURS', 'analysis/idle_ours.log')]:
    c = collections.Counter()
    for l in open(p, encoding='utf-8', errors='replace'):
        m = re.match(r'^\d{4}/\d{2}/\d{2}-\d{2}:\d{2}:\d{2}\.\d+,([^,]+),(\d+),\((\w+)\),', l)
        if m: c[(m.group(1), int(m.group(2)), m.group(3))] += 1
        else: c[('?', 0, '?')] += 1
    print('===', name, sum(c.values()))
    for k, v in c.most_common(30): print('%5d %s:%d %s' % (v, k[0], k[1], k[2]))
