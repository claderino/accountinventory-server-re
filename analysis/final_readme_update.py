import io
p = r'C:\serverside_re\accountinventory\README.md'
s = io.open(p, encoding='utf-8').read()
s = s.replace('''original binary as an oracle** - 6/6 command probes match byte-for-byte
(`python tests\\e2e\\compare_oracle.py`, see `analysis/verification.md`).''', '''original binary as an oracle** - all probed lobby, world and manage paths match byte-for-byte
(`tests\\e2e\\compare_oracle.py`, `compare_remaining.py`, `compare_world.py`; clean 3-suite
result in `tests\\e2e\\all_fresh_results.txt`, see `analysis/verification.md`).''')
s = s.replace('''(`cmd5 [tid, GoodsCount]`). The full goods-list frame (cmd8) and the instant-payment job state
machine remain documented in `analysis/verification.md`.''', '''(`cmd5 [tid, GoodsCount]`); the full goods-list frame (cmd8) is implemented for both lobby and
world. The instant-payment job state machine beyond the restore request remains documented in
`analysis/verification.md`.''')
io.open(p, 'w', encoding='utf-8').write(s)
p = r'C:\serverside_re\accountinventory\SUMMARY.md'
s = io.open(p, encoding='utf-8').read()
s = s.replace('''all six probes (cmd4, 7, 13, 16, 19, 25) match byte-for-byte.''', '''all probed lobby, world and manage paths match byte-for-byte (clean 3-suite run in
`tests/e2e/all_fresh_results.txt`).''')
io.open(p, 'w', encoding='utf-8').write(s)
print('README/SUMMARY updated')
