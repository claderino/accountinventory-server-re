import io
p = r'C:\serverside_re\accountinventory\analysis\verification.md'
s = io.open(p, encoding='utf-8').read()
s = s.replace('''manage cmd4 restore    MATCH  cmd6 [tid,4]
COMPARE RESULT: PASS (11/11 probed paths)''', '''manage cmd4 restore    MATCH  cmd6 [tid,4]
COMPARE RESULT: PASS (11/11 probed paths)

World channel (compare_world.py, NP-enabled oracle) — all MATCH:
  cmd4 cmd5 / cmd7 cmd8 (281 variant + extra per-item field) / cmd13 cmd15 /
  cmd16 cmd17 / cmd19 cmd21 / cmd22 cmd24 / cmd34 cmd36.
  cmd10/25/28/31: oracle sends no reply; our handlers mirror that.''')
s = s.replace('''- Lobby cmd32 (retry-take) and cmd33 (logout) produce no reply on the oracle with the recovered
  request formats; our handlers mirror that behaviour.''', '''- Lobby cmd32 (retry-take) and cmd33 (logout) and world cmd10/25/28/31 produce no reply on the
  oracle with the recovered request formats; our handlers mirror that behaviour.
- The oracle's NP socket goes idle between long runs and it falls back to its NP-unavailable
  replies; each differential suite passes when run against a freshly started oracle.''')
io.open(p, 'w', encoding='utf-8').write(s)
print('docs updated')
