"""Compile the real public join entrypoint and taskset memory helpers in isolation."""
from pathlib import Path
import subprocess,sys
root=Path(__file__).resolve().parent.parent
sys.path.insert(0,str(root/'ps3recomp-src/tools'))
from msvc_env import environ
out=root/'diagnostics/stability';out.mkdir(parents=True,exist_ok=True)
s=(root/'ps3recomp-src/libs/spurs/cellSpurs.c').read_text()
a=s.index('s32 cellSpursShutdownTaskset(');j=s.index('s32 cellSpursJoinTaskset(',a);end=s.index('\ns32 ',j+1)
(out/'join_entry.inc').write_text(s[a:end])
cc=next(Path('C:/Program Files (x86)/Microsoft Visual Studio').glob('*/BuildTools/VC/Tools/Llvm/x64/bin/clang-cl.exe'))
inc=['/I'+str(root/p) for p in ['ps3recomp-src/include','ps3recomp-src/libs/spurs','diagnostics/stability']]
env=environ()
subprocess.run([str(cc),'/nologo','/TC','/std:c17','/MD','/O2','/c',*inc,str(root/'ps3recomp-src/libs/spurs/spurs_taskset.c'),'/Fo:taskset.obj'],cwd=out,env=env,check=True)
subprocess.run([str(cc),'/nologo','/std:c++20','/EHsc','/MD','/O2',*inc,str(root/'tools/tests/spurs_join_test.cpp'),str(out/'taskset.obj'),'/Fe:spurs_join_test.exe'],cwd=out,env=env,check=True)
subprocess.run([str(out/'spurs_join_test.exe')],check=True,timeout=10)
