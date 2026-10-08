"""Read-only checks of specified zlib blocks in a NAND image; not a full filesystem audit."""
import argparse
from pathlib import Path
import zlib


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', type=Path)
    parser.add_argument('--raw-528', action='store_true', help='strip 16 OOB bytes per 512 data bytes')
    parser.add_argument('--base', type=lambda s: int(s, 0), default=0, help='data-stream base, e.g. rootfs offset')
    parser.add_argument('--block', action='append', required=True, help='offset:length, decimal or 0x numbers')
    args = parser.parse_args()
    raw = args.image.read_bytes()
    if args.raw_528:
        if len(raw) % 528:
            parser.error('raw NAND image length must be a multiple of 528')
        data = b''.join(raw[i:i + 512] for i in range(0, len(raw), 528))
    else:
        data = raw
    failed = False
    for specification in args.block:
        try:
            offset, size = (int(n, 0) for n in specification.split(':'))
        except ValueError:
            parser.error('block must be offset:length')
        start = args.base + offset
        if start < 0 or size <= 0 or start + size > len(data):
            parser.error('block outside NAND data stream')
        erased = [page for page in range(start // 512, (start + size + 511) // 512)
                  if data[page * 512:(page + 1) * 512] == b'\xff' * 512]
        print(f'Block offset={offset:#x} data_address={start:#x} size={size:#x}')
        if erased:
            print('  Entirely erased data pages:', ', '.join(hex(page * 512) for page in erased))
        try:
            decoded = zlib.decompress(data[start:start + size])
            print(f'  PASS: decoded {len(decoded)} bytes')
        except zlib.error as error:
            failed = True
            print('  FAIL:', error)
    return failed


if __name__ == '__main__':
    raise SystemExit(main())
