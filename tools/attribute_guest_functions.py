"""Attribute PC samples to byte-verified ELF32 ARM function symbols.

Reads local cartridge files and SDRAM; exports metadata without code bytes.
"""
import argparse
import bisect
import collections
import csv
import hashlib
import json
from pathlib import Path
import struct
from inspect_guest_elf import Memory


def functions(data, base):
    header = struct.unpack_from('<16sHHIIIIIHHHHHH', data)
    if header[0][:6] != b'\x7fELF\x01\x01' or header[2] != 40:
        raise ValueError('a little-endian ELF32 ARM file is required')
    if header[11] != 40 or not 0 < header[12] <= 4096:
        raise ValueError('unsupported section table')
    sections = [struct.unpack_from('<IIIIIIIIII', data, header[6] + i * 40)
                for i in range(header[12])]
    result = {}
    for section in sections:
        if section[1] not in (2, 11):
            continue
        if section[9] != 16 or section[5] % 16:
            raise ValueError('unsupported symbol table')
        strings = sections[section[6]]
        names = data[strings[4]:strings[4] + strings[5]]
        for offset in range(section[4], section[4] + section[5], 16):
            name, address, size, info, _, index = struct.unpack_from('<IIIBBH', data, offset)
            if info & 15 != 2 or not size or not 0 < index < len(sections):
                continue
            target = sections[index]
            address &= ~1  # Thumb symbols carry a low-bit marker.
            file_offset = target[4] + address - target[3]
            if not (target[3] <= address and address + size <= target[3] + target[5]):
                continue
            end = names.find(b'\0', name)
            if end < 0:
                raise ValueError('unterminated symbol name')
            result[(base + address, size)] = dict(name=names[name:end].decode('utf-8'),
                                                address=base + address, size=size,
                                                file_offset=file_offset)
    return sorted(result.values(), key=lambda item: item['address'])


def attribute(elf, memory, base, pcs, start, end, ttb):
    symbols = functions(elf, base)
    addresses = [s['address'] for s in symbols]
    counts = collections.Counter()
    total = 0
    for row in pcs:
        if not start * 20_000_000 < int(row['ticks']) <= end * 20_000_000:
            continue
        count = int(row['samples'])
        total += count
        if int(row['ttb'], 0) != ttb:
            continue
        pc = int(row['pc'], 0) & ~1
        index = bisect.bisect_right(addresses, pc) - 1
        if index >= 0 and pc < symbols[index]['address'] + symbols[index]['size']:
            counts[index] += count
    result = []
    for index, count in counts.most_common():
        symbol = symbols[index]
        offset, size = symbol['file_offset'], symbol['size']
        expected = elf[offset:offset + size]
        try:
            actual = memory.read(symbol['address'], size)
        except ValueError:
            status = 'unmapped_in_final_capture'
        else:
            status = 'identical' if actual == expected else 'different'
        result.append(dict(name=symbol['name'], address=hex(symbol['address']), size=size,
                           code_sha256=hashlib.sha256(expected).hexdigest(),
                           capture_comparison=status, samples=count,
                           sample_percent=100 * count / total if total else None))
    return dict(start_modeled_second=start, end_modeled_second=end,
                elf_sha256=hashlib.sha256(elf).hexdigest(), base=hex(base), ttb=hex(ttb),
                total_samples=total, attributed_samples=sum(counts.values()),
                byte_verified_samples=sum(r['samples'] for r in result if r['capture_comparison'] == 'identical'),
                functions=result)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('elf', type=Path)
    parser.add_argument('sdram', type=Path)
    parser.add_argument('pcs', type=Path)
    parser.add_argument('--base', type=lambda s: int(s, 0), required=True)
    parser.add_argument('--ttb', type=lambda s: int(s, 0), required=True)
    parser.add_argument('--start', type=int, required=True)
    parser.add_argument('--end', type=int, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if not 0 <= args.start < args.end:
        parser.error('an increasing, nonnegative modeled-time interval is required')
    try:
        memory = Memory(args.sdram.read_bytes(), args.ttb)
        with args.pcs.open(encoding='utf-8', newline='') as stream:
            result = attribute(args.elf.read_bytes(), memory, args.base,
                               csv.DictReader(stream), args.start, args.end, args.ttb)
        args.output.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    except (OSError, ValueError, IndexError, KeyError, struct.error) as error:
        parser.error(str(error))
    print(f"Attributed {result['attributed_samples']} of {result['total_samples']} samples; inspect code comparisons.")


if __name__ == '__main__':
    main()
