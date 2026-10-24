"""Find the code that references a string in the guest ELF.

Usage: python find_string_refs.py <elf> <substring>
Prints the string's guest address and every lis/addi (or lis/ori) pair that
materialises it, with the containing function (from analysis/EBOOT.functions.json
when present) and a short disassembly window.
"""
import json
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "ps3recomp-src" / "tools"))
from capstone import Cs, CS_ARCH_PPC, CS_MODE_64
from capstone import CS_MODE_BIG_ENDIAN  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
elf_path = sys.argv[1] if len(sys.argv) > 1 else str(ROOT / "game/PS3_GAME/USRDIR/EBOOT.elf")
needle = (sys.argv[2] if len(sys.argv) > 2 else "edgezlib_inflate_queue.cpp").encode()

d = Path(elf_path).read_bytes()
phoff = struct.unpack_from(">Q", d, 0x20)[0]
phentsize, phnum = struct.unpack_from(">HH", d, 0x36)
segs = []
for i in range(phnum):
    o = phoff + i * phentsize
    p_type, p_flags = struct.unpack_from(">II", d, o)
    p_offset, p_vaddr, p_paddr, p_filesz, p_memsz = struct.unpack_from(">5Q", d, o + 8)
    if p_type == 1:
        segs.append((p_offset, p_vaddr, p_filesz, p_flags))
        print("PT_LOAD off=0x%X vaddr=0x%X filesz=0x%X flags=0x%X" % (p_offset, p_vaddr, p_filesz, p_flags))

# file offset -> vaddr
def vaddr(off):
    for p_off, p_va, p_sz, _f in segs:
        if p_off <= off < p_off + p_sz:
            return p_va + (off - p_off)
    return None

hits = []
start = 0
while True:
    i = d.find(needle, start)
    if i < 0:
        break
    hits.append(i)
    start = i + 1
print("string occurrences:", [(hex(o), hex(vaddr(o)) if vaddr(o) else None) for o in hits])
if not hits:
    sys.exit(1)

targets = {vaddr(o) for o in hits if vaddr(o)}
print("target vaddrs:", [hex(t) for t in targets])

# executable segment(s)
text = [s for s in segs if s[3] & 0x1]
md = Cs(CS_ARCH_PPC, CS_MODE_64 | CS_MODE_BIG_ENDIAN)
md.detail = False

# function list for attribution
funcs = []
fjson = ROOT / "analysis" / "EBOOT.functions.json"
if fjson.exists():
    funcs = sorted((int(f["start"], 16), int(f["end"], 16), int(f["opd"], 16))
                   for f in json.loads(fjson.read_text()))

def containing(addr):
    lo, hi = 0, len(funcs) - 1
    best = None
    while lo <= hi:
        mid = (lo + hi) // 2
        if funcs[mid][0] <= addr:
            best = funcs[mid]
            lo = mid + 1
        else:
            hi = mid - 1
    if best and best[0] <= addr < max(best[1], best[0] + 0x100000):
        return best
    return None

for p_off, p_va, p_sz, _f in text:
    blob = d[p_off:p_off + p_sz]
    for i in range(0, len(blob) - 8, 4):
        w = struct.unpack_from(">I", blob, i)[0]
        op = w >> 26
        if op != 15:          # addis
            continue
        rt = (w >> 21) & 0x1F
        ra = (w >> 16) & 0x1F
        hi = w & 0xFFFF
        if ra != 0:
            continue
        ah = (p_va + i) & 0xFFFF0000 | hi << 16
        for t in targets:
            if (t >> 16) != hi:
                continue
            lo16 = t & 0xFFFF
            for j in range(1, 12):
                w2 = struct.unpack_from(">I", blob, i + 4 * j)[0]
                op2 = w2 >> 26
                rt2 = (w2 >> 21) & 0x1F
                ra2 = (w2 >> 16) & 0x1F
                if op2 not in (14, 24) or rt2 != rt or ra2 != rt:
                    continue
                if (w2 & 0xFFFF) != lo16:
                    continue
                addr = p_va + i
                fn = containing(addr)
                print("\nref at 0x%08X  (in func 0x%08X..0x%08X, opd 0x%08X)" % (
                    addr, fn[0], fn[1], fn[2]) if fn else "\nref at 0x%08X" % addr)
                s = max(0, i - 64)
                for ins in md.disasm(blob[s:i + 64], p_va + s):
                    mark = " <== " if ins.address == addr else "     "
                    print("  %08X%s%-8s %s" % (ins.address, mark, ins.mnemonic, ins.op_str))
                break
