"""Check upload bytes across mip levels and cube faces using D3D12 footprints."""
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "ps3recomp-src/tools"))
from msvc_env import environ

out = ROOT / "diagnostics/arena/texture-mips"
out.mkdir(parents=True, exist_ok=True)
source = (ROOT / "ps3recomp-src/libs/video/rsx_d3d12_backend.c").read_text(encoding="utf-8")
helper = "static void vp_decode_texture_subresources("
if helper in source:
    start = source.index(helper)
    body = source.index("{", start)
    depth, end = 1, body + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    extracted = source[start:end]
else:
    # The current uploader decodes one image per face, with MipLevels=1.
    start = source.index("    rsx_texture_decode(mapped, pitch, vm_base + off")
    end = source.index(";", start) + 1
    call = source[start:end].replace("rsx_texture_argb_is_rgba()", "argb_as_rgba")
    extracted = """static void vp_decode_texture_subresources(void* mapped,
    const u8* source, const rsx_tex_level* levels, u32 level_count, u32 faces,
    u32 face_stride, const D3D12_PLACED_SUBRESOURCE_FOOTPRINT* footprints,
    int argb_as_rgba)
{
    u8* original = (u8*)mapped;
    const u8* vm_base = source;
    for (u32 face = 0; face < faces; ++face) {
        u32 off = face * face_stride;
        u32 w = levels[0].w, h = levels[0].h;
        rsx_tex_layout tl = levels[0].tl;
        u32 pitch = footprints[face * level_count].Footprint.RowPitch;
        mapped = original + footprints[face * level_count].Offset;
""" + call + "\n    }\n}\n"
(out / "texture_mips.inc").write_text(extracted, encoding="utf-8")
cc = next(Path("C:/Program Files (x86)/Microsoft Visual Studio").glob(
    "*/BuildTools/VC/Tools/Llvm/x64/bin/clang-cl.exe"))
subprocess.run([str(cc), "/nologo", "/std:c++20", "/MD", "/O2", "/EHsc",
    "/I" + str(out), "/I" + str(ROOT / "ps3recomp-src/include"),
    "/I" + str(ROOT / "ps3recomp-src/libs/video"),
    str(ROOT / "tools/tests/rsx_texture_mips_test.cpp"),
    str(ROOT / "ps3recomp-src/libs/video/rsx_texture_layout.c"),
    "/Fe:texture_mips_test.exe", "/link", "d3d12.lib"],
    cwd=out, env=environ(), check=True)
subprocess.run([str(out / "texture_mips_test.exe")], check=True, timeout=15)
