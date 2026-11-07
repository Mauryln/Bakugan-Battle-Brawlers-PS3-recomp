"""Summarize local PS3 I/O trace metadata without reading game assets."""
import argparse
from collections import defaultdict
from pathlib import Path
import struct

RECORD = struct.Struct("<7Q512s")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("trace", type=Path)
    args = parser.parse_args()
    files = defaultdict(lambda: {"reads": 0, "bytes": 0, "blocks": set()})
    with args.trace.open("rb") as stream:
        if stream.read(8) != b"PS3IO01\0":
            parser.error("unsupported trace header")
        for _ in range(4096):
            record = stream.read(RECORD.size)
            if len(record) != RECORD.size:
                break
            *identity, offset, count, path = RECORD.unpack(record)
            if not count or count > 1048576 or offset >= identity[2] or count > identity[2] - offset:
                continue
            guest = path.split(b"\0", 1)[0].decode("utf-8", errors="replace")
            entry = files[guest]
            entry["reads"] += 1
            entry["bytes"] += count
            entry["blocks"].update(range(offset // 65536, (offset + count - 1) // 65536 + 1))
    print("reads  requested_MB  block_footprint_MB  guest_path")
    for path, entry in sorted(files.items(), key=lambda item: item[1]["bytes"], reverse=True):
        print(f"{entry['reads']:5d}  {entry['bytes'] / 1048576:12.3f}  "
              f"{len(entry['blocks']) / 16:18.3f}  {path}")


if __name__ == "__main__":
    main()
