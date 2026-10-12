"""Create a new isolated 256-MiB FAT16 USB image; never replace an existing file."""
import argparse
import math
import struct
from pathlib import Path


def create_image(destination: Path, music: list[Path]) -> None:
    sector = 512
    total = 256 * 1024 * 1024 // sector
    partition = total - 1
    cluster_sectors, reserved, copies, fat_sectors, root_entries = 8, 1, 2, 256, 512
    root_sectors = root_entries * 32 // sector
    data_sector = 1 + reserved + copies * fat_sectors + root_sectors
    clusters = (total - data_sector) // cluster_sectors
    if len(music) > root_entries:
        raise ValueError("Too many music files")
    files = []
    required = 0
    for track in music:
        if not track.is_file() or track.suffix.lower() != ".mp3":
            raise ValueError("Music input must be a regular .mp3 file")
        size = track.stat().st_size
        count = math.ceil(size / (sector * cluster_sectors))
        required += count
        if required > clusters:
            raise ValueError("Music exceeds image capacity")
        files.append((track, size, count))
    mbr = bytearray(sector)
    mbr[446:462] = struct.pack("<B3sB3sII", 0, b"\x00\x02\x00", 0x06, b"\xfe\xff\xff", 1, partition)
    mbr[510:512] = b"\x55\xaa"
    boot = bytearray(sector)
    boot[:11] = b"\xeb\x3c\x90OCNBLAST"
    struct.pack_into("<HBHBHHBHHHII", boot, 11, sector, cluster_sectors, reserved, copies,
                     root_entries, 0, 0xf8, fat_sectors, 56, 9, 1, partition)
    boot[36:39] = bytes([128, 0, 0x29])
    struct.pack_into("<I", boot, 39, 0x4f434e42)
    boot[43:62] = b"OCEANBLAST " + b"FAT16   "
    boot[510:512] = b"\x55\xaa"
    fat = bytearray(fat_sectors * sector)
    struct.pack_into("<HH", fat, 0, 0xfff8, 0xffff)
    directory = bytearray(root_sectors * sector)
    next_cluster = 2
    for index, (_, size, count) in enumerate(files):
        offset = index * 32
        directory[offset:offset + 11] = f"TRACK{index + 1:03d}MP3".encode("ascii")
        directory[offset + 11] = 0x20
        struct.pack_into("<H", directory, offset + 24, 0x21)  # 1980-01-01
        struct.pack_into("<HI", directory, offset + 26, next_cluster if count else 0, size)
        for cluster in range(next_cluster, next_cluster + count):
            struct.pack_into("<H", fat, cluster * 2, 0xffff if cluster + 1 == next_cluster + count else cluster + 1)
        next_cluster += count
    with destination.open("xb") as output:
        output.truncate(total * sector)
        output.seek(0)
        output.write(mbr)
        output.write(boot)
        for _ in range(copies):
            output.write(fat)
        output.write(directory)
        next_cluster = 2
        for track, size, count in files:
            output.seek((data_sector + (next_cluster - 2) * cluster_sectors) * sector)
            with track.open("rb") as source:
                remaining = size
                while remaining:
                    chunk = source.read(min(1024 * 1024, remaining))
                    if not chunk:
                        raise OSError("Music input changed during creation")
                    output.write(chunk)
                    remaining -= len(chunk)
            next_cluster += count


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path)
    parser.add_argument("--music", type=Path, nargs="*", default=[])
    args = parser.parse_args()
    create_image(args.image, args.music)
    print(f"Created 256-MiB storage image with {len(args.music)} track(s). Guest formatting and writes affect this image only.")
