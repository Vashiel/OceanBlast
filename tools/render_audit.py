"""Render active framebuffer dumps from audit_games.py (requires Pillow)."""
import argparse
import csv
from pathlib import Path
from PIL import Image, ImageDraw


def decode(data, pixel_format=None, stride=0):
    if pixel_format or stride:
        pixel_format = pixel_format or ('rgb565' if len(data) == 76800 else 'rgb444')
        row_bytes = 480 if pixel_format == 'rgb565' else 360
        stride = stride or row_bytes
        if stride < row_bytes or len(data) < stride * 159 + row_bytes:
            raise ValueError('framebuffer does not fit the selected format and stride')
        data = b''.join(data[y * stride:y * stride + row_bytes] for y in range(160))
    pixels = []
    if len(data) == 76800:
        for offset in range(0, len(data), 2):
            value = data[offset] | data[offset+1] << 8
            r, g, b = value >> 11, (value >> 5) & 63, value & 31
            pixels.append(((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)))
    elif len(data) == 57600:
        for offset in range(0, len(data), 3):
            a, b, c = data[offset:offset+3]
            for value in ((a << 4) | (b >> 4), ((b & 15) << 8) | c):
                pixels.append((((value >> 8) & 15)*17, ((value >> 4) & 15)*17, (value & 15)*17))
    else:
        raise ValueError(f'unsupported framebuffer size: {len(data)}')
    frame = Image.new('RGB', (240, 160))
    frame.putdata(pixels)
    return frame


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('audit', type=Path)
    parser.add_argument('--timeline', action='store_true', help='render snapshot files from one cartridge directory')
    parser.add_argument('--format', choices=['rgb444', 'rgb565'], help='explicit pixel decoder')
    parser.add_argument('--stride', type=int, default=0, help='scanline stride in bytes')
    args = parser.parse_args()
    if args.timeline:
        files = sorted(args.audit.glob('snapshot_*.raw'), key=lambda path: int(path.stem.split('_')[1]))
        if not files:
            parser.error('no snapshots found')
        panel = Image.new('RGB', (960, ((len(files)+3)//4)*185), '#eeeeee')
        draw = ImageDraw.Draw(panel)
        for index, path in enumerate(files):
            x, y = index % 4 * 240, index // 4 * 185
            draw.text((x+3,y+3), path.stem, fill='black')
            panel.paste(decode(path.read_bytes(), args.format, args.stride),(x,y+25))
        panel.save(args.audit / 'timeline.png')
        return
    with (args.audit / 'results.csv').open(encoding='utf-8') as source:
        rows = list(csv.DictReader(source))
    panel = Image.new('RGB', (720, ((len(rows)+2)//3)*210), '#eeeeee')
    draw = ImageDraw.Draw(panel)
    for index, row in enumerate(rows):
        directory = args.audit / Path(row['rom']).stem
        raw = directory / 'fb_active.raw'
        x, y = index % 3 * 240, index // 3 * 210
        draw.text((x+4,y+3), row['rom'].split(' [')[0][:32], fill='black')
        if raw.exists():
            frame = decode(raw.read_bytes(), args.format, args.stride)
            frame.save(directory / 'frame.png')
            panel.paste(frame,(x,y+25))
        draw.text((x+4,y+187), row['nonzero_bytes'], fill='black')
    panel.save(args.audit / 'frames.png')


if __name__ == '__main__':
    main()
