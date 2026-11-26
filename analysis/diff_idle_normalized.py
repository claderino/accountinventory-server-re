import re, collections

def norm(p):
    out = []
    for line in open(p, encoding='utf-8', errors='replace'):
        s = line.rstrip('\n')
        s = re.sub(r'^\d{4}/\d{2}/\d{2}-\d{2}:\d{2}:\d{2}\.\d+,', '', s)
        s = s.replace('C:\\serverside_re\\accountinventory\\analysis\\oracle', '<DIR>')
        s = s.replace('C:\\serverside_re\\accountinventory\\run\\test', '<DIR>')
        for a, b in [('33700', '<PORT>'), ('23700', '<PORT>'), ('23701', '<PORT>'),
                     ('32700', '<PORT>'), ('22700', '<PORT>'), ('22701', '<PORT>')]:
            s = s.replace(a, b)
        s = re.sub(r'diff\(msec\):\d+', 'diff(msec):X', s)
        s = re.sub(r'0x[0-9A-Fa-f]{8,16}', '<HEX>', s)
        s = re.sub(r'frozen-seconds:\d+', 'frozen-seconds:X', s)
        s = re.sub(r'loading time: \d+', 'loading time: X', s)
        out.append(s)
    return out

o = collections.Counter(norm('analysis/idle_oracle.log'))
m = collections.Counter(norm('analysis/idle_ours.log'))
print('oracle lines', sum(o.values()), 'ours', sum(m.values()), 'distinct', len(o), len(m))
print('--- only in oracle (top 20) ---')
for k, v in (o - m).most_common(20): print('%5d %s' % (v, k[:180]))
print('--- only in ours (top 20) ---')
for k, v in (m - o).most_common(20): print('%5d %s' % (v, k[:180]))
print('--- common count', sum((o & m).values()))
