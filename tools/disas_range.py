"""Disassemble a guest address range of the title ELF.

Usage: python disas_range.py <elf> <start> [end] [--calls]
Prints the PPU disassembly; --calls marks bl/bctrl targets and resolves them to
their containing function (from analysis/EBOOT.functions.json).
"""
import bisect
import json
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "ps3recomp-src" / "tools"))
from capstone import Cs, CS_ARCH_PPC, CS_MODE_64
from capstone import CS_MODE_BIG_ENDIAN  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
elf = Path(sys.argv[1])
start = int(sys.argv[2], 0)
end = int(sys.argv[3], 0) if len(sys.argv) > 3 and not sys.argv[3].startswith("--") else None

d = elf.read_bytes()
phoff = struct.unpack_from(">Q", d, 0x20)[0]
phentsize, phnum = struct.unpack_from(">HH", d, 0x36)
segs = []
for i in range(phnum):
    o = phoff + i * phentsize
    p_type, p_flags = struct.unpack_from(">II", d, o)
    p_off, p_va, _pa, p_filesz, _memsz = struct.unpack_from(">5Q", d, o + 8)
    if p_type == 1 and p_filesz:
        segs.append((p_off, p_va, p_filesz))
if end is None:
    end = start + 0x100

blob = None
base = 0
for p_off, p_va, p_sz in segs:
    if p_va <= start < p_va + p_sz:
        base = p_va
        blob = d[p_off:p_off + p_sz]
        break
if blob is None:
    sys.exit("address not in a segment")

funcs = []
fjson = ROOT / "analysis" / "EBOOT.functions.json"
if fjson.exists():
    funcs = sorted((int(f["start"], 16), int(f["end"], 16)) for f in json.loads(fjson.read_text()))
starts = [a for a, _ in funcs]


def fn_of(a):
    i = bisect.bisect_right(starts, a) - 1
    if i < 0:
        return None
    s, e = funcs[i]
    return (s, e) if a < max(e, s + 0x40000) else None


md = Cs(CS_ARCH_PPC, CS_MODE_64 | CS_MODE_BIG_ENDIAN)
md.detail = True
code = blob[start - base:end - base]
for ins in md.disasm(code, start):
    note = ""
    if ins.id in (0,):
        pass
    if ins.mnemonic in ("bl", "bctrl"):
        try:
            tgt = int(ins.op_str.replace("0x", ""), 16)
            f = fn_of(tgt)
            note = "   -> func_%08X" % (f[0] if f else tgt)
        except ValueError:
            pass
    print("%08X  %-8s %-24s%s" % (ins.address, ins.mnemonic, ins.op_str, note))
