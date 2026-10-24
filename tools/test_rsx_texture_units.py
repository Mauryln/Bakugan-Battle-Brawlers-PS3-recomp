"""Validate the actual backend root signature against D3D12, without a window."""
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "ps3recomp-src/tools"))
from msvc_env import environ

out = ROOT / "diagnostics/arena/texture-units"
out.mkdir(parents=True, exist_ok=True)
source = (ROOT / "ps3recomp-src/libs/video/rsx_d3d12_backend.c").read_text(encoding="utf-8")
start = source.index("D3D12_DESCRIPTOR_RANGE srv_range", source.index("RSX fragment texture SRV table"))
end = source.index("ID3DBlob* sig = NULL;", start)
(out / "texture_root_entry.inc").write_text(source[start:end].replace("{0}", "{}"))
cc = next(Path("C:/Program Files (x86)/Microsoft Visual Studio").glob(
    "*/BuildTools/VC/Tools/Llvm/x64/bin/clang-cl.exe"))
subprocess.run([str(cc), "/nologo", "/std:c++20", "/MD", "/O2", "/EHsc", "/I" + str(out),
    "/I" + str(ROOT / "ps3recomp-src/include"), "/I" + str(ROOT / "ps3recomp-src/libs/video"),
    str(ROOT / "tools/tests/rsx_texture_units_test.cpp"), "/Fe:rsx_texture_units_test.exe",
    "/link", "d3d12.lib", "d3dcompiler.lib", "dxgi.lib"], cwd=out, env=environ(), check=True)
subprocess.run([str(out / "rsx_texture_units_test.exe")], check=True, timeout=15)
