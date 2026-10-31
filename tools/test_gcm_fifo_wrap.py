"""Run the actual flip wrap and recycle code against a delayed FIFO consumer."""
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "ps3recomp-src/tools"))
from msvc_env import environ

out = ROOT / "diagnostics/arena/fifo-wrap-test"
out.mkdir(parents=True, exist_ok=True)
source = (ROOT / "ps3recomp-src/libs/video/cellGcmSys.c").read_text(encoding="utf-8")
functions = []
for signature in ("void cellGcm_fifo_recycle(u32 ctx_ea)",
                  "static int gcm_flip_into_fifo(u32 ctx, u32 bufferId)"):
    start = source.index(signature)
    body = source.index("{", start)
    depth, end = 1, body + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    functions.append(source[start:end])
(out / "fifo_wrap.inc").write_text("\n".join(functions), encoding="utf-8")
cc = next(Path("C:/Program Files (x86)/Microsoft Visual Studio").glob(
    "*/BuildTools/VC/Tools/Llvm/x64/bin/clang-cl.exe"))
subprocess.run([str(cc), "/nologo", "/std:c++20", "/MD", "/O2", "/EHsc",
                "/I" + str(out), str(ROOT / "tools/tests/gcm_fifo_wrap_test.cpp"),
                "/Fe:fifo_wrap_test.exe"], cwd=out, env=environ(), check=True)
subprocess.run([str(out / "fifo_wrap_test.exe")], check=True, timeout=15)
