#!/usr/bin/env python3
"""analyze_rt.py -- numeric structure stats for captured render targets.

No image viewing: report what an image *contains* (colour spread, edge
density, non-black fraction) so a garbled/blank RT is distinguishable from a
coherent scene render without looking at it.

Usage: analyze_rt.py <png> [<png> ...]
"""
import sys
from PIL import Image, ImageFilter, ImageStat

def stats(path):
    im = Image.open(path).convert("RGB")
    w, h = im.size
    stat = ImageStat.Stat(im)
    mean = tuple(round(m, 1) for m in stat.mean)
    std = tuple(round(s, 1) for s in stat.stddev)
    # non-black: pixels with any channel >= 16
    nblack = sum(1 for p in im.getdata() if max(p) < 16)
    frac_black = nblack / (w * h)
    # distinct colours on a coarse grid (5 bits/channel)
    small = im.resize((w // 4, h // 4))
    colors = {tuple((c >> 5) << 5 for c in p) for p in small.getdata()}
    # edge density
    edges = ImageStat.Stat(im.filter(ImageFilter.FIND_EDGES))
    edge_mean = round(sum(edges.mean) / 3, 1)
    # brightest / darkest
    ext = im.getextrema()
    print(f"{path}: {w}x{h} mean={mean} std={std} black={frac_black:.1%} "
          f"colors5bit={len(colors)} edge={edge_mean} extrema={ext}")

for p in sys.argv[1:]:
    try:
        stats(p)
    except Exception as e:
        print(f"{p}: ERROR {e}")
