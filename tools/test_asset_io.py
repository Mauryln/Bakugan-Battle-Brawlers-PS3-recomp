"""Build and exercise the real asset backend without a game dump or guest VM."""
from pathlib import Path
import argparse
import json
import os
import shutil
import statistics
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--benchmark", action="store_true", help="Measure warm hits and bounded-cache eviction")
    parser.add_argument("--backend", type=Path, help="Alternative backend source for an A/B comparison")
    parser.add_argument("--streaming", action="store_true", help="Test trace recording, replay and stale traces")
    args = parser.parse_args()
    sdk = ROOT / "ps3recomp-src"
    fixture = "asset_io_trace_test.cpp" if args.streaming else ("asset_io_bench.cpp" if args.benchmark else "asset_io_test.cpp")
    backend = args.backend.resolve() if args.backend else sdk / "runtime/io/asset_io.cpp"
    sources = [ROOT / "tools/tests" / fixture, backend]
    with tempfile.TemporaryDirectory(prefix="bakugan-asset-io-") as directory:
        temporary = Path(directory)
        if os.name == "nt":
            compiler = os.environ.get("CXX") or shutil.which("clang-cl")
            if not compiler:
                compiler = str(Path(os.environ["ProgramFiles(x86)"]) /
                               "Microsoft Visual Studio/2022/BuildTools/VC/Tools/Llvm/x64/bin/clang-cl.exe")
            binary = temporary / "asset_io_test.exe"
            command = [compiler, "/nologo", "/std:c++20", "/EHsc", "/O2",
                       "/D_CRT_SECURE_NO_WARNINGS", f"/I{sdk / 'include'}",
                       *map(str, sources), f"/Fe{binary}", f"/Fo{temporary}{os.sep}"]
        else:
            binary = temporary / "asset_io_test"
            command = [os.environ.get("CXX", "c++"), "-std=c++17", "-O2", "-pthread",
                       f"-I{sdk / 'include'}", *map(str, sources), "-o", str(binary)]
        subprocess.run(command, check=True, cwd=temporary)
        if args.streaming:
            data = temporary / "data.bin"
            data.write_bytes(bytes((i * 31 + i // 997) & 255 for i in range(3 * 1024 * 1024)))
            trace = temporary / "reads.trace"
            env = {k: v for k, v in os.environ.items() if not k.startswith("PS3_IO_")}
            env.update(PS3_IO_CACHE_MB="1", PS3_IO_STATS="0")
            def run(mode: str, **options: str) -> None:
                subprocess.run([str(binary), mode, str(data)], cwd=temporary,
                               env=dict(env, **options), check=True, timeout=30)
            run("record", PS3_IO_TRACE_RECORD=str(trace))
            if not trace.is_file() or trace.stat().st_size < 8:
                raise RuntimeError("No trace was recorded")
            run("replay", PS3_IO_PRELOAD=str(trace))
            run("race", PS3_IO_PRELOAD=str(trace))
            run("protected", PS3_IO_PRELOAD=str(trace))
            run("shutdown", PS3_IO_PRELOAD=str(trace))
            run("missing", PS3_IO_PRELOAD=str(temporary / "missing.trace"))
            corrupt = temporary / "corrupt.trace"
            corrupt.write_bytes(b"invalid!")
            run("corrupt", PS3_IO_PRELOAD=str(corrupt))
            truncated = temporary / "truncated.trace"
            truncated.write_bytes(trace.read_bytes()[:-7])
            run("truncated", PS3_IO_PRELOAD=str(truncated))
            invalid_ranges = bytearray(trace.read_bytes())
            for offset in range(8, len(invalid_ranges) - 568 + 1, 568):
                invalid_ranges[offset + 40:offset + 48] = b"\xff" * 8
            corrupt.write_bytes(invalid_ranges)
            run("invalid-range", PS3_IO_PRELOAD=str(corrupt))
            limited = temporary / "limited.trace"
            run("limit", PS3_IO_TRACE_RECORD=str(limited))
            if limited.stat().st_size != 8 + 4096 * 568:
                raise RuntimeError("Trace exceeded its record budget or stopped early")
            with data.open("ab") as output:
                output.write(b"changed-generation")
            run("stale", PS3_IO_PRELOAD=str(trace))
            return
        if args.benchmark:
            for mode, budget in (("hits", "64"), ("eviction", "1")):
                samples = []
                checksums = set()
                env = {k: v for k, v in os.environ.items() if not k.startswith("PS3_IO_")}
                env.update(PS3_IO_CACHE_MB=budget, PS3_IO_STATS="0")
                for _ in range(5):
                    arguments = [str(binary)] + ([mode] if mode == "eviction" else [])
                    result = subprocess.run(arguments, env=env, cwd=temporary, check=True,
                                            capture_output=True, text=True, timeout=30)
                    _, elapsed, checksum = result.stdout.strip().split(",")
                    samples.append(float(elapsed))
                    checksums.add(checksum)
                if len(checksums) != 1:
                    raise RuntimeError("Benchmark checksums changed between runs")
                print(json.dumps({"mode": mode, "samples_ms": samples,
                                  "median_ms": statistics.median(samples), "checksum": checksums.pop()}))
            return
        for budget in ("0", "1", "64"):
            env = {k: v for k, v in os.environ.items() if not k.startswith("PS3_IO_")}
            env.update(PS3_IO_CACHE_MB=budget, PS3_IO_STATS="1", PS3_IO_TIMING="1")
            print(f"Testing PS3_IO_CACHE_MB={budget}", flush=True)
            subprocess.run([str(binary)], env=env, check=True, timeout=30)


if __name__ == "__main__":
    main()
