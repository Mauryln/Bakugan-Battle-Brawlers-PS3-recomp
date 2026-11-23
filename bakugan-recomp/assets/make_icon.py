"""Build the executable icon from the game's own save icon (user-supplied dump).

The source is `game_icon.png`, the ICON0.PNG stored in the player's save. It is
320x176; the centre square keeps the Bakugan logo and characters.

Entries are written as standard uncompressed 32-bit BGRA DIB bitmaps rather than
PNG-compressed layers so the `.rsrc` section stays compact (~34 KB) and low
entropy (avoids AV packer/obfuscation heuristics on small unsigned utilities).
"""
import struct
from pathlib import Path
from PIL import Image

HERE = Path(__file__).parent
SRC = HERE / "game_icon.png"
OUT = HERE / "bakugan.ico"
SIZES = (16, 24, 32, 48)


def encode_dib(rgba_img: Image.Image) -> bytes:
    w, h = rgba_img.size
    header = struct.pack(
        "<IIIHHIIIIII",
        40,      # biSize
        w,       # biWidth
        h * 2,   # biHeight (XOR + AND masks)
        1,       # biPlanes
        32,      # biBitCount
        0,       # biCompression = BI_RGB
        w * h * 4,
        0, 0, 0, 0,
    )
    pixels = rgba_img.tobytes("raw", "BGRA")
    row_stride = w * 4
    xor_rows = bytearray()
    for y in range(h - 1, -1, -1):
        xor_rows.extend(pixels[y * row_stride:(y + 1) * row_stride])
    and_stride = ((w + 31) // 32) * 4
    and_mask = bytes(and_stride * h)
    return header + bytes(xor_rows) + and_mask


img = Image.open(SRC).convert("RGBA")
w, h = img.size
side = min(w, h)
left = (w - side) // 2
top = (h - side) // 2
square = img.crop((left, top, left + side, top + side))

entries = []
for s in SIZES:
    resized = square.resize((s, s), Image.LANCZOS)
    entries.append((s, encode_dib(resized)))

header = struct.pack("<HHH", 0, 1, len(entries))
offset = 6 + 16 * len(entries)
directory = bytearray()
payload = bytearray()
for s, dib in entries:
    dim = 0 if s >= 256 else s
    directory.extend(struct.pack("<BBBBHHII", dim, dim, 0, 0, 1, 32, len(dib), offset))
    payload.extend(dib)
    offset += len(dib)

OUT.write_bytes(header + bytes(directory) + bytes(payload))
print(f"{OUT} ({OUT.stat().st_size} bytes)")
