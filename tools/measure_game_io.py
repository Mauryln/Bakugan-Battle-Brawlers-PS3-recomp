"""Launch a manual-input game run and timestamp existing performance/I/O logs.

This never sends input or closes the game. Keep its window visible, enter the
same arena, and leave the camera idle for at least 30 seconds in each variant.
"""
import argparse
import ctypes
import json
import os
from pathlib import Path
import re
import statistics
import subprocess
import time

ROOT = Path(__file__).resolve().parents[1]
PERF = re.compile(r"\[PERF\] ([\d.]+) fps \| draws ([\d.]+)")
IO = re.compile(r"\[asset_io\].*host_reads=(\d+).*host_MB=([\d.]+).*cache_MB=([\d.]+)/.*preload=(\d+) used=(\d+) demand_reads=(\d+) demand_ms=([\d.]+)")


def process_cpu(process: subprocess.Popen) -> float:
    values = [ctypes.c_uint64() for _ in range(4)]
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.GetProcessTimes.argtypes = [ctypes.c_void_p] + [ctypes.POINTER(ctypes.c_uint64)] * 4
    if not kernel.GetProcessTimes(int(process._handle), *(ctypes.byref(value) for value in values)):
        return 0.0
    return (values[2].value + values[3].value) / 10_000_000


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("label", help="Unique directory name under diagnostics/")
    parser.add_argument("--preload", type=Path)
    parser.add_argument("--cache-mb", type=int, default=64)
    parser.add_argument("--no-timing", action="store_true", help="Disable added I/O timing instrumentation")
    args = parser.parse_args()
    if os.name != "nt":
        parser.error("this game runner is Windows-only")
    if not 0 <= args.cache_mb <= 1024:
        parser.error("cache-mb must be between 0 and 1024")
    if Path(args.label).name != args.label or args.label in (".", ".."):
        parser.error("label must be a single directory name")
    folder = ROOT / "diagnostics" / args.label
    folder.mkdir(exist_ok=False)
    exe = ROOT / "bakugan-recomp/build/BakuganRecomp.exe"
    elf = ROOT / "game/PS3_GAME/USRDIR/EBOOT.elf"
    env = {k: v for k, v in os.environ.items() if not k.startswith("PS3_IO_")}
    for option in ("PAD_FILE", "PAD_SCRIPT", "PAD_CLOCK", "PAD_AUTOPRESS", "PAD_NO_KEYBOARD", "YDKJ_INJECT_PAD"):
        env.pop(option, None)
    env.update(PS3_VFS_ROOT=str(ROOT / "game"), PS3_VERBOSE="0", PERF="1",
               PS3_IO_CACHE_MB=str(args.cache_mb), PS3_IO_STATS="1", PS3_IO_TIMING="0" if args.no_timing else "1")
    if args.preload:
        trace = args.preload.resolve(strict=True)
        env["PS3_IO_PRELOAD"] = str(trace)
    started = time.monotonic()
    with (folder / "stdout.log").open("wb", buffering=0) as out, (folder / "stderr.log").open("wb", buffering=0) as err:
        process = subprocess.Popen([str(exe), str(elf)], cwd=ROOT, env=env, stdout=out, stderr=err)
        (folder / "run.json").write_text(json.dumps({"pid": process.pid, "preload": env.get("PS3_IO_PRELOAD"),
            "cache_mb": args.cache_mb, "io_timing": not args.no_timing, "inputs": "manual",
            "os_cache": "uncontrolled/warm; not cold-cache"}, indent=2))
        print(f"Started {args.label}, pid={process.pid}. Manual input; keep the game visible.", flush=True)
        fps = []
        io = []
        first_battle = None
        cpu_at_battle = 0.0
        last_summary = 0.0
        pending = b""
        with (folder / "stderr.log").open("rb", buffering=0) as reader, (folder / "timeline.jsonl").open("w", encoding="utf-8") as timeline:
            while True:
                chunk = reader.read(262144)
                if chunk:
                    pending += chunk
                    lines = pending.split(b"\n"); pending = lines.pop()
                    for raw in lines:
                        line = raw.decode("utf-8", errors="replace")
                        perf, sample = PERF.search(line), IO.search(line)
                        if not perf and not sample:
                            continue
                        now = time.monotonic() - started
                        timeline.write(json.dumps({"t_s": now, "line": line}) + "\n")
                        timeline.flush()
                        if sample:
                            io.append({"t_s": now, "host_reads": int(sample[1]), "host_mb": float(sample[2]),
                                "cache_mb": float(sample[3]), "preloaded": int(sample[4]), "used": int(sample[5]),
                                "demand_reads": int(sample[6]), "demand_ms": float(sample[7])})
                        if perf and float(perf[2]) >= 1000:
                            if first_battle is None:
                                first_battle, cpu_at_battle = now, process_cpu(process)
                                print("Battle candidate detected. Leave the camera idle for 30 s; confirm visually.", flush=True)
                            fps.append({"t_s": now, "fps": float(perf[1]), "draws": float(perf[2])})
                elapsed = time.monotonic() - started
                if elapsed - last_summary >= 1:
                    last_summary = elapsed
                    stable = [entry for entry in fps if entry["t_s"] >= (first_battle or elapsed) + 5]
                    summary = {"elapsed_s": elapsed, "battle_candidate_s": first_battle, "fps_windows": len(stable),
                        "fps_window_median": statistics.median(x["fps"] for x in stable) if stable else None,
                        "fps_weighted": len(stable) / sum(1 / x["fps"] for x in stable) if stable else None,
                        "last_io": io[-1] if io else None, "sampling": "20-frame PERF windows; not individual-frame percentiles"}
                    if first_battle is not None:
                        summary["battle_observed_s"] = elapsed - first_battle
                        summary["cpu_core_equivalents"] = (process_cpu(process) - cpu_at_battle) / max(elapsed - first_battle, 0.001)
                    (folder / "summary.json").write_text(json.dumps(summary, indent=2))
                if process.poll() is not None and not chunk:
                    break
                time.sleep(0.05)
        print(f"Run ended; results in {folder}", flush=True)


if __name__ == "__main__":
    main()
