"""Build the executable icon from the game's own save icon (user-supplied dump).

The source is `game_icon.png`, the ICON0.PNG stored in the player's save. It is
320x176; the centre square keeps the Bakugan logo and characters. Pillow writes
the multi-size ICO directly, so no external tool is needed.
"""
from PIL import Image
from pathlib import Path

HERE = Path(__file__).parent
SRC = HERE / "game_icon.png"
OUT = HERE / "bakugan.ico"

img = Image.open(SRC).convert("RGBA")
w, h = img.size
side = min(w, h)
left = (w - side) // 2
top = (h - side) // 2
img = img.crop((left, top, left + side, top + side))
img = img.resize((1024, 1024), Image.LANCZOS)
img.save(OUT, format="ICO", sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
print(OUT)
