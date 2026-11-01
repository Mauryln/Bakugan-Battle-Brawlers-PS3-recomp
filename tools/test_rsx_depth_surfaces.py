"""Verify native depth surfaces retain independent, sampleable GPU contents."""
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "ps3recomp-src/tools"))
from msvc_env import environ

source = (ROOT / "ps3recomp-src/libs/video/rsx_d3d12_backend.c").read_text(encoding="utf-8")
assert "int zeta_slot" in source, "draws still share the single legacy depth buffer"
assert "DXGI_FORMAT_R24_UNORM_X8_TYPELESS" in source, "depth texture bindings must sample native depth"
out = ROOT / "diagnostics/arena/depth-surfaces"
out.mkdir(parents=True, exist_ok=True)
header = (ROOT / "ps3recomp-src/libs/video/rsx_d3d12_depth.h").read_text(encoding="utf-8")
(out / "depth_pool.inc").write_text(header.replace("{0}", "{}").replace(
    "&IID_ID3D12Resource", "IID_ID3D12Resource").replace(
    "view.Flags = D3D12_DSV_FLAG_READ_ONLY_DEPTH | D3D12_DSV_FLAG_READ_ONLY_STENCIL;",
    "view.Flags = (D3D12_DSV_FLAGS)(D3D12_DSV_FLAG_READ_ONLY_DEPTH | D3D12_DSV_FLAG_READ_ONLY_STENCIL);"), encoding="utf-8")
cc = next(Path("C:/Program Files (x86)/Microsoft Visual Studio").glob(
    "*/BuildTools/VC/Tools/Llvm/x64/bin/clang-cl.exe"))
subprocess.run([str(cc), "/nologo", "/std:c++20", "/MD", "/O2", "/EHsc",
    "/I" + str(out), str(ROOT / "tools/tests/rsx_depth_surfaces_test.cpp"),
    "/Fe:depth_surfaces_test.exe", "/link", "d3d12.lib"], cwd=out, env=environ(), check=True)
subprocess.run([str(out / "depth_surfaces_test.exe")], check=True, timeout=30)
