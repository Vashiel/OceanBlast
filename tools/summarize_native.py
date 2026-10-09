"""Resolve sample_native executable RVAs; output function totals without local paths."""
import argparse
import bisect
import collections
import csv
import struct
import re
import subprocess
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('exe', type=Path)
parser.add_argument('samples', type=Path)
parser.add_argument('output', type=Path)
parser.add_argument('--addr2line', default='addr2line')
parser.add_argument('--nm', default='nm')
args = parser.parse_args()
image = args.exe.read_bytes()
pe = struct.unpack_from('<I', image, 0x3c)[0]
if image[:2] != b'MZ' or image[pe:pe+4] != b'PE\0\0' or struct.unpack_from('<H', image, pe+24)[0] != 0x20b:
    parser.error('an x64 PE executable is required')
base = struct.unpack_from('<Q', image, pe+48)[0]
with args.samples.open(newline='', encoding='utf-8') as source:
    rows = list(csv.DictReader(source))
addresses = ''.join(hex(base + int(row['rva'], 16)) + '\n' for row in rows)
resolved = subprocess.run([args.addr2line, '-f', '-C', '-e', str(args.exe)], input=addresses,
                          text=True, capture_output=True, check=True).stdout.splitlines()
if len(resolved) != len(rows) * 2:
    raise ValueError('unexpected symbol resolution output')
symbols = {}
if '??' in resolved[::2]:
    listing = subprocess.run([args.nm, '-n', '-C', '--defined-only', str(args.exe)],
                             text=True, capture_output=True, check=True).stdout
    for line in listing.splitlines():
        match = re.match(r'^([0-9a-fA-F]+) [tT] (.+)$', line)
        if match and not match[2].startswith('.'):
            symbols[int(match[1], 16)] = match[2]
addresses_sorted = sorted(symbols)
totals = collections.Counter()
for row, function in zip(rows, resolved[::2]):
    if function == '??':
        index = bisect.bisect_right(addresses_sorted, base + int(row['rva'], 16)) - 1
        if index >= 0:
            function = symbols[addresses_sorted[index]] + ' [symbol interval]'
    totals[function] += int(row['samples'])
count = sum(totals.values())
with args.output.open('w', newline='', encoding='utf-8') as target:
    writer = csv.writer(target)
    writer.writerow(['function', 'samples', 'percent_of_executable_samples'])
    for function, samples in totals.most_common():
        writer.writerow([function, samples, round(samples * 100 / count, 2)])
        print(f'{samples:6d} {samples * 100 / count:6.2f}% {function}')
