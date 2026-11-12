"""Check offscreen RT sampling pin, LRU eviction, and alias EA invalidation."""
import argparse
from pathlib import Path
import re
import subprocess
import tempfile


def _define_int(src, name):
    m = re.search(rf"^#define\s+{name}\s+(\d+)\b", src, re.M)
    if not m:
        raise RuntimeError(f"Missing macro {name}")
    return int(m[1])


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--compiler", default="clang-cl")
    compiler = parser.parse_args().compiler
    root = Path(__file__).resolve().parent.parent
    src = (root / "ps3recomp-src/libs/video/rsx_d3d12_backend.c").read_text(encoding="utf-8")
    depth_src = (root / "ps3recomp-src/libs/video/rsx_d3d12_depth.h").read_text(encoding="utf-8")
    gcm_src = (root / "ps3recomp-src/libs/video/cellGcmSys.c").read_text(encoding="utf-8")

    max_rts = _define_int(src, "MAX_OFF_RTS")
    rt_srv_base = _define_int(src, "RT_SRV_BASE")
    draw_srv_base = _define_int(src, "DRAW_SRV_BASE")
    assert max_rts >= 64, f"MAX_OFF_RTS={max_rts} < 64"
    assert draw_srv_base >= rt_srv_base + max_rts, (
        f"DRAW_SRV_BASE={draw_srv_base} overlaps RT_SRV_BASE+MAX_OFF_RTS={rt_srv_base + max_rts}"
    )
    assert _define_int(depth_src, "MAX_DEPTH_SURFACES") >= 32
    assert "ps3_guest_stamp(dst2, out_w * out_h * 4u);" in gcm_src

    alias_decl = re.search(r"static struct \{ u32 dst, src, gen; \} s_rt_alias\[\d+\];\s*static int s_rt_alias_n;", src)
    tick_decl = re.search(r"static u64 s_off_rt_tick = 0;", src)
    m_find = re.search(r"^static int off_rt_find\([^\n]*\n\{.*?^\}", src, re.M | re.S)
    m_alias = re.search(r"^static int off_rt_alias_find\([^\n]*\n\{.*?^\}", src, re.M | re.S)
    m_get = re.search(r"^static int off_rt_get\([^\n]*\n\{.*?^\}", src, re.M | re.S)
    m_pre = re.search(
        r"    /\* Render-to-texture pre-pass:.*?(?=\n\s*int pack_parallel = vp_pack_workers_active\(\);)",
        src, re.S,
    )
    if not all((alias_decl, tick_decl, m_find, m_alias, m_get, m_pre)):
        raise RuntimeError("Failed to extract offscreen RT prepass or helpers")

    inc_body = "\n\n".join((
        tick_decl[0],
        m_find[0],
        alias_decl[0],
        m_alias[0],
        m_get[0],
        "static void off_rt_prepass(void) {\n" + m_pre[0] + "\n}",
    ))
    with tempfile.TemporaryDirectory(prefix="bakugan-offrt-pin-") as directory:
        work = Path(directory)
        (work / "offrt_pin.inc").write_text(inc_body, encoding="utf-8")
        subprocess.run([compiler, "/nologo", "/O2", "/I" + str(work),
                        str(root / "tools/tests/rsx_offrt_pin_test.c"),
                        "/Fe" + str(work / "test.exe")], cwd=work, check=True)
        subprocess.run([str(work / "test.exe")], check=True)


if __name__ == "__main__":
    main()
