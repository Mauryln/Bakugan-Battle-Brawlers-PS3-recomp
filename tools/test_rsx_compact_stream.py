"""Compact GPU attributes must preserve the canonical stream and rasterization."""
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "ps3recomp-src/tools"))
from msvc_env import environ

out = ROOT / "diagnostics/arena/compact-stream"
out.mkdir(parents=True, exist_ok=True)
source = (ROOT / "ps3recomp-src/libs/video/rsx_d3d12_backend.c").read_text(encoding="utf-8")
parts = []
for name in ["static u32 vp_vertex_stride(",
             "static void vp_pack_vertex_stream(", "static u32 vp_compact_input_layout("]:
    assert name in source, "GPU still uploads all 16 vertex slots"
    start = source.index(name)
    parts.append(source[start:source.index("\n}", start) + 2])
(out / "compact_stream.inc").write_text("\n".join(parts), encoding="utf-8")
cc = next(Path("C:/Program Files (x86)/Microsoft Visual Studio").glob(
    "*/BuildTools/VC/Tools/Llvm/x64/bin/clang-cl.exe"))
subprocess.run([str(cc), "/nologo", "/std:c++20", "/MD", "/O2", "/EHsc",
    "/I" + str(out), "/I" + str(ROOT / "ps3recomp-src/include"),
    "/I" + str(ROOT / "ps3recomp-src/libs/video"),
    str(ROOT / "tools/tests/rsx_compact_stream_test.cpp"),
    str(ROOT / "ps3recomp-src/libs/video/rsx_vp_decompiler.c"),
    "/Fe:compact_stream_test.exe", "/link", "d3d11.lib", "d3dcompiler.lib"],
    cwd=out, env=environ(), check=True)
subprocess.run([str(out / "compact_stream_test.exe")], check=True, timeout=30)
