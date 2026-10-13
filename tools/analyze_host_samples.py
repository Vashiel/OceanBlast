"""Resolve sample_host_cpu offsets against a DWARF-enabled PE executable."""

import argparse
import csv
from collections import Counter
from pathlib import Path
import re
import struct
import subprocess


def image_base(executable: Path) -> int:
    with executable.open("rb") as source:
        source.seek(0x3C)
        pe_offset = struct.unpack("<I", source.read(4))[0]
        source.seek(pe_offset)
        if source.read(4) != b"PE\0\0":
            raise ValueError("Not a PE executable")
        source.seek(pe_offset + 4 + 20)
        optional = source.read(32)
    if struct.unpack_from("<H", optional)[0] != 0x20B:
        raise ValueError("Expected a 64-bit PE executable")
    return struct.unpack_from("<Q", optional, 24)[0]


def resolve(addr2line: Path, executable: Path, addresses: list[int]) -> dict[int, tuple[str, str]]:
    resolved = {}
    for start in range(0, len(addresses), 100):
        chunk = addresses[start : start + 100]
        command = [str(addr2line), "-a", "-f", "-C", "-i", "-e", str(executable)]
        command.extend(hex(address) for address in chunk)
        lines = subprocess.check_output(command, text=True, errors="replace").splitlines()
        current = None
        frames = []

        def finish():
            if current is None:
                return
            for function, location in frames:
                match = re.search(r"src[/\\](cpu|memory|core|display|audio)[/\\]", location)
                if match:
                    resolved[current] = (match.group(1), function)
                    return
                if re.search(r"src[/\\]main\.cpp", location):
                    resolved[current] = ("main", function)
                    return
            resolved[current] = ("runtime_or_unknown", frames[0][0] if frames else "??")

        index = 0
        while index < len(lines):
            line = lines[index].strip()
            if re.fullmatch(r"0x[0-9a-fA-F]+", line):
                finish()
                current = int(line, 16)
                frames = []
                index += 1
            elif line and index + 1 < len(lines):
                frames.append((line, lines[index + 1]))
                index += 2
            else:
                index += 1
        finish()
    return resolved


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("executable", type=Path)
    parser.add_argument("samples", type=Path)
    parser.add_argument("--addr2line", type=Path, default=Path("addr2line"))
    args = parser.parse_args()
    with args.samples.open(newline="") as source:
        counts = {int(row["module_offset"], 16): int(row["samples"]) for row in csv.DictReader(source)}
    base = image_base(args.executable)
    symbols = resolve(args.addr2line, args.executable, [base + offset for offset in counts])
    categories = Counter()
    functions = Counter()
    for offset, count in counts.items():
        category, function = symbols.get(base + offset, ("runtime_or_unknown", "??"))
        categories[category] += count
        functions[(category, function)] += count
    total = sum(counts.values())
    print(f"In-module samples: {total}")
    print("Category,samples,percent")
    for category, count in categories.most_common():
        print(f"{category},{count},{count * 100 / total:.1f}")
    print("\nTop functions:")
    for (category, function), count in functions.most_common(25):
        print(f"{count:5d} {count * 100 / total:5.1f}% {category:18s} {function}")


if __name__ == "__main__":
    main()
