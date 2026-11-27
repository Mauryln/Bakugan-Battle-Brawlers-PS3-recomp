"""Generate whitelisted 2D Xbox 360 and Nintendo Switch button prompt atlases for Bakugan."""
from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path
import re
import struct
import sys
import zlib

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

FONT_ATLAS_HASH = "a748b20fa470925a"
FNV1A_OFFSET_BASIS = 0xCBF29CE484222325
FNV1A_PRIME = 0x100000001B3
U64_MASK = 0xFFFFFFFFFFFFFFFF

SWITCH_FILL = (40, 42, 48, 255)
SWITCH_RIM = (218, 222, 230, 255)
WHITE = (255, 255, 255, 255)

STYLE_SPECS: dict[str, dict[str, tuple]] = {
    "xbox": {
        "east": ("circle", "B", (208, 36, 42, 255), (255, 206, 208, 255), WHITE),
        "south": ("circle", "A", (34, 158, 62, 255), (196, 248, 206, 255), WHITE),
        "north": ("circle", "Y", (222, 154, 16, 255), (255, 236, 178, 255), WHITE),
        "west": ("circle", "X", (36, 108, 214, 255), (198, 224, 255, 255), WHITE),
        "select": ("pill", "BACK", (36, 38, 44, 255), (212, 218, 226, 255), WHITE),
        "start": ("pill", "START", (36, 38, 44, 255), (212, 218, 226, 255), WHITE),
        "l1": ("bumper", "LB"),
        "r1": ("bumper", "RB"),
        "l2": ("trigger", "LT"),
        "r2": ("trigger", "RT"),
        "l3_label": ("stick_label", "LS"),
        "r3_label": ("stick_label", "RS"),
        "pad": ("controller", "xbox", False),
        "pad_glow": ("controller", "xbox", True),
    },
    "nintendo": {
        "east": ("circle", "A", SWITCH_FILL, SWITCH_RIM, WHITE),
        "south": ("circle", "B", SWITCH_FILL, SWITCH_RIM, WHITE),
        "north": ("circle", "X", SWITCH_FILL, SWITCH_RIM, WHITE),
        "west": ("circle", "Y", SWITCH_FILL, SWITCH_RIM, WHITE),
        "select": ("switch_symbol", "-", SWITCH_FILL, SWITCH_RIM, WHITE),
        "start": ("switch_symbol", "+", SWITCH_FILL, SWITCH_RIM, WHITE),
        "l1": ("bumper", "L"),
        "r1": ("bumper", "R"),
        "l2": ("trigger", "ZL"),
        "r2": ("trigger", "ZR"),
        "l3_label": ("stick_label", "LS"),
        "r3_label": ("stick_label", "RS"),
        "pad": ("controller", "nintendo", False),
        "pad_glow": ("controller", "nintendo", True),
    },
}


@dataclass(frozen=True)
class PatchSlot:
    box: tuple[int, int, int, int]
    slot: str
    clear_mode: str = "rect"


# font/font.bin (256x256, hash a748b20fa470925a) shared across all 7 languages.
FONT_SLOTS: list[PatchSlot] = [
    PatchSlot((4, 3, 25, 25), "east"),
    PatchSlot((32, 3, 53, 25), "south"),
    PatchSlot((60, 3, 81, 25), "north"),
    PatchSlot((89, 3, 110, 25), "west"),
    PatchSlot((118, 1, 155, 25), "select"),
    PatchSlot((162, 1, 194, 26), "start"),
    PatchSlot((3, 31, 29, 46), "l1"),
    PatchSlot((37, 31, 63, 46), "r1"),
    PatchSlot((72, 32, 98, 50), "l2"),
    PatchSlot((106, 32, 132, 50), "r2"),
    PatchSlot((140, 70, 167, 82), "l3_label"),
    PatchSlot((175, 70, 202, 82), "r3_label"),
]

# Style-agnostic UV slot recipes across all spani/ UI GTF textures.
SPANI_RECIPES: dict[tuple[str, str], list[PatchSlot]] = {
    ("spani/ps3_game", "ps3_g_16.gtf"): [
        PatchSlot((356, 5, 599, 171), "pad"),
        PatchSlot((617, 185, 685, 222), "l1"),
        PatchSlot((694, 184, 763, 221), "r1"),
        PatchSlot((496, 285, 554, 321), "select"),
        PatchSlot((791, 276, 850, 320), "r2"),
        PatchSlot((790, 325, 851, 371), "l2"),
    ],
    ("spani/window", "win_16.gtf"): [
        PatchSlot((746, 2, 778, 34), "south", "circle"),
    ],
    ("spani/common", "common_16.gtf"): [
        PatchSlot((438, 329, 470, 361), "south", "circle"),
        PatchSlot((4, 930, 72, 967), "l1"),
        PatchSlot((86, 929, 155, 966), "r1"),
        PatchSlot((249, 920, 291, 943), "l1"),
        PatchSlot((305, 919, 347, 942), "r1"),
    ],
    ("spani/game", "g_16.gtf"): [
        PatchSlot((177, 433, 209, 465), "west", "circle"),
    ],
    ("spani/game", "g_16_button.gtf"): [
        PatchSlot((7, 77, 250, 243), "pad"),
        PatchSlot((1, 248, 255, 428), "pad_glow"),
        PatchSlot((0, 439, 72, 509), "west", "circle"),
        PatchSlot((74, 440, 146, 512), "south", "circle"),
        PatchSlot((150, 440, 246, 511), "pad"),
    ],
    ("spani/game", "g_for_gtarget.gtf"): [
        PatchSlot((4, 430, 109, 508), "r2"),
    ],
    ("spani/name", "name_16.gtf"): [
        PatchSlot((466, 2, 508, 36), "start"),
        PatchSlot((477, 43, 511, 75), "west", "circle"),
        PatchSlot((477, 84, 509, 116), "north", "circle"),
        PatchSlot((344, 128, 382, 157), "l2"),
        PatchSlot((471, 127, 509, 155), "r2"),
    ],
    ("spani/shop", "shop_16.gtf"): [
        PatchSlot((670, 246, 738, 283), "l1"),
        PatchSlot((753, 245, 822, 282), "r1"),
    ],
    ("spani/rule_1", "Rule_1_16.gtf"): [
        PatchSlot((147, 263, 179, 295), "south", "circle"),
    ],
    ("spani/deck_sel", "de_sel_16.gtf"): [
        PatchSlot((338, 499, 378, 521), "l1"),
        PatchSlot((397, 499, 437, 521), "r1"),
    ],
    ("spani/edit", "edit_16.gtf"): [
        PatchSlot((345, 2, 385, 24), "r1"),
        PatchSlot((396, 2, 436, 24), "l1"),
    ],
    ("spani/deck_data", "deck_data_16.gtf"): [
        PatchSlot((119, 19, 151, 51), "south", "circle"),
        PatchSlot((119, 64, 151, 96), "south", "circle"),
        PatchSlot((250, 18, 283, 50), "east", "circle"),
        PatchSlot((250, 63, 283, 95), "east", "circle"),
    ],
}

GAME_G16_3_CROSS_BY_ARCHIVE: dict[str, tuple[int, int, int, int]] = {
    "game.dat": (565, 715, 619, 769),
    "game_ger.dat": (589, 715, 643, 769),
    "game_fre.dat": (607, 715, 661, 769),
    "game_spa.dat": (565, 715, 619, 769),
    "game_ita.dat": (565, 715, 619, 769),
    "game_dut.dat": (595, 715, 649, 769),
    "game_swe.dat": (567, 715, 621, 769),
}

GAME_WII16_CROSS_BY_ARCHIVE: dict[str, tuple[tuple[int, int, int, int], tuple[int, int, int, int]]] = {
    "game.dat": ((132, 1, 166, 35), (132, 37, 166, 71)),
    "game_ger.dat": ((132, 2, 166, 36), (132, 38, 166, 72)),
    "game_fre.dat": ((158, 2, 192, 36), (158, 38, 192, 72)),
    "game_spa.dat": ((132, 2, 166, 36), (132, 38, 166, 72)),
    "game_ita.dat": ((132, 2, 166, 36), (132, 38, 166, 72)),
    "game_dut.dat": ((144, 2, 178, 36), (144, 38, 178, 72)),
    "game_swe.dat": ((128, 2, 162, 36), (128, 38, 162, 72)),
}

TARGET_SPANI_FOLDERS: frozenset[str] = frozenset({
    "spani/ps3_game",
    "spani/window",
    "spani/common",
    "spani/game",
    "spani/name",
    "spani/shop",
    "spani/rule_1",
    "spani/deck_sel",
    "spani/edit",
    "spani/deck_data",
})


def slots_for_spani_gtf(folder: str, archive_base: str, gtf_name: str) -> list[PatchSlot]:
    if folder == "spani/game" and gtf_name == "g_16_3.gtf":
        slots = [
            PatchSlot((15, 817, 104, 865), "l1"),
            PatchSlot((125, 818, 214, 865), "r1"),
        ]
        cross = GAME_G16_3_CROSS_BY_ARCHIVE.get(archive_base)
        if cross:
            slots.append(PatchSlot(cross, "south", "circle"))
        return slots

    if folder == "spani/game" and gtf_name == "wii_16.gtf":
        slots = [
            PatchSlot((263, 119, 296, 151), "north", "circle"),
            PatchSlot((1, 212, 62, 255), "pad"),
        ]
        pair = GAME_WII16_CROSS_BY_ARCHIVE.get(archive_base)
        if pair:
            slots.append(PatchSlot(pair[0], "south", "circle"))
            slots.append(PatchSlot(pair[1], "south", "circle"))
        return slots

    return SPANI_RECIPES.get((folder, gtf_name), [])


KNOWN_HASH_SLOTS: dict[str, tuple[str, list[PatchSlot]]] = {
    "78ebb10a104045ff": ("spani/ps3_game/ps3_g_16.gtf", SPANI_RECIPES[("spani/ps3_game", "ps3_g_16.gtf")]),
    "669c6f29a0463b84": ("spani/window/win_16.gtf", SPANI_RECIPES[("spani/window", "win_16.gtf")]),
    "784c7f008431a52a": ("spani/game/g_16_button.gtf", SPANI_RECIPES[("spani/game", "g_16_button.gtf")]),
    "f79d7bb55b6de762": ("spani/deck_sel/de_sel_16.gtf", SPANI_RECIPES[("spani/deck_sel", "de_sel_16.gtf")]),
    "2c5fbc3e816c9337": ("spani/edit/edit_16.gtf", SPANI_RECIPES[("spani/edit", "edit_16.gtf")]),
    "858c5300ef3792ee": ("spani/deck_data/deck_data_16.gtf", SPANI_RECIPES[("spani/deck_data", "deck_data_16.gtf")]),
    "e539a6958ceb94ba": ("spani/common/common_spa.dat/common_16.gtf", SPANI_RECIPES[("spani/common", "common_16.gtf")]),
    "c7b6008f69b8fbb7": ("spani/game/game_spa.dat/g_16.gtf", SPANI_RECIPES[("spani/game", "g_16.gtf")]),
    "3869cff920d24630": ("spani/game/game_spa.dat/g_16_3.gtf", slots_for_spani_gtf("spani/game", "game_spa.dat", "g_16_3.gtf")),
    "29d524733fcd2c5c": ("spani/game/game_spa.dat/g_for_gtarget.gtf", SPANI_RECIPES[("spani/game", "g_for_gtarget.gtf")]),
    "65d81d20ee94cfee": ("spani/game/game_spa.dat/wii_16.gtf", slots_for_spani_gtf("spani/game", "game_spa.dat", "wii_16.gtf")),
}


@dataclass(frozen=True)
class PaaEntry:
    name: str
    arc_offset: int
    arc_size: int


def fnv1a64_tex(raw_level0: bytes, width: int, height: int, base_format: int) -> int:
    """Match ui_tex_content_hash() in rsx_d3d12_backend.c."""
    hval = FNV1A_OFFSET_BASIS
    payload = struct.pack("<III", width, height, base_format) + raw_level0
    for byte in payload:
        hval = ((hval ^ byte) * FNV1A_PRIME) & U64_MASK
    return hval


def decompress_segs(blob: bytes) -> bytes:
    """Decompress a PS3 'segs' chunk stream if encountered."""
    if len(blob) < 16 or blob[:4] != b"segs":
        return blob
    _magic, _flags, count, _total = struct.unpack_from(">4sHHI", blob, 0)
    out = bytearray()
    pos = 16
    for _ in range(count):
        if pos + 8 > len(blob):
            break
        raw_sz, comp_sz, off = struct.unpack_from(">HHI", blob, pos)
        pos += 8
        is_comp = (off & 1) != 0
        off &= ~1
        if raw_sz == 0:
            raw_sz = 65536
        if comp_sz == 0:
            comp_sz = raw_sz
        chunk = blob[off:off + comp_sz]
        if is_comp:
            out.extend(zlib.decompress(chunk, -15))
        else:
            out.extend(chunk)
    return bytes(out)


def read_paa_entries(bin_path: Path) -> list[PaaEntry]:
    hdr = bin_path.read_bytes()
    magic, _ver, count, tbl_off, dir_off = struct.unpack_from(">4sIIII", hdr, 0)
    if magic != b"PAA\x00":
        raise ValueError(f"Unsupported PAA header in {bin_path}: {magic!r}")
    entries: list[PaaEntry] = []
    for i in range(count):
        name_off, arc_size, _u2, _u3 = struct.unpack_from(">IIII", hdr, tbl_off + i * 16)
        (arc_off,) = struct.unpack_from(">I", hdr, dir_off + i * 4)
        end = hdr.find(b"\x00", name_off)
        name = hdr[name_off:end].decode("ascii", errors="replace")
        entries.append(PaaEntry(name=name, arc_offset=arc_off, arc_size=arc_size))
    return entries


def decode_rsx_level0(raw: bytes, w: int, h: int, basef: int) -> Image.Image:
    if basef == 0x85:
        arr = np.frombuffer(raw[:w * h * 4], dtype=np.uint8).reshape(h, w, 4)
        rgba = np.empty_like(arr)
        rgba[..., 0] = arr[..., 1]
        rgba[..., 1] = arr[..., 2]
        rgba[..., 2] = arr[..., 3]
        rgba[..., 3] = arr[..., 0]
        return Image.fromarray(rgba, "RGBA")

    bw, bh = (w + 3) // 4, (h + 3) // 4
    is_bc1 = basef == 0x86
    is_bc2 = basef == 0x87
    bsz = 8 if is_bc1 else 16
    blocks = np.frombuffer(raw[:bw * bh * bsz], dtype=np.uint8).reshape(bh, bw, bsz)
    cblk = blocks if is_bc1 else blocks[..., 8:16]
    c0 = cblk[..., 0].astype(np.uint16) | (cblk[..., 1].astype(np.uint16) << 8)
    c1 = cblk[..., 2].astype(np.uint16) | (cblk[..., 3].astype(np.uint16) << 8)
    cbits = (
        cblk[..., 4].astype(np.uint32)
        | (cblk[..., 5].astype(np.uint32) << 8)
        | (cblk[..., 6].astype(np.uint32) << 16)
        | (cblk[..., 7].astype(np.uint32) << 24)
    )
    pal = np.zeros((bh, bw, 4, 4), dtype=np.uint8)
    pal[..., 0, 0] = ((c0 >> 11) & 31) * 255 // 31
    pal[..., 0, 1] = ((c0 >> 5) & 63) * 255 // 63
    pal[..., 0, 2] = (c0 & 31) * 255 // 31
    pal[..., 0, 3] = 255
    pal[..., 1, 0] = ((c1 >> 11) & 31) * 255 // 31
    pal[..., 1, 1] = ((c1 >> 5) & 63) * 255 // 63
    pal[..., 1, 2] = (c1 & 31) * 255 // 31
    pal[..., 1, 3] = 255
    four = (not is_bc1) | (c0 > c1)
    for k in range(3):
        p0 = pal[..., 0, k].astype(np.uint16)
        p1 = pal[..., 1, k].astype(np.uint16)
        pal[..., 2, k] = np.where(four, (2 * p0 + p1) // 3, (p0 + p1) // 2).astype(np.uint8)
        pal[..., 3, k] = np.where(four, (p0 + 2 * p1) // 3, 0).astype(np.uint8)
    pal[..., 2, 3] = 255
    pal[..., 3, 3] = np.where(four, 255, 0).astype(np.uint8)

    cidx = ((cbits[..., None] >> np.arange(0, 32, 2, dtype=np.uint32)) & 3).astype(np.intp)
    by = np.arange(bh)[:, None, None]
    bx = np.arange(bw)[None, :, None]
    pix = pal[by, bx, cidx].reshape(bh, bw, 4, 4, 4)

    if is_bc2:
        a16 = blocks[..., 0:8:2].astype(np.uint16) | (blocks[..., 1:8:2].astype(np.uint16) << 8)
        alpha = np.stack([((a16 >> (4 * px)) & 0xF) * 17 for px in range(4)], axis=-1).astype(np.uint8)
        pix[..., 3] = alpha
    elif not is_bc1:
        a0 = blocks[..., 0].astype(np.uint16)
        a1 = blocks[..., 1].astype(np.uint16)
        apal = np.zeros((bh, bw, 8), dtype=np.uint8)
        apal[..., 0] = a0
        apal[..., 1] = a1
        gt = a0 > a1
        for k in range(1, 7):
            v_gt = ((7 - k) * a0 + k * a1) // 7
            if k <= 4:
                v_le = ((5 - k) * a0 + k * a1) // 5
                apal[..., k + 1] = np.where(gt, v_gt, v_le).astype(np.uint8)
            else:
                apal[..., k + 1] = np.where(gt, v_gt, 0 if k == 5 else 255).astype(np.uint8)
        abits = np.zeros((bh, bw), dtype=np.uint64)
        for k in range(6):
            abits |= blocks[..., 2 + k].astype(np.uint64) << np.uint64(8 * k)
        aidx = ((abits[..., None] >> np.arange(0, 48, 3, dtype=np.uint64)) & np.uint64(7)).astype(np.intp)
        pix[..., 3] = apal[by, bx, aidx].reshape(bh, bw, 4, 4)

    rgba = pix.transpose(0, 2, 1, 3, 4).reshape(bh * 4, bw * 4, 4)[:h, :w]
    return Image.fromarray(rgba, "RGBA")


def parse_par_gtfs(dec: bytes) -> dict[str, tuple[int, int, int, bytes]]:
    if len(dec) < 0x30 or dec[0x20:0x24] != b"PAR\x02":
        return {}
    nfiles = struct.unpack_from(">I", dec, 0x28)[0]
    offs = [struct.unpack_from(">I", dec, 0x30 + i * 4)[0] for i in range(nfiles)]
    name_base = (0x30 + nfiles * 4 + 15) & ~15
    gtfs: dict[str, tuple[int, int, int, bytes]] = {}
    for i in range(nfiles):
        nm_off = name_base + i * 32
        nm_end = dec.find(b"\x00", nm_off)
        nm = dec[nm_off:nm_end].decode("ascii", errors="replace")
        if not nm.lower().endswith(".gtf"):
            continue
        gtf_off = 0x20 + offs[i]
        if gtf_off + 48 > len(dec) or dec[gtf_off:gtf_off + 4] != b"\x01\x04\x00\x00":
            continue
        _ver, _sz, ntex, _tid, toff, _tsz, fmt, _mip, _dim, _cube, _remap, w, h, _d, _loc, _pitch = (
            struct.unpack_from(">IIIIIIBBBBIHHHBxI", dec, gtf_off)
        )
        if ntex != 1:
            continue
        basef = fmt & 0x9F
        if basef == 0x85:
            l0_sz = w * h * 4
        elif basef in (0x86, 0x87, 0x88):
            bsz = 8 if basef == 0x86 else 16
            l0_sz = ((w + 3) // 4) * ((h + 3) // 4) * bsz
        else:
            continue
        pix_off = gtf_off + toff
        raw_l0 = dec[pix_off:pix_off + l0_sz]
        if len(raw_l0) == l0_sz:
            gtfs[nm] = (w, h, basef, raw_l0)
    return gtfs


def extract_font_atlas_from_disc(game_dir: Path, entries: list[PaaEntry]) -> tuple[str, Image.Image]:
    arc_path = game_dir / "D00.ARC"
    entry = next((e for e in entries if e.name == "font/font.bin"), None)
    if entry is None:
        raise KeyError("font/font.bin entry not found in D00.BIN")

    with arc_path.open("rb") as f:
        f.seek(entry.arc_offset)
        blob = f.read(entry.arc_size)
    if blob[:4] == b"segs":
        blob = decompress_segs(blob)

    gtf_off = 0x7183E0
    if len(blob) < gtf_off + 0x80 or blob[gtf_off:gtf_off + 4] != b"\x01\x04\x00\x00":
        probe = blob.find(b"\x01\x04\x00\x00\x00\x04\x00\x00\x00\x00\x00\x01")
        if probe < 0:
            raise ValueError("Could not locate 256x256 GTF header inside font/font.bin")
        gtf_off = probe

    tex_off, _tex_sz, fmt, _mip, _dim, _cube, _remap, w, h, _d, _loc, pitch = (
        struct.unpack_from(">IIBBBBIHHHBxI", blob, gtf_off + 16)
    )
    pix_start = gtf_off + tex_off
    base_format = fmt & 0x9F
    if w != 256 or h != 256 or base_format != 0x85 or pitch != 1024:
        raise ValueError(
            f"Unexpected font/font.bin GTF format: {w}x{h} fmt=0x{fmt:02x} pitch={pitch}"
        )

    argb = blob[pix_start:pix_start + w * h * 4]
    tex_hash = f"{fnv1a64_tex(argb, w, h, base_format):016x}"
    if tex_hash != FONT_ATLAS_HASH:
        raise ValueError(
            f"Unexpected font/font.bin hash {tex_hash}; expected {FONT_ATLAS_HASH}"
        )
    return tex_hash, decode_rsx_level0(argb, w, h, base_format)


def load_bold_font(size_px: int) -> ImageFont.FreeTypeFont | ImageFont.ImageFont:
    candidates = [
        Path("C:/Windows/Fonts/segoeuib.ttf"),
        Path("C:/Windows/Fonts/arialbd.ttf"),
        Path("C:/Windows/Fonts/calibrib.ttf"),
        Path("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf"),
    ]
    for path in candidates:
        if path.is_file():
            return ImageFont.truetype(str(path), size=size_px)
    return ImageFont.load_default()


def draw_centered_text(
    draw: ImageDraw.ImageDraw,
    cx: float,
    cy: float,
    text: str,
    fill: tuple[int, int, int, int],
    max_w: float,
    max_h: float,
    outline: tuple[int, int, int, int] = (14, 16, 20, 255),
    stroke_w: int = 3,
) -> None:
    size = max(12, int(max_h))
    font = load_bold_font(size)
    while size > 12:
        font = load_bold_font(size)
        bbox = draw.textbbox((0, 0), text, font=font, stroke_width=stroke_w)
        tw = bbox[2] - bbox[0]
        th = bbox[3] - bbox[1]
        if tw <= max_w and th <= max_h:
            break
        size -= 2

    bbox = draw.textbbox((0, 0), text, font=font, stroke_width=stroke_w)
    tx = cx - (bbox[0] + bbox[2]) * 0.5
    ty = cy - (bbox[1] + bbox[3]) * 0.5
    draw.text(
        (tx, ty),
        text,
        font=font,
        fill=fill,
        stroke_width=stroke_w,
        stroke_fill=outline,
    )


def apply_red_controller_glow(
    canvas: Image.Image,
    body_pts: list[tuple[float, float]],
    sw: int,
    sh: int,
    bw: float,
    bh: float,
) -> None:
    glow = Image.new("RGBA", (sw, sh), (0, 0, 0, 0))
    gd = ImageDraw.Draw(glow)
    gw = max(24, int(min(bw, bh) * 0.16))
    gd.polygon(body_pts, fill=(240, 20, 16, 255))
    gd.line(body_pts + [body_pts[0]], fill=(255, 28, 20, 255), width=gw, joint="curve")
    glow = glow.filter(ImageFilter.GaussianBlur(radius=max(8.0, min(sw, sh) * 0.048)))
    g_arr = np.array(glow)
    g_arr[..., 3] = np.clip(g_arr[..., 3].astype(np.int32) * 2, 0, 255).astype(np.uint8)
    canvas.alpha_composite(Image.fromarray(g_arr, "RGBA"))


def render_controller(style: str, w: int, h: int, red_glow: bool = False) -> Image.Image:
    ss = 4
    sw, sh = w * ss, h * ss
    canvas = Image.new("RGBA", (sw, sh), (0, 0, 0, 0))
    margin = 0.08 if red_glow else 0.03
    bx0 = sw * margin
    by0 = sh * (margin + 0.02)
    bw = sw * (1.0 - 2.0 * margin)
    bh = sh * (1.0 - 2.0 * margin - 0.02)

    def pt(nx: float, ny: float) -> tuple[float, float]:
        return (bx0 + nx * bw, by0 + ny * bh)

    def ellipse_at(d: ImageDraw.ImageDraw, nx: float, ny: float, r_frac: float, **kw) -> None:
        r = r_frac * min(bw, bh)
        cx, cy = pt(nx, ny)
        d.ellipse((cx - r, cy - r, cx + r, cy + r), **kw)

    shell_outline_w = max(3, int(min(bw, bh) * 0.016))
    inner_w = max(2, int(min(bw, bh) * 0.010))

    if style == "xbox":
        body_norm = [
            (0.24, 0.14), (0.38, 0.12), (0.50, 0.13), (0.62, 0.12), (0.76, 0.14),
            (0.86, 0.22), (0.92, 0.38), (0.95, 0.60), (0.94, 0.79), (0.89, 0.92),
            (0.82, 0.96), (0.74, 0.92), (0.66, 0.76), (0.58, 0.69), (0.50, 0.68),
            (0.42, 0.69), (0.34, 0.76), (0.26, 0.92), (0.18, 0.96), (0.11, 0.92),
            (0.06, 0.79), (0.05, 0.60), (0.08, 0.38), (0.14, 0.22),
        ]
    else:
        # Nintendo Switch Pro Controller: wider flat shoulder bridge & angled grips.
        body_norm = [
            (0.21, 0.15), (0.37, 0.14), (0.50, 0.14), (0.63, 0.14), (0.79, 0.15),
            (0.87, 0.23), (0.92, 0.42), (0.94, 0.68), (0.92, 0.87), (0.86, 0.96),
            (0.77, 0.94), (0.71, 0.77), (0.64, 0.71), (0.50, 0.71), (0.36, 0.71),
            (0.29, 0.77), (0.23, 0.94), (0.14, 0.96), (0.08, 0.87), (0.06, 0.68),
            (0.08, 0.42), (0.13, 0.23),
        ]

    body_pts = [pt(x, y) for x, y in body_norm]
    if red_glow:
        apply_red_controller_glow(canvas, body_pts, sw, sh, bw, bh)

    draw = ImageDraw.Draw(canvas)

    # Rear ZL/ZR or LT/RT nubs and L/R or LB/RB shoulder bumpers.
    for lx0, rx0 in ((0.20, 0.36), (0.64, 0.80)):
        p0, p1 = pt(lx0, 0.02), pt(rx0, 0.15)
        draw.rounded_rectangle(
            (p0[0], p0[1], p1[0], p1[1]), radius=max(6, int(bw * 0.04)),
            fill=(28, 30, 34, 255), outline=(145, 150, 160, 255), width=inner_w,
        )
    for lx0, rx0 in ((0.16, 0.39), (0.61, 0.84)):
        p0, p1 = pt(lx0, 0.09), pt(rx0, 0.21)
        draw.rounded_rectangle(
            (p0[0], p0[1], p1[0], p1[1]), radius=max(6, int(bw * 0.04)),
            fill=(44, 46, 52, 255), outline=(175, 180, 190, 255), width=inner_w,
        )

    # Main controller shell.
    body_fill = (32, 34, 38, 255) if style == "xbox" else (38, 40, 46, 255)
    draw.polygon(body_pts, fill=body_fill)
    if style == "nintendo":
        # Pro Controller matte darker left/right grips.
        left_grip = [pt(x, y) for x, y in ((0.14, 0.23), (0.22, 0.17), (0.28, 0.76), (0.22, 0.94), (0.14, 0.96), (0.08, 0.87), (0.06, 0.68), (0.08, 0.42))]
        right_grip = [pt(x, y) for x, y in ((0.86, 0.23), (0.78, 0.17), (0.72, 0.76), (0.78, 0.94), (0.86, 0.96), (0.92, 0.87), (0.94, 0.68), (0.92, 0.42))]
        draw.polygon(left_grip, fill=(24, 26, 30, 255))
        draw.polygon(right_grip, fill=(24, 26, 30, 255))
        draw.line([pt(0.22, 0.17), pt(0.28, 0.76)], fill=(96, 102, 114, 220), width=inner_w)
        draw.line([pt(0.78, 0.17), pt(0.72, 0.76)], fill=(96, 102, 114, 220), width=inner_w)
    else:
        left_trim = [pt(x, y) for x, y in ((0.18, 0.24), (0.14, 0.44), (0.18, 0.72), (0.24, 0.90))]
        right_trim = [pt(x, y) for x, y in ((0.82, 0.24), (0.86, 0.44), (0.82, 0.72), (0.76, 0.90))]
        draw.line(left_trim, fill=(92, 96, 106, 200), width=inner_w, joint="curve")
        draw.line(right_trim, fill=(92, 96, 106, 200), width=inner_w, joint="curve")

    draw.line(body_pts + [body_pts[0]], fill=(192, 196, 206, 255), width=shell_outline_w, joint="curve")

    # Top center port / receiver bezel.
    p0, p1 = pt(0.42, 0.14), pt(0.58, 0.23)
    draw.rounded_rectangle(
        (p0[0], p0[1], p1[0], p1[1]), radius=max(4, int(bw * 0.02)),
        fill=(22, 24, 28, 255), outline=(120, 125, 134, 255), width=max(1, inner_w - 1),
    )

    # Asymmetric Left Stick (upper left) and Right Stick (lower right).
    for sx, sy in ((0.270, 0.39), (0.612, 0.60)):
        ellipse_at(draw, sx, sy, 0.128, fill=(22, 23, 26, 255), outline=(140, 145, 155, 255), width=inner_w)
        ellipse_at(draw, sx, sy, 0.094, fill=(58, 60, 66, 255), outline=(198, 202, 212, 255), width=inner_w)
        ellipse_at(draw, sx, sy, 0.058, fill=(42, 44, 48, 255))

    # Lower-left D-pad: saucer disc on Xbox 360, classic + cross on Switch Pro.
    dx, dy = pt(0.382, 0.60)
    dr = 0.110 * min(bw, bh)
    if style == "xbox":
        draw.ellipse(
            (dx - dr, dy - dr, dx + dr, dy + dr),
            fill=(24, 26, 30, 255), outline=(145, 150, 160, 255), width=inner_w,
        )
        arm_w = dr * 0.34
        arm_l = dr * 0.76
    else:
        arm_w = dr * 0.36
        arm_l = dr * 0.92
    draw.rounded_rectangle(
        (dx - arm_l, dy - arm_w, dx + arm_l, dy + arm_w),
        radius=max(2, int(arm_w * 0.28)), fill=(56, 58, 66, 255), outline=(185, 190, 200, 255), width=max(1, inner_w - 1),
    )
    draw.rounded_rectangle(
        (dx - arm_w, dy - arm_l, dx + arm_w, dy + arm_l),
        radius=max(2, int(arm_w * 0.28)), fill=(56, 58, 66, 255), outline=(185, 190, 200, 255), width=max(1, inner_w - 1),
    )
    if style == "nintendo":
        # Fill center intersection cleanly so the cross reads as one solid piece.
        draw.rectangle(
            (dx - arm_w + inner_w, dy - arm_w + inner_w, dx + arm_w - inner_w, dy + arm_w - inner_w),
            fill=(56, 58, 66, 255),
        )

    if style == "xbox":
        gx, gy = pt(0.495, 0.365)
        gr = 0.084 * min(bw, bh)
        draw.ellipse(
            (gx - gr, gy - gr, gx + gr, gy + gr),
            fill=(180, 186, 196, 255), outline=(232, 236, 242, 255), width=max(2, inner_w),
        )
        gr2 = gr * 0.72
        draw.ellipse((gx - gr2, gy - gr2, gx + gr2, gy + gr2), fill=(30, 42, 30, 255))
        xw = max(2, int(gr * 0.24))
        draw.line((gx - gr2 * 0.6, gy - gr2 * 0.6, gx + gr2 * 0.6, gy + gr2 * 0.6), fill=(116, 224, 64, 255), width=xw)
        draw.line((gx - gr2 * 0.6, gy + gr2 * 0.6, gx + gr2 * 0.6, gy - gr2 * 0.6), fill=(116, 224, 64, 255), width=xw)
        for px in (0.402, 0.586):
            ellipse_at(draw, px, 0.375, 0.026, fill=(48, 50, 56, 255), outline=(185, 190, 200, 255), width=max(1, inner_w - 1))

        diamond = [
            (0.742, 0.285, "Y", (222, 154, 16, 255), (255, 236, 178, 255)),
            (0.812, 0.388, "B", (208, 36, 42, 255), (255, 204, 206, 255)),
            (0.742, 0.490, "A", (34, 158, 62, 255), (196, 248, 206, 255)),
            (0.672, 0.388, "X", (36, 108, 214, 255), (198, 224, 255, 255)),
        ]
    else:
        # Switch Pro center cluster: - / +, Capture (square), Home (ring), and 4 bottom LEDs.
        ellipse_at(draw, 0.415, 0.320, 0.027, fill=(52, 54, 60, 255), outline=(196, 200, 210, 255), width=max(1, inner_w - 1))
        ellipse_at(draw, 0.585, 0.320, 0.027, fill=(52, 54, 60, 255), outline=(196, 200, 210, 255), width=max(1, inner_w - 1))
        cx0, cy0 = pt(0.435, 0.440)
        cr = 0.025 * min(bw, bh)
        draw.rounded_rectangle(
            (cx0 - cr, cy0 - cr, cx0 + cr, cy0 + cr), radius=max(2, int(cr * 0.25)),
            fill=(48, 50, 56, 255), outline=(185, 190, 200, 255), width=max(1, inner_w - 1),
        )
        ellipse_at(draw, 0.565, 0.440, 0.030, fill=(48, 52, 58, 255), outline=(190, 216, 232, 255), width=max(1, inner_w - 1))
        for idx, lx in enumerate((0.462, 0.487, 0.513, 0.538)):
            col = (84, 228, 96, 255) if idx == 0 else (68, 72, 80, 255)
            ellipse_at(draw, lx, 0.665, 0.011, fill=col)

        diamond = [
            (0.742, 0.285, "X", SWITCH_FILL, SWITCH_RIM),
            (0.812, 0.388, "A", SWITCH_FILL, SWITCH_RIM),
            (0.742, 0.490, "B", SWITCH_FILL, SWITCH_RIM),
            (0.672, 0.388, "Y", SWITCH_FILL, SWITCH_RIM),
        ]

    br = 0.052 * min(bw, bh)
    for ax, ay, lbl, fcol, rcol in diamond:
        cx, cy = pt(ax, ay)
        draw.ellipse((cx - br - 2, cy - br - 2, cx + br + 2, cy + br + 2), fill=(16, 18, 22, 255))
        draw.ellipse((cx - br, cy - br, cx + br, cy + br), fill=fcol, outline=rcol, width=max(2, inner_w - 1))
        if w >= 120:
            draw_centered_text(
                draw, cx, cy, lbl, WHITE,
                br * 1.35, br * 1.35, outline=(14, 16, 20, 220), stroke_w=max(2, int(br * 0.22)),
            )

    return canvas.resize((w, h), Image.Resampling.LANCZOS)


def render_sprite(w: int, h: int, kind: tuple) -> Image.Image:
    shape = kind[0]
    if shape == "controller":
        return render_controller(kind[1], w, h, red_glow=bool(kind[2]))

    ss = 4
    sw, sh = w * ss, h * ss
    canvas = Image.new("RGBA", (sw, sh), (0, 0, 0, 0))
    draw = ImageDraw.Draw(canvas)
    unit = min(sw, sh)
    rim_w = max(3, int(round(unit * 0.045)))
    outer_pad = max(2, int(round(unit * 0.025)))
    txt_stroke = max(2, int(round(unit * 0.038)))

    if shape == "circle":
        _, label, fill_col, rim_col, txt_col = kind
        d = min(sw, sh) - 2 * outer_pad
        x0 = (sw - d) * 0.5
        y0 = (sh - d) * 0.5
        x1 = x0 + d
        y1 = y0 + d
        draw.ellipse((x0, y0, x1, y1), fill=(16, 18, 22, 255))
        draw.ellipse(
            (x0 + rim_w, y0 + rim_w, x1 - rim_w, y1 - rim_w),
            fill=fill_col,
            outline=rim_col,
            width=rim_w,
        )
        draw_centered_text(
            draw,
            sw * 0.5,
            sh * 0.5,
            label,
            txt_col,
            d * 0.58,
            d * 0.62,
            outline=(18, 20, 24, 240),
            stroke_w=txt_stroke,
        )
    elif shape == "pill":
        _, label, fill_col, rim_col, txt_col = kind
        py0 = max(4, int(sh * 0.08))
        py1 = min(sh - 5, int(sh * 0.92))
        px0, px1 = 3, sw - 4
        rad = min(sw, py1 - py0) // 2 - 2
        draw.rounded_rectangle((px0, py0, px1, py1), radius=rad, fill=(14, 16, 20, 255))
        draw.rounded_rectangle(
            (px0 + rim_w, py0 + rim_w, px1 - rim_w, py1 - rim_w),
            radius=max(4, rad - rim_w),
            fill=fill_col,
            outline=rim_col,
            width=rim_w,
        )
        draw_centered_text(
            draw,
            sw * 0.5,
            (py0 + py1) * 0.5,
            label,
            txt_col,
            (px1 - px0) * 0.78,
            (py1 - py0) * 0.58,
            outline=(14, 16, 20, 255),
            stroke_w=max(2, txt_stroke - 1),
        )
    elif shape == "switch_symbol":
        _, sym, fill_col, rim_col, txt_col = kind
        d = min(sw, sh) - 2 * outer_pad
        cx, cy = sw * 0.5, sh * 0.5
        bx0 = cx - d * 0.5
        by0 = cy - d * 0.5
        bx1 = cx + d * 0.5
        by1 = cy + d * 0.5
        draw.ellipse((bx0, by0, bx1, by1), fill=(14, 16, 20, 255))
        draw.ellipse(
            (bx0 + rim_w, by0 + rim_w, bx1 - rim_w, by1 - rim_w),
            fill=fill_col,
            outline=rim_col,
            width=rim_w,
        )
        bar_w = d * 0.46
        thick = max(6, int(round(d * 0.14)))
        rad = max(2, thick // 3)
        draw.rounded_rectangle(
            (cx - bar_w * 0.5, cy - thick * 0.5, cx + bar_w * 0.5, cy + thick * 0.5),
            radius=rad,
            fill=txt_col,
        )
        if sym == "+":
            draw.rounded_rectangle(
                (cx - thick * 0.5, cy - bar_w * 0.5, cx + thick * 0.5, cy + bar_w * 0.5),
                radius=rad,
                fill=txt_col,
            )
    elif shape == "bumper":
        _, label = kind
        rad = max(12, int(unit * 0.24))
        draw.rounded_rectangle((2, 2, sw - 3, sh - 3), radius=rad, fill=(14, 16, 20, 255))
        draw.rounded_rectangle(
            (2 + rim_w, 2 + rim_w, sw - 3 - rim_w, sh - 3 - rim_w),
            radius=max(6, rad - rim_w),
            fill=(42, 44, 50, 255),
            outline=(212, 218, 226, 255),
            width=rim_w,
        )
        draw_centered_text(
            draw,
            sw * 0.5,
            sh * 0.5,
            label,
            WHITE,
            sw * 0.72,
            sh * 0.68,
            outline=(14, 16, 20, 255),
            stroke_w=max(2, txt_stroke - 1),
        )
    elif shape == "trigger":
        _, label = kind
        rad = max(16, int(unit * 0.28))
        draw.rounded_rectangle((2, 2, sw - 3, sh - 3), radius=rad, fill=(14, 16, 20, 255))
        draw.rounded_rectangle(
            (2 + rim_w, 2 + rim_w, sw - 3 - rim_w, sh - 3 - rim_w),
            radius=max(8, rad - rim_w),
            fill=(38, 40, 46, 255),
            outline=(212, 218, 226, 255),
            width=rim_w,
        )
        draw_centered_text(
            draw,
            sw * 0.5,
            sh * 0.5,
            label,
            WHITE,
            sw * 0.72,
            sh * 0.64,
            outline=(14, 16, 20, 255),
            stroke_w=max(2, txt_stroke - 1),
        )
    elif shape == "stick_label":
        _, label = kind
        # Upper arc of analog stick cap whose equator meets y=82 cleanly (width=25px).
        draw.ellipse((4, 16, sw - 4, sh * 2 - 16), fill=(65, 65, 65, 255), outline=(42, 42, 42, 255), width=4)
        draw_centered_text(
            draw,
            sw * 0.5,
            sh * 0.50,
            label,
            WHITE,
            sw * 0.74,
            sh * 0.84,
            outline=(14, 16, 20, 255),
            stroke_w=5,
        )
    else:
        raise ValueError(f"Unknown sprite kind: {shape}")

    return canvas.resize((w, h), Image.Resampling.LANCZOS)


def apply_patch_slots(base_atlas: Image.Image, slots: list[PatchSlot], style: str) -> Image.Image:
    out = base_atlas.copy()
    specs = STYLE_SPECS[style]
    for item in slots:
        x0, y0, x1, y1 = item.box
        w, h = x1 - x0, y1 - y0
        sprite = render_sprite(w, h, specs[item.slot])
        if item.clear_mode == "circle":
            # Clear only inside a centered disk so adjacent glow / card backing stays intact.
            mask = Image.new("L", (w, h), 255)
            md = ImageDraw.Draw(mask)
            d = min(w, h)
            cx0 = (w - d) // 2
            cy0 = (h - d) // 2
            md.ellipse((cx0, cy0, cx0 + d - 1, cy0 + d - 1), fill=0)
            region = out.crop(item.box)
            region.putalpha(Image.fromarray(
                np.minimum(np.array(region.getchannel("A")), np.array(mask)), mode="L"
            ))
            out.paste(region, (x0, y0))
        else:
            out.paste((0, 0, 0, 0), item.box)
        out.paste(sprite, (x0, y0), sprite)
    return out


def selected_styles(styles_arg: str) -> tuple[str, ...]:
    if styles_arg == "all":
        return ("xbox", "nintendo")
    return (styles_arg,)


def build_from_disc(game_dir: Path, out_dir: Path, styles: tuple[str, ...]) -> int:
    bin_path = game_dir / "D00.BIN"
    arc_path = game_dir / "D00.ARC"
    if not bin_path.is_file() or not arc_path.is_file():
        raise FileNotFoundError(f"Missing D00.BIN / D00.ARC under {game_dir}")

    entries = read_paa_entries(bin_path)
    written_keys: set[tuple[str, str]] = set()
    written_count = 0

    def emit(style: str, tex_hash: str, img: Image.Image, label: str) -> None:
        nonlocal written_count
        key = (style, tex_hash)
        if key in written_keys:
            return
        written_keys.add(key)
        dst_dir = out_dir / style
        dst_dir.mkdir(parents=True, exist_ok=True)
        dst_path = dst_dir / f"{tex_hash}.png"
        img.save(dst_path, format="PNG")
        written_count += 1
        print(f"Generated {dst_path} ({label})")

    font_hash, font_img = extract_font_atlas_from_disc(game_dir, entries)
    for style in styles:
        emit(style, font_hash, apply_patch_slots(font_img, FONT_SLOTS, style), f"font/font.bin {font_hash}")

    spani_entries = [e for e in entries if e.name.rsplit("/", 1)[0] in TARGET_SPANI_FOLDERS]
    with arc_path.open("rb") as arc_file:
        for entry in spani_entries:
            folder, archive_base = entry.name.rsplit("/", 1)
            arc_file.seek(entry.arc_offset)
            dec = decompress_segs(arc_file.read(entry.arc_size))
            gtfs = parse_par_gtfs(dec)
            for gtf_name, (w, h, basef, raw_l0) in gtfs.items():
                slots = slots_for_spani_gtf(folder, archive_base, gtf_name)
                if not slots:
                    continue
                tex_hash = f"{fnv1a64_tex(raw_l0, w, h, basef):016x}"
                needed = [s for s in styles if (s, tex_hash) not in written_keys]
                if not needed:
                    continue
                base_img = decode_rsx_level0(raw_l0, w, h, basef)
                for style in needed:
                    emit(style, tex_hash, apply_patch_slots(base_img, slots, style), f"{entry.name}/{gtf_name}")

    return written_count


def process_dump_dir(dump_dir: Path, out_dir: Path, styles: tuple[str, ...]) -> int:
    if not dump_dir.is_dir():
        raise FileNotFoundError(f"Dump directory not found: {dump_dir}")
    pattern = re.compile(r"^([0-9a-fA-F]{16})_(\d+)x(\d+)_fmt([0-9a-fA-F]{2})\.png$")
    discovered: list[tuple[str, int, int, str, Path]] = []
    for entry in sorted(dump_dir.iterdir()):
        match = pattern.match(entry.name)
        if match:
            h, w, hgt, fmt = match.group(1).lower(), int(match.group(2)), int(match.group(3)), match.group(4).lower()
            discovered.append((h, w, hgt, fmt, entry))

    print(f"Found {len(discovered)} dumped UI texture(s) in {dump_dir}:")
    generated = 0
    for h, w, hgt, fmt, path in discovered:
        tag = ""
        slots: list[PatchSlot] | None = None
        if h == FONT_ATLAS_HASH:
            tag = " [MAIN BUTTON ATLAS]"
            slots = FONT_SLOTS
        elif h in KNOWN_HASH_SLOTS:
            tag = f" [{KNOWN_HASH_SLOTS[h][0]}]"
            slots = KNOWN_HASH_SLOTS[h][1]
        print(f"  {h}  {w}x{hgt}  fmt=0x{fmt}  ({path.name}){tag}")

        if slots is not None:
            base = Image.open(path).convert("RGBA")
            for style in styles:
                dst_dir = out_dir / style
                dst_dir.mkdir(parents=True, exist_ok=True)
                dst_path = dst_dir / f"{h}.png"
                apply_patch_slots(base, slots, style).save(dst_path, format="PNG")
                print(f"    -> generated {dst_path}")
                generated += 1

    if generated == 0:
        print("Note: no known button-prompt hashes matched in dump directory.")
    return generated


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    repo_root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument(
        "--from-disc",
        action="store_true",
        help="Extract UI atlases from game/PS3_GAME/USRDIR/D00.BIN+D00.ARC (default)",
    )
    mode.add_argument(
        "--from-dump",
        type=Path,
        default=None,
        help="Scan a BAKUGAN_DUMP_UI_TEX=1 dump directory (e.g. diagnostics/ui_tex_dump)",
    )
    parser.add_argument(
        "--styles",
        choices=("xbox", "nintendo", "all"),
        default="all",
        help="Button prompt styles to generate (default: all)",
    )
    parser.add_argument(
        "--game-dir",
        type=Path,
        default=repo_root / "game" / "PS3_GAME" / "USRDIR",
        help="Path to PS3_GAME/USRDIR containing D00.BIN and D00.ARC",
    )
    parser.add_argument(
        "--out-dir",
        type=Path,
        default=repo_root / "bakugan-recomp" / "assets" / "button_prompts",
        help="Output root for whitelisted PNG atlases",
    )
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    styles = selected_styles(args.styles)
    if args.from_dump is not None:
        process_dump_dir(args.from_dump, args.out_dir, styles)
        return 0

    count = build_from_disc(args.game_dir, args.out_dir, styles)
    print(f"Total generated atlas files: {count}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
