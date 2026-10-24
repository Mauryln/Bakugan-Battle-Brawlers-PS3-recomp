import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
p = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, 'game', 'PS3_GAME', 'PARAM.SFO')
d = open(p, 'rb').read()
assert d[:4] == b'\x00PSF', d[:4]
version, key_start, data_start, count = struct.unpack('<4I', d[4:20])
print('version 0x%x key_table 0x%x data_table 0x%x entries %d' % (version, key_start, data_start, count))


def cstr(off):
    end = d.index(b'\x00', off)
    return d[off:end].decode('latin1')


o = 20
for _ in range(count):
    ko, fmt, ln, mx, vo = struct.unpack('<HHIII', d[o:o + 16])
    o += 16
    k = cstr(key_start + ko)
    raw = d[data_start + vo:data_start + vo + ln]
    if fmt == 0x0404:
        v = raw.rstrip(b'\x00').decode('utf-8', 'replace')
    elif fmt == 0x0204:
        v = int.from_bytes(raw, 'big')
    elif fmt == 0x0004:
        v = raw.hex()
    else:
        v = raw.hex()
    print('%-24s fmt=0x%04x len=%d %r' % (k, fmt, ln, v))
