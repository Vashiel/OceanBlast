"""Compare local BIN files and ZIP members with a supplied MAME software list."""
import argparse
import csv
import hashlib
from pathlib import Path
import xml.etree.ElementTree as ET
import zipfile
import zlib


def identify(stream):
    sha1, crc, size = hashlib.sha1(), 0, 0
    while block := stream.read(1024 * 1024):
        sha1.update(block)
        crc = zlib.crc32(block, crc)
        size += len(block)
    return size, f'{crc:08x}', sha1.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--catalog', type=Path, required=True)
    parser.add_argument('--roms', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    entries = {}
    names = {}
    for software in ET.parse(args.catalog).getroot().findall('software'):
        for rom in software.findall('.//rom'):
            if not rom.get('crc') or not rom.get('sha1'):
                continue
            key = int(rom.get('size'), 0), rom.get('crc').lower(), rom.get('sha1').lower()
            entry = software.get('name'), rom.get('status', 'good'), software.get('supported', 'yes')
            entries[key] = entry
            names[rom.get('name').casefold()] = key
    rows = []

    def check(name, stream, member=None):
        size, crc, sha1 = identify(stream)
        match = entries.get((size, crc, sha1))
        basename = member.filename if member else Path(name).name
        status = 'MATCH' if match else ('CHECKSUM_MISMATCH' if basename.casefold() in names else 'NO_CATALOG_MATCH')
        row = dict(file=name, size=size, crc32=crc, sha1=sha1,
                   mame_match=match[0] if match else '', mame_dump_status=match[1] if match else '',
                   mame_supported=match[2] if match else '', assessment=status)
        rows.append(row)
        print(f'{name}: {status} CRC32={crc}')

    # Refuse to overwrite an earlier report. ZIP members are read without extraction.
    with args.output.open('x', encoding='utf-8', newline='') as report:
        for path in sorted(args.roms.rglob('*')):
            relative = path.relative_to(args.roms).as_posix()
            if path.suffix.lower() == '.bin':
                with path.open('rb') as stream:
                    check(relative, stream)
            elif path.suffix.lower() == '.zip':
                with zipfile.ZipFile(path) as archive:
                    for member in archive.infolist():
                        if member.filename.lower().endswith('.bin'):
                            with archive.open(member) as stream:
                                check(relative + '::' + member.filename, stream, member)
        if not rows:
            parser.error('no BIN files or BIN ZIP members found')
        writer = csv.DictWriter(report, fieldnames=rows[0].keys())
        writer.writeheader()
        writer.writerows(rows)
    print(f'Checked {len(rows)} images: {sum(r["assessment"] == "MATCH" for r in rows)} MAME matches')


if __name__ == '__main__':
    main()
