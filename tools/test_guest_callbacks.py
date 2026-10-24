from pathlib import Path
import subprocess,sys
root=Path(__file__).resolve().parent.parent
sys.path.insert(0,str(root/'ps3recomp-src/tools'))
from msvc_env import environ
out=root/'diagnostics/stability';out.mkdir(parents=True,exist_ok=True)
cc=next(Path('C:/Program Files (x86)/Microsoft Visual Studio').glob('*/BuildTools/VC/Tools/Llvm/x64/bin/clang-cl.exe'))
inc=['/I'+str(root/p) for p in ['bakugan-recomp/recompiled','ps3recomp-src/runtime/ppu','ps3recomp-src/include']]
subprocess.run([str(cc),'/nologo','/std:c++20','/EHsc','/MD','/O2','/Gy',*inc,str(root/'tools/tests/guest_callback_test.cpp'),str(root/'ps3recomp-src/runtime/ppu/ppu_loader.cpp'),'/Fe:guest_callback_test.exe','/link','/OPT:REF'],cwd=out,env=environ(),check=True)
subprocess.run([str(out/'guest_callback_test.exe')],check=True,timeout=20)
