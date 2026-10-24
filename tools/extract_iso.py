"""Extract the file tree of a plaintext (decrypted) PS3 ISO9660 image.

Handles ISO9660 directory records, including multi-extent files (flag 0x80)
which this disc uses for files larger than 4 GiB-ish blocks.

Usage: python extract_iso.py [iso] [outdir]
"""
import glob
import os
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_OUT = os.path.join(ROOT, 'game')


def find_iso():
    """The dump is user-supplied, so its filename is not fixed."""
    found = sorted(glob.glob(os.path.join(ROOT, 'iso', '*.iso')))
    return found[0] if found else ''


ISO = sys.argv[1] if len(sys.argv) > 1 else find_iso()
OUT = sys.argv[2] if len(sys.argv) > 2 else DEFAULT_OUT

if not ISO:
    sys.exit('no .iso in iso/ -- put your own disc dump there')

f = open(ISO, 'rb')


def read_sectors(lba, count):
    f.seek(lba * 2048)
    return f.read(2048 * count)


def le32(b, o):
    return struct.unpack('<I', b[o:o + 4])[0]


def list_dir(lba, size):
    data = read_sectors(lba, (size + 2047) // 2048)
    recs = []
    o = 0
    while o < len(data):
        ln = data[o]
        if ln == 0:
            o = ((o // 2048) + 1) * 2048
            continue
        r = data[o:o + ln]
        o += ln
        nlen = r[32]
        raw = r[33:33 + nlen]
        if raw in (b'\x00', b'\x01'):
            continue
        name = raw.decode('latin1')
        if name.endswith(';1'):
            name = name[:-2]
        recs.append({
            'name': name,
            'lba': le32(r, 2),
            'size': le32(r, 10),
            'dir': bool(r[25] & 2),
            'multi': bool(r[25] & 0x80),
        })
    return recs


def walk(lba, size, dest, depth=0):
    recs = list_dir(lba, size)
    # group consecutive multi-extent / same-name records into one file
    grouped = []
    for r in recs:
        if grouped and grouped[-1]['name'] == r['name']:
            g = grouped[-1]
            g['extents'].append((r['lba'], r['size']))
            g['multi'] = r['multi']
        else:
            grouped.append({'name': r['name'], 'dir': r['dir'], 'multi': r['multi'],
                            'extents': [(r['lba'], r['size'])]})
    for g in grouped:
        path = os.path.join(dest, g['name'])
        if g['dir']:
            os.makedirs(path, exist_ok=True)
            walk(g['extents'][0][0], g['extents'][0][1], path, depth + 1)
            continue
        os.makedirs(os.path.dirname(path), exist_ok=True)
        total = sum(s for _, s in g['extents'])
        with open(path, 'wb') as out:
            for (l, s) in g['extents']:
                f.seek(l * 2048)
                left = s
                while left:
                    chunk = f.read(min(left, 1 << 24))
                    if not chunk:
                        break
                    out.write(chunk)
                    left -= len(chunk)
        print('%-78s %12d' % (path, total), flush=True)


def main():
    pvd = read_sectors(16, 1)
    assert pvd[1:6] == b'CD001', 'not an ISO9660 image'
    root = pvd[156:156 + 34]
    os.makedirs(OUT, exist_ok=True)
    walk(le32(root, 2), le32(root, 10), OUT)


if __name__ == '__main__':
    main()
