#!/usr/bin/env python3
"""diff_trace.py -- compare a RPCS3 command_dump.log against a recomp manifest.txt.

Both inputs are decoded to the same per-draw schema (guest RSX state at each
draw), then the two draw streams are aligned by signature (fragment program,
colour target, vertex count) and compared field by field. Frame numbers never
match between emulator and recomp (different FPS), so alignment is by content,
not by frame.

Usage:
    .venv\\Scripts\\python tools\\diff_trace.py \
        diagnostics/arena/<emu-dir>/command_dump.log \
        diagnostics/arena/<recomp-dir>/frame_NNN/manifest.txt
"""
import re
import sys
from collections import Counter

# ---------------------------------------------------------------------------
# Decoding tables (RSX/GL enums as printed by both sides)

GL_CMP = {"Never": 0, "Less": 1, "Equal": 2, "Lequal": 3, "Less_equal": 3,
          "Greater": 4, "Notequal": 5, "Not_equal": 5, "Gequal": 6,
          "Greater_equal": 6, "Always": 7}
GL_CMP_NAMES = {v: k for k, v in GL_CMP.items()}

# RSX primitive enums (NV4097 order; emulator prints names)
PRIM = {"Points": 1, "Lines": 2, "Line_loop": 3, "Line_strip": 4,
        "Triangles": 5, "Triangle_strip": 6, "Triangle_fan": 7, "Quads": 8,
        "Quad_strip": 9, "Polygon": 10}

# D3D12 blend enums (recomp blend_key) -> RPCS3-style factor names
D3D_FACTOR = {1: "0", 2: "1", 3: "src.color", 4: "(1 - src.color)", 5: "src.a",
              6: "(1 - src.a)", 7: "dst.a", 8: "(1 - dst.a)", 9: "dst.color",
              10: "(1 - dst.color)", 11: "src.a_sat", 14: "constant.color",
              15: "(1 - constant.color)"}
D3D_OP = {1: "Add", 2: "Subtract", 3: "Rev subtract", 4: "Min", 5: "Max"}

# ---------------------------------------------------------------------------
# Emulator side: RPCS3 command_dump.log

# RSX blend factor names as printed by RPCS3's to_blend_factor (rsx_decode.h)
EMU_BLEND_FACTOR = {"1": "1", "0": "0", "src.a": "src.a", "src.color": "src.color",
                    "(1 - src.a)": "(1 - src.a)", "(1 - src.color)": "(1 - src.color)",
                    "dst.a": "dst.a", "(1 - dst.a)": "(1 - dst.a)",
                    "dst.color": "dst.color", "(1 - dst.color)": "(1 - dst.color)",
                    "src.a_sat": "src.a_sat", "constant.color": "constant.color",
                    "(1 - constant.color)": "(1 - constant.color)",
                    "constant.a": "constant.a", "(1 - constant.a)": "(1 - constant.a)"}


class EmuState:
    """Aggregates the pretty-printed RSX command stream into draw records."""

    def __init__(self):
        self.surf = {"A": 0, "B": 0, "C": 0, "D": 0, "Z": 0}
        self.viewport = (0, 0, 1024, 1024)
        self.scissor = (0, 0, 0, 0)
        self.depth = {"en": False, "write": True, "func": 1}
        self.alpha = {"en": False, "func": 7, "ref": 0}
        self.blend = {"en": False, "eq_rgb": "Add", "eq_a": "Add",
                      "sf_rgb": "1", "sf_a": "1", "df_rgb": "0", "df_a": "0"}
        self.cull = {"en": False, "face": "back"}
        self.cmask = 0
        self.prim = 0
        self.fp = None
        self.tex = {}
        self.idx = {"off": None, "type": None}
        self.constant_load = None
        self.constants = []
        self.draws = []

    def _tex(self, unit):
        return self.tex.setdefault(unit, {"off": 0, "loc": "Local", "en": False,
                                          "address": 0, "filter": 0})

    def feed(self, line):
        s = line.strip()
        if not s:
            return
        t = self._tex

        m = re.match(r"^Surface: ([A-Z]) offset 0x([0-9a-fA-F]+)", s)
        if m:
            self.surf[m.group(1)] = int(m.group(2), 16)
            return
        m = re.match(r"^Viewport: x: (-?\d+) width: (-?\d+)", s)
        if m:
            self.viewport = (int(m.group(1)), self.viewport[1],
                             int(m.group(2)), self.viewport[3])
            return
        m = re.match(r"^Viewport: y: (-?\d+) height: (-?\d+)", s)
        if m:
            self.viewport = (self.viewport[0], int(m.group(1)),
                             self.viewport[2], int(m.group(2)))
            return
        m = re.match(r"^Scissor: x: (-?\d+) width: (-?\d+)", s)
        if m:
            self.scissor = (int(m.group(1)), self.scissor[1],
                            int(m.group(2)), self.scissor[3])
            return
        m = re.match(r"^Scissor: y: (-?\d+) height: (-?\d+)", s)
        if m:
            self.scissor = (self.scissor[0], int(m.group(1)),
                            self.scissor[2], int(m.group(2)))
            return
        if s.startswith("Depth: test"):
            self.depth["en"] = "true" in s
            return
        if s.startswith("Depth: write"):
            self.depth["write"] = "true" in s
            return
        m = re.match(r"^Depth: compare_function: (\w+)", s)
        if m:
            self.depth["func"] = GL_CMP.get(m.group(1), -1)
            return
        if s.startswith("Alpha: test"):
            self.alpha["en"] = "true" in s
            return
        m = re.match(r"^Alpha: compare_function: (\w+)", s)
        if m:
            self.alpha["func"] = GL_CMP.get(m.group(1), -1)
            return
        m = re.match(r"^Alpha: ref unorm8: ([\d.]+)", s)
        if m:
            self.alpha["ref"] = round(float(m.group(1)) * 255)
            return
        if s.startswith("Blend: mrt"):
            return
        if s == "Blend: true" or s == "Blend: false":
            self.blend["en"] = s == "Blend: true"
            return
        m = re.match(r"^Blend: equation rgb: (\w+) a: (\w+)", s)
        if m:
            self.blend["eq_rgb"], self.blend["eq_a"] = m.group(1), m.group(2)
            return
        m = re.match(r"^Blend: sfactor rgb: (.+) a: (.+)", s)
        if m:
            self.blend["sf_rgb"] = EMU_BLEND_FACTOR.get(m.group(1), m.group(1))
            self.blend["sf_a"] = EMU_BLEND_FACTOR.get(m.group(2), m.group(2))
            return
        m = re.match(r"^Blend: dfactor rgb: (.+) a: (.+)", s)
        if m:
            self.blend["df_rgb"] = EMU_BLEND_FACTOR.get(m.group(1), m.group(1))
            self.blend["df_a"] = EMU_BLEND_FACTOR.get(m.group(2), m.group(2))
            return
        if s.startswith("Cull face:"):
            self.cull["en"] = "true" in s
            return
        m = re.match(r"^Cull Face: (back|front)", s)
        if m:
            self.cull["face"] = m.group(1)
            return
        m = re.match(r"^Surface: color mask A = (\w+) R = (\w+) G = (\w+) B = (\w+)", s)
        if m:
            a, r_, g, b = (x == "true" for x in m.groups())
            self.cmask = (1 if r_ else 0) | (2 if g else 0) | (4 if b else 0) | (8 if a else 0)
            return
        m = re.match(r"^NV4097_SET_COLOR_MASK_MRT: 0x([0-9a-fA-F]+)", s)
        if m:
            self.cmask = int(m.group(1), 16)
            return
        m = re.match(r"^Primitive: (\w+)", s)
        if m:
            self.prim = PRIM.get(m.group(1), 0)
            return
        m = re.match(r"^Shader: (\w+), offset: 0x([0-9a-fA-F]+)", s)
        if m:
            self.fp = int(m.group(2), 16)
            return
        m = re.match(r"^Texture (\d+): Offset: 0x([0-9a-fA-F]+)", s)
        if m:
            t(int(m.group(1)))["off"] = int(m.group(2), 16)
            return
        m = re.match(r"^Texture (\d+): (\w+), Cubemap:", s)
        if m:
            t(int(m.group(1)))["loc"] = m.group(2)
            return
        m = re.match(r"^Texture (\d+): (true|false), Min/Max LOD: (\d+)/(\d+), Max Aniso: (\d+), AKill: (\w+)", s)
        if m:
            tx = t(int(m.group(1)))
            tx["en"] = m.group(2) == "true"
            tx["minlod"], tx["maxlod"], tx["aniso"] = (int(m.group(3)),
                                                       int(m.group(4)),
                                                       int(m.group(5)))
            return
        m = re.match(r"^NV4097_SET_TEXTURE_ADDRESS( \+ 0x[0-9a-fA-F]+)?: 0x([0-9a-fA-F]+)", s)
        if m:
            unit = 0 if not m.group(1) else int(m.group(1).strip(" +x"), 16) // 0x20
            t(unit)["address"] = int(m.group(2), 16)
            return
        m = re.match(r"^NV4097_SET_TEXTURE_FILTER( \+ 0x[0-9a-fA-F]+)?: 0x([0-9a-fA-F]+)", s)
        if m:
            unit = 0 if not m.group(1) else int(m.group(1).strip(" +x"), 16) // 0x20
            t(unit)["filter"] = int(m.group(2), 16)
            return
        m = re.match(r"^Index: array offset 0x([0-9a-fA-F]+)", s)
        if m:
            self.idx["off"] = int(m.group(1), 16)
            return
        m = re.match(r"^Index: type: (\w+)", s)
        if m:
            self.idx["type"] = m.group(1)
            return
        m = re.match(r"^Transform constant load: (\d+)", s)
        if m:
            self.constant_load = int(m.group(1))
            return
        m = re.match(r"^Draw vertexes range \{IdxArray\[(\d+)\], IdxArray\[(\d+)\]\}", s)
        if m:
            first, last = int(m.group(1)), int(m.group(2))
            self.draws.append(self._draw(last - first))
            return
        m = re.match(r"^Draw arrays range \{(\d+), (\d+)\}", s)
        if m:
            self.draws.append(self._draw(int(m.group(2)) - int(m.group(1))))

    def _draw(self, count):
        return {
            "rt": self.surf["A"],
            "mrt": [self.surf["B"], self.surf["C"], self.surf["D"]],
            "zeta": self.surf["Z"],
            "fp": self.fp,
            "prim": self.prim,
            "count": count,
            "cmask": self.cmask,
            "blend": dict(self.blend),
            "depth": dict(self.depth),
            "alpha": dict(self.alpha),
            "cull": dict(self.cull),
            "viewport": self.viewport,
            "idx_off": self.idx["off"],
            "textures": {u: dict(v) for u, v in self.tex.items()},
        }


def parse_emulator(path):
    st = EmuState()
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            st.feed(line)
    return st.draws


# ---------------------------------------------------------------------------
# Recomp side: manifest.txt

def parse_recomp(path):
    draws = []
    cur = None
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        for line in f:
            s = line.strip()
            m = re.match(r"^op\d+ DRAW  rt=0x([0-9a-fA-F]+) mrt=0x([0-9a-fA-F]+)/0x([0-9a-fA-F]+)/0x([0-9a-fA-F]+) vp=(-?\d+),(-?\d+),(\d+)x(\d+) fp=0x([0-9a-fA-F]+) vs=(-?\d+) n=(\d+) cmask=([0-9a-fA-F]+) blend=(\d+) bk=0x([0-9a-fA-F]+)", s)
            if m:
                cur = {
                    "rt": int(m.group(1), 16),
                    "mrt": [int(m.group(2), 16), int(m.group(3), 16), int(m.group(4), 16)],
                    "viewport": (int(m.group(5)), int(m.group(6)),
                                 int(m.group(7)), int(m.group(8))),
                    "fp": int(m.group(9), 16),
                    "prim": 0, "count": int(m.group(11)),
                    "cmask": int(m.group(12), 16),
                    "blend_key": int(m.group(14), 16),
                    "depth_key": None, "alpha_ctl": None, "cull_key": None,
                    "textures": {},
                }
                draws.append(cur)
                continue
            m = re.match(r"^\s*depth=([0-9a-fA-F]+) bias=(\d+)/(\S+) cull=([0-9a-fA-F]+) actl=([0-9a-fA-F]+)", s)
            if m and cur is not None:
                cur["depth_key"] = int(m.group(1), 16)
                cur["cull_key"] = int(m.group(4), 16)
                cur["alpha_ctl"] = int(m.group(5), 16)
                continue
            m = re.match(r"^t(\d+) off=0x([0-9a-fA-F]+) fmt=0x[0-9a-fA-F]+ \(\w+\) (\d+)x(\d+) remap=0x[0-9a-fA-F]+ address=([0-9a-fA-F]+) filter=([0-9a-fA-F]+) control=([0-9a-fA-F]+) border=([0-9a-fA-F]+)", s)
            if m and cur is not None:
                cur["textures"][int(m.group(1))] = {
                    "off": int(m.group(2), 16), "address": int(m.group(5), 16),
                    "filter": int(m.group(6), 16), "control": int(m.group(7), 16),
                }
                continue
    return draws


def decode_blend_key(key):
    """D3D12-packed blend key -> emulator-style names."""
    if not key:
        return {"en": False}
    return {
        "en": bool(key & 1),
        "sf_rgb": D3D_FACTOR.get((key >> 1) & 0x1F, "?"),
        "df_rgb": D3D_FACTOR.get((key >> 6) & 0x1F, "?"),
        "sf_a": D3D_FACTOR.get((key >> 11) & 0x1F, "?"),
        "df_a": D3D_FACTOR.get((key >> 16) & 0x1F, "?"),
        "eq_rgb": D3D_OP.get((key >> 21) & 7, "?"),
        "eq_a": D3D_OP.get((key >> 24) & 7, "?"),
    }


def decode_depth_key(key):
    if key is None:
        return None
    return {"en": bool(key & 1), "write": bool(key & 2),
            "func": (key >> 2) & 7}


def decode_alpha_ctl(ctl):
    if ctl is None:
        return None
    return {"en": bool(ctl & 0x10000), "func": (ctl >> 8) & 7, "ref": ctl & 0xFF}


def decode_cull_key(key):
    if key is None:
        return None
    return {"en": bool(key & 1), "face": "front" if key & 2 else "back"}


# ---------------------------------------------------------------------------
# Comparison

def rank_pair_delta(emu_draws, rec_draws):
    """Sessions allocate memory at different bases: every fragment program
    address is shifted by a constant delta. Recover it by rank-pairing the two
    fp frequency histograms (the game loads the same programs in the same
    order)."""
    def hist(draws):
        c = Counter()
        for d in draws:
            if d["fp"] is not None:
                c[d["fp"]] += 1
        return c
    eh, rh = hist(emu_draws), hist(rec_draws)
    e_ranked = sorted(eh, key=lambda fp: (-eh[fp], fp))
    r_ranked = sorted(rh, key=lambda fp: (-rh[fp], fp))
    deltas = Counter()
    for e, r in zip(e_ranked, r_ranked):
        deltas[r - e] += eh[e] * rh[r]
    best = deltas.most_common(1)[0]
    total = sum(deltas.values())
    return best[0], best[1] / total


def map_rts(emu_draws, rec_draws, fp_delta):
    """Match emulator render targets to recomp ones (one-to-one) by draw
    overlap: same (normalized fp) drawing to both targets, with a bonus when
    the modal viewport size of draws to both targets agrees."""
    def viewport_size(draws, rt):
        c = Counter(d["viewport"][2:] for d in draws if d["rt"] == rt)
        return c.most_common(1)[0][0] if c else None
    e_rts = {d["rt"] for d in emu_draws}
    r_rts = {d["rt"] for d in rec_draws}
    scores = []
    for ert in e_rts:
        efp = Counter(d["fp"] + fp_delta for d in emu_draws if d["rt"] == ert and d["fp"] is not None)
        esize = viewport_size(emu_draws, ert)
        for rrt in r_rts:
            rfp = Counter(d["fp"] for d in rec_draws if d["rt"] == rrt)
            overlap = sum(min(efp[fp], rfp[fp]) for fp in efp if fp in rfp)
            if not overlap:
                continue
            rsize = viewport_size(rec_draws, rrt)
            score = overlap + (overlap * 2 if esize and esize == rsize else 0)
            scores.append((score, overlap, ert, rrt))
    taken_r, pairs = set(), {}
    for score, overlap, ert, rrt in sorted(scores, reverse=True):
        if ert in pairs or rrt in taken_r:
            continue
        pairs[ert] = (rrt, overlap)
        taken_r.add(rrt)
    return pairs


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(2)
    emu_path, recomp_path = sys.argv[1], sys.argv[2]

    emu = parse_emulator(emu_path)
    rec = parse_recomp(recomp_path)
    print(f"emulator draws: {len(emu)}   recomp draws: {len(rec)}")

    fp_delta, fp_conf = rank_pair_delta(emu, rec)
    print(f"fp base delta: +0x{fp_delta:X}  (confidence {fp_conf:.0%})")

    rt_map = map_rts(emu, rec, fp_delta)
    print("rt mapping (emu -> recomp):")
    for ert, (rrt, score) in sorted(rt_map.items()):
        print(f"  0x{ert:08X} -> 0x{rrt:08X}  (overlap {score})")

    def emu_rt(d):
        return rt_map[d["rt"]][0] if d["rt"] in rt_map else None

    def sig_e(d):
        return (d["fp"] + fp_delta if d["fp"] is not None else -1, emu_rt(d))

    def sig_r(d):
        return (d["fp"], d["rt"])

    # Pair draws per (fp, rt) group, in order inside the group. Frames are the
    # same scene at different moments, so the global sequence never matches;
    # the per-pass draw order does.
    e_groups, r_groups = {}, {}
    for i, d in enumerate(emu):
        e_groups.setdefault(sig_e(d), []).append(i)
    for i, d in enumerate(rec):
        r_groups.setdefault(sig_r(d), []).append(i)

    pairs = []
    group_counts = []
    for sig in sorted(set(e_groups) | set(r_groups)):
        eg, rg = e_groups.get(sig, []), r_groups.get(sig, [])
        for e, r in zip(eg, rg):
            pairs.append((e, r))
        if len(eg) != len(rg):
            group_counts.append((sig, len(eg), len(rg)))

    def dname(d, rebased=False):
        fp = d["fp"]
        if rebased and fp is not None:
            fp += fp_delta
        return ("?" if fp is None else f"0x{fp:08X}") + f" rt=0x{d['rt']:08X} n={d['count']}"

    print(f"aligned pairs: {len(pairs)}")

    if group_counts:
        print("\ndraw count per pass differs (fp,rt | emu vs recomp):")
        for sig, ne, nr in sorted(group_counts, key=lambda x: -(abs(x[1] - x[2])))[:25]:
            print(f"  fp={sig[0]:08X} rt={sig[1]:08X}   emu={ne}  recomp={nr}")

    # Multiset comparison per pass: the game's per-pass state is deterministic,
    # so the *sets* of states used by a pass must agree even when the moments
    # differ. A differing set means the recomp applies different state; a
    # matching set with mismatched pairs is just pairing noise.
    def tex_state(d):
        out = []
        for u in sorted(d["textures"]):
            if u >= 5:  # the recomp tracks RSX_MAX_TEXTURES=5 units; the
                continue  # emulator dump prints all 16 (defaults included)
            tx = d["textures"][u]
            out.append((u, tx.get("address", 0), tx.get("filter", 0)))
        return tuple(out)

    def emu_state(d):
        b = d["blend"]
        # factors are irrelevant while blending is disabled; the emulator
        # still tracks the last written values, the recomp packs them away
        if not b["en"]:
            return (d["alpha"]["en"], d["alpha"]["func"], d["alpha"]["ref"],
                    False, None, None, None,
                    d["depth"]["en"], d["depth"]["write"], d["depth"]["func"],
                    d["cull"]["en"], d["cull"]["face"], tex_state(d))
        return (d["alpha"]["en"], d["alpha"]["func"], d["alpha"]["ref"],
                True, b.get("eq_rgb"), b.get("sf_rgb"), b.get("df_rgb"),
                d["depth"]["en"], d["depth"]["write"], d["depth"]["func"],
                d["cull"]["en"], d["cull"]["face"], tex_state(d))

    def rec_state(d):
        a = decode_alpha_ctl(d["alpha_ctl"])
        b = decode_blend_key(d["blend_key"])
        de = decode_depth_key(d["depth_key"])
        c = decode_cull_key(d["cull_key"])
        return (a["en"], a["func"], a["ref"],
                b["en"], b.get("eq_rgb"), b.get("sf_rgb"), b.get("df_rgb"),
                de["en"], de["write"], de["func"],
                c["en"], c["face"], tex_state(d))

    print("\nper-pass state multiset differences:")
    reported = 0
    for sig in sorted(set(e_groups) | set(r_groups)):
        eg, rg = e_groups.get(sig, []), r_groups.get(sig, [])
        if not eg or not rg:
            continue
        es = Counter(emu_state(emu[e]) for e in eg)
        rs = Counter(rec_state(rec[r]) for r in rg)
        if es == rs:
            continue
        reported += 1
        if reported > 12:
            print("  ...")
            break
        print(f"  fp={sig[0]:08X} rt={sig[1]:08X}:")
        for st, n in es.items():
            if rs[st] != n:
                print(f"      emu x{n}: {st}")
        for st, n in rs.items():
            if es[st] != n:
                print(f"      recomp x{n}: {st}")

    print("\nper-pass state multiset differences:")
    reported = 0
    for sig in sorted(set(e_groups) | set(r_groups)):
        eg, rg = e_groups.get(sig, []), r_groups.get(sig, [])
        if not eg or not rg:
            continue
        es = Counter(emu_state(emu[e]) for e in eg)
        rs = Counter(rec_state(rec[r]) for r in rg)
        if es == rs:
            continue
        reported += 1
        if reported > 12:
            print("  ...")
            break
        print(f"  fp={sig[0]:08X} rt={sig[1]:08X}:")
        for st, n in es.items():
            if rs[st] != n:
                print(f"      emu x{n}: {st}")
        for st, n in rs.items():
            if es[st] != n:
                print(f"      recomp x{n}: {st}")

    diffs = Counter()
    examples = {}
    for e, r in pairs:
        de, dr = emu[e], rec[r]
        for field, ve, vr in compare_fields(de, dr, fp_delta):
            diffs[field] += 1
            ex = examples.setdefault(field, [])
            if len(ex) < 3:
                ex.append((dname(de, True), ve, vr))

    if diffs:
        print("\nfield differences (aligned draws; texture fields are secondary:"
              "\nthe two frames show different objects, only systematic per-pass"
              "\ntexture diffs are meaningful):")
        for field, n in diffs.most_common():
            print(f"  {n:5d}  {field}")
        print("\nexamples per field (emulator draw | emu value -> recomp value):")
        for field in diffs:
            print(f"  {field}:")
            for name, ve, vr in examples.get(field, []):
                print(f"      {name}   {ve} -> {vr}")
    else:
        print("\nno field differences in aligned draws")

    print("\nfp histogram (emu rebased vs recomp):")
    emu_fp = Counter(sig_e(d)[0] for d in emu if d["fp"] is not None)
    rec_fp = Counter(sig_r(d)[0] for d in rec)
    for fp in sorted(set(emu_fp) | set(rec_fp), key=lambda x: (-max(emu_fp[x], rec_fp[x]), x)):
        mark = "" if emu_fp[fp] == rec_fp[fp] else "  <-- differs"
        print(f"  fp={fp:08X}  emu={emu_fp[fp]:4d}  recomp={rec_fp[fp]:4d}{mark}")


def compare_fields(de, dr, fp_delta):
    """Yield (field, emu_value, recomp_value) for each difference.
    Pointer-valued fields (texture offsets, index buffers) are session-rebased
    and skipped; state fields are directly comparable."""
    if de["fp"] is not None and (de["fp"] + fp_delta) != dr["fp"]:
        yield ("fp", f"0x{de['fp'] + fp_delta:08X}", f"0x{dr['fp']:08X}")
    if de["cmask"] != dr["cmask"]:
        yield ("cmask", f"0x{de['cmask']:02X}", f"0x{dr['cmask']:02X}")
    if de["blend"]["en"] != bool(dr["blend_key"] & 1):
        yield ("blend.en", de["blend"]["en"], bool(dr["blend_key"] & 1))
    rb = decode_blend_key(dr["blend_key"])
    if de["blend"]["en"] and rb["en"]:
        for k in ("sf_rgb", "df_rgb", "sf_a", "df_a", "eq_rgb", "eq_a"):
            if de["blend"][k] != rb[k]:
                yield (f"blend.{k}", de["blend"][k], rb[k])
    rd = decode_depth_key(dr["depth_key"])
    if rd and de["depth"] != rd:
        for k in ("en", "write", "func"):
            if de["depth"][k] != rd[k]:
                if k == "func":
                    yield ("depth.func", GL_CMP_NAMES.get(de["depth"][k], de["depth"][k]),
                           GL_CMP_NAMES.get(rd[k], rd[k]))
                else:
                    yield (f"depth.{k}", de["depth"][k], rd[k])
    ra = decode_alpha_ctl(dr["alpha_ctl"])
    if ra and de["alpha"]["en"] != ra["en"]:
        yield ("alpha.en", de["alpha"]["en"], ra["en"])
    if ra and de["alpha"]["en"] and ra["en"]:
        # func/ref only matter while the test is enabled
        if de["alpha"]["func"] != ra["func"]:
            yield ("alpha.func", GL_CMP_NAMES.get(de["alpha"]["func"], de["alpha"]["func"]),
                   GL_CMP_NAMES.get(ra["func"], ra["func"]))
        if de["alpha"]["ref"] != ra["ref"]:
            yield ("alpha.ref", de["alpha"]["ref"], ra["ref"])
    rc = decode_cull_key(dr["cull_key"])
    if rc and de["cull"]["en"] != rc["en"]:
        yield ("cull.en", de["cull"]["en"], rc["en"])
    if rc and rc["en"] and de["cull"]["face"] != rc["face"]:
        yield ("cull.face", de["cull"]["face"], rc["face"])
    units = set(de["textures"]) & set(dr["textures"])
    for u in sorted(units):
        te = de["textures"].get(u, {})
        tr = dr["textures"].get(u, {})
        if te.get("address", 0) != tr.get("address", 0):
            yield (f"tex{u}.address", f"0x{te.get('address', 0):08X}",
                   f"0x{tr.get('address', 0):08X}")
        if te.get("filter", 0) != tr.get("filter", 0):
            yield (f"tex{u}.filter", f"0x{te.get('filter', 0):08X}",
                   f"0x{tr.get('filter', 0):08X}")
        if te.get("en") is not None and tr.get("control") is not None:
            # CONTROL0: enabled = bit 31 (rsx_decode.h bf_decoder<31,1>)
            if te.get("en") != bool(tr.get("control") & 0x80000000):
                yield (f"tex{u}.enabled", te.get("en"), bool(tr.get("control") & 0x80000000))


def selftest():
    """Sanity checks on synthetic + real capture pairs (assert-based)."""
    import os
    base = os.path.join(os.path.dirname(__file__), "..", "diagnostics", "arena")
    emu_log = os.path.join(base, "rpcs3-aquos-trace-20261101", "command_dump.log")
    manifest = os.path.join(base, "aquos-compare-20261101", "frame_100", "manifest.txt")
    assert os.path.exists(emu_log) and os.path.exists(manifest), "capture pair missing"
    emu = parse_emulator(emu_log)
    rec = parse_recomp(manifest)
    assert len(emu) > 1000 and len(rec) > 1000, "draw counts implausible"
    delta, conf = rank_pair_delta(emu, rec)
    assert conf > 0.5, f"fp delta confidence too low ({conf})"
    rt_map = map_rts(emu, rec, delta)
    assert 0x0EAB0000 in (rrt for rrt, _ in rt_map.values()), "main RT not mapped"
    print(f"selftest ok: {len(emu)} emu draws, {len(rec)} recomp draws, "
          f"delta=+0x{delta:X}, rts={len(rt_map)}")


if __name__ == "__main__":
    if len(sys.argv) == 2 and sys.argv[1] == "--selftest":
        selftest()
    else:
        main()
