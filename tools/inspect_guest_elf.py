"""Read mapped ARM ELF export metadata from a local SDRAM dump and guest TTB."""
import argparse
import hashlib
import json
from pathlib import Path
import struct


class Memory:
    def __init__(self, data, ttb):
        if len(data) != 32 * 1024 * 1024 or not 0x30000000 <= ttb <= 0x31ffc000 or ttb & 0x3fff:
            raise ValueError('a 32 MiB SDRAM image and aligned SDRAM TTB are required')
        self.data, self.ttb = data, ttb

    def physical_word(self, address):
        offset = address - 0x30000000
        if not 0 <= offset <= len(self.data) - 4:
            raise ValueError('page table outside SDRAM')
        return struct.unpack_from('<I', self.data, offset)[0]

    def translate(self, address):
        descriptor = self.physical_word(self.ttb + (address >> 20) * 4)
        kind = descriptor & 3
        if kind == 2:
            return (descriptor & 0xfff00000) | (address & 0xfffff)
        if kind == 1:
            page = self.physical_word((descriptor & 0xfffffc00) + ((address >> 12) & 255) * 4)
        elif kind == 3:
            page = self.physical_word((descriptor & 0xfffff000) + ((address >> 10) & 1023) * 4)
        else:
            raise ValueError('unmapped guest address')
        if page & 3 == 1:
            return (page & 0xffff0000) | (address & 0xffff)
        if page & 3 == 2 or (kind == 1 and page & 3 == 3):
            return (page & 0xfffff000) | (address & 4095)
        if kind == 3 and page & 3 == 3:
            return (page & 0xfffffc00) | (address & 1023)
        raise ValueError('unmapped guest page')

    def read(self, address, length):
        result = bytearray()
        while length:
            offset = self.translate(address) - 0x30000000
            count = min(length, 1024 - (address & 1023))
            if not 0 <= offset <= len(self.data) - count:
                raise ValueError('mapped data outside SDRAM')
            result += self.data[offset:offset + count]
            address += count
            length -= count
        return bytes(result)

    def string(self, address):
        value = bytearray()
        for offset in range(512):
            byte = self.read(address + offset, 1)[0]
            if not byte:
                return value.decode('utf-8', errors='replace')
            value.append(byte)
        raise ValueError('unterminated ELF string')


def inspect(memory, address):
    header = struct.unpack('<16sHHIIIIIHHHHHH', memory.read(address, 52))
    if header[0][:6] != b'\x7fELF\x01\x01' or header[2] != 40 or not 0 < header[10] <= 256 or header[9] < 32:
        raise ValueError('unsupported ELF header')
    segments = [struct.unpack('<IIIIIIII', memory.read(address + header[5] + i * header[9], 32))
                for i in range(header[10])]
    base = address - min(segment[2] & ~4095 for segment in segments if segment[0] == 1)
    dynamic = next(segment for segment in segments if segment[0] == 2)
    tags = {}
    for index in range(min(dynamic[4] // 8, 4096)):
        tag, value = struct.unpack('<II', memory.read(base + dynamic[2] + index * 8, 8))
        if not tag:
            break
        tags[tag] = value
    pointer = lambda tag: tags[tag] if tags[tag] >= base else tags[tag] + base
    strings, symbols, table = pointer(5), pointer(6), pointer(4)
    count = struct.unpack('<II', memory.read(table, 8))[1]
    if count > 100000:
        raise ValueError('invalid ELF symbol count')
    exports = []
    for index in range(count):
        name, value, size, info, _, section = struct.unpack('<IIIBBH', memory.read(symbols + index * 16, 16))
        if name and section and info & 15 == 2:
            exports.append(dict(name=memory.string(strings + name), address=hex(base + value), size=size))
    return dict(elf_header=hex(address), base=hex(base),
                soname=memory.string(strings + tags[14]) if 14 in tags else '', exports=exports)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('sdram', type=Path)
    parser.add_argument('ttb', type=lambda value: int(value, 0))
    parser.add_argument('output', type=Path)
    parser.add_argument('--start', type=lambda value: int(value, 0), default=0x40000000)
    parser.add_argument('--end', type=lambda value: int(value, 0), default=0x41000000)
    args = parser.parse_args()
    if not 0 <= args.start < args.end <= 0x100000000 or args.start & 4095:
        parser.error('an aligned, increasing 32-bit scan range is required')
    data = args.sdram.read_bytes()
    memory = Memory(data, args.ttb)
    objects = []
    for address in range(args.start, args.end, 4096):
        try:
            if memory.read(address, 4) == b'\x7fELF':
                objects.append(inspect(memory, address))
        except (ValueError, KeyError, StopIteration, struct.error):
            continue
    args.output.write_text(json.dumps(dict(sdram_sha256=hashlib.sha256(data).hexdigest(),
                                          ttb=hex(args.ttb), objects=objects), indent=2) + '\n', encoding='utf-8')
    print(f'Inspected {len(objects)} mapped ELF objects; only ELF metadata was exported.')


if __name__ == '__main__':
    main()
