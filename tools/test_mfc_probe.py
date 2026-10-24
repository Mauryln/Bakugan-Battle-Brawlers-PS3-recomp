"""Guard the MFC channel path against repeated process-wide environment locking."""
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "ps3recomp-src/tools"))
from msvc_env import environ

out = ROOT / "diagnostics/arena/mfc-probe"
out.mkdir(parents=True, exist_ok=True)
source = (ROOT / "ps3recomp-src/runtime/spu/spu_channels.c").read_text(encoding="utf-8")
start = source.index("typedef struct spu_mfc_slot")
end = source.index("/* SPU_PCSAMPLE=", start)
function = source.index("static mfc_engine* mfc_for(", end)
function_end = source.index("\nvoid spu_mfc_release(", function)
(out / "mfc_probe_entry.inc").write_text(source[start:end] + source[function:function_end], encoding="utf-8")
cc = next(Path("C:/Program Files (x86)/Microsoft Visual Studio").glob(
    "*/BuildTools/VC/Tools/Llvm/x64/bin/clang-cl.exe"))
subprocess.run([str(cc), "/nologo", "/std:c17", "/MD", "/O2", "/Gy", "/w",
    "/I" + str(ROOT / "ps3recomp-src/include"), "/I" + str(ROOT / "ps3recomp-src/runtime/spu"),
    "/I" + str(out), str(ROOT / "tools/tests/mfc_probe_test.c"), "/Fe:mfc_probe_test.exe",
    "/link", "/OPT:REF"], cwd=out, env=environ(), check=True)
subprocess.run([str(out / "mfc_probe_test.exe")], check=True, timeout=10)
