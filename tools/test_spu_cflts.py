"""Verify native CFLTS against the exact scalar SPU conversion."""
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "ps3recomp-src/tools"))
from msvc_env import environ

out = ROOT / "diagnostics/arena/spu-cflts"
out.mkdir(parents=True, exist_ok=True)
cc = next(Path("C:/Program Files (x86)/Microsoft Visual Studio").glob(
    "*/BuildTools/VC/Tools/Llvm/x64/bin/clang-cl.exe"))
for name, flags in [("sse2", []), ("avx2", ["/clang:-mavx2"]),
                    ("scalar", ["/DSPU_SCALAR_HELPERS"])]:
    executable = "spu_cflts_" + name + ".exe"
    subprocess.run([str(cc), "/nologo", "/std:c++20", "/MD", "/O2", "/EHsc", "/w",
        *flags, "/I" + str(ROOT / "ps3recomp-src/runtime/spu"),
        "/I" + str(ROOT / "ps3recomp-src/include"),
        str(ROOT / "tools/tests/spu_cflts_test.cpp"), "/Fe:" + executable],
        cwd=out, env=environ(), check=True)
    print(name, flush=True)
    subprocess.run([str(out / executable)], check=True, timeout=30)
