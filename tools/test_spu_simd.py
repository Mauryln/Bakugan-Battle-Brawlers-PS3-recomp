"""Check the toolkit's SSE4.1 byte shuffle against its scalar reference."""
from pathlib import Path
import argparse
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "ps3recomp-src/tools"))
from msvc_env import environ

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--avx2", action="store_true", help="Also check vector xfloat (requires AVX2 CPU)")
args = parser.parse_args()

out = ROOT / "diagnostics/arena/spu-simd"
out.mkdir(parents=True, exist_ok=True)
cc = next(Path("C:/Program Files (x86)/Microsoft Visual Studio").glob(
    "*/BuildTools/VC/Tools/Llvm/x64/bin/clang-cl.exe"))
subprocess.run([str(cc), "/nologo", "/std:c17", "/MD", "/O2", "/Gy", "/w", "/clang:-msse4.1",
    "/I" + str(ROOT / "ps3recomp-src/include"),
    str(ROOT / "ps3recomp-src/runtime/spu/tests/test_spu_shufb.c"),
    "/Fe:spu_simd_test.exe", "/link", "/OPT:REF"], cwd=out, env=environ(), check=True)
result = subprocess.run([str(out / "spu_simd_test.exe")], check=True, capture_output=True, text=True)
assert "200000 random cases" in result.stdout, result.stdout
print(result.stdout, end="")
if args.avx2:
    for name in ["test_spu_float_neon", "test_spu_helpers"]:
        subprocess.run([str(cc), "/nologo", "/std:c17", "/MD", "/O2", "/Gy", "/w",
            "/clang:-mavx2", "/clang:-ffp-contract=off",
            "/I" + str(ROOT / "ps3recomp-src/include"),
            str(ROOT / "ps3recomp-src/runtime/spu/tests" / f"{name}.c"),
            f"/Fe:{name}.exe", "/link", "/OPT:REF"], cwd=out, env=environ(), check=True)
        subprocess.run([str(out / f"{name}.exe")], check=True, timeout=60)
