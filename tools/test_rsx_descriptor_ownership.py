"""Ensure cache uploads cannot overwrite already prepared per-draw SRVs."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parent.parent
SOURCE = (ROOT / "ps3recomp-src/libs/video/rsx_d3d12_backend.c").read_text(encoding="utf-8")

class DescriptorOwnershipTest(unittest.TestCase):
    def test_texture_cache_does_not_clobber_draw_descriptors(self):
        start = SOURCE.index("static int vp_upload_tex_slot(")
        end = SOURCE.index("\n}", start) + 2
        uploader = SOURCE[start:end]
        if "CreateShaderResourceView" in uploader:
            slots = int(re.search(r"#define VP_TEX_SLOTS\s+(\d+)", SOURCE)[1])
            draw_base = int(re.search(r"#define DRAW_SRV_BASE\s+(\d+)", SOURCE)[1])
            self.assertLessEqual(1 + slots, draw_base,
                "legacy cache descriptors overwrite per-draw bindings after texture slot 30")
        self.assertIn("srv_write_ex(wslot, s_d3d.vp_tex[ts].res", SOURCE)

if __name__ == "__main__":
    unittest.main()
