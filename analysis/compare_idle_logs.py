import re, collections

def load(p):
    rows = []
    for line in open(p, encoding='utf-8', errors='replace'):
        line = line.rstrip('\n')
        m = re.match(r'^\d{4}/\d{2}/\d{2}-\d{2}:\d{2}:\d{2}\.\d+,([^,]+),(\d+),\((\w+)\),(\[[^\]]+\]),?(.*)$', line)
        if m:
            rows.append((m.group(1), int(m.group(2)), m.group(3), m.group(4), m.group(5).strip()))
        else:
            rows.append(('?', 0, '?', '?', line[:80]))
    return rows

for name, p in [('ORACLE', 'analysis/idle_oracle.log'), ('OURS', 'analysis/idle_ours.log')]:
    rows = load(p)
    print('===', name, 'lines', len(rows))
    c = collections.Counter((r[0], r[1], r[2], r[3]) for r in rows)
    for k, v in c.most_common(25):
        print('%5d %s:%d %s %s' % (v, k[0], k[1], k[2], k[3]))
    print('--- last 8 messages ---')
    for r in rows[-8:]:
        print('%s:%d %s %s | %s' % (r[0], r[1], r[2], r[3], r[4][:120]))
