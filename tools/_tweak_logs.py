from pathlib import Path

p = Path('ps3recomp-src/libs/spurs/cellSpurs.c')
s = p.read_text(encoding='utf-8')
s = s.replace('if (PS3_LOG_FIRST(_n, 6))', 'if (PS3_LOG_FIRST(_n, 200))')
s = s.replace('if (woke && PS3_LOG_FIRST(_n, 8))', 'if (PS3_LOG_FIRST(_n, 200))')
p.write_text(s, encoding='utf-8')
print('ok')
