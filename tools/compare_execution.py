"""Compare scalar and batched host execution with identical cartridge inputs."""
import argparse
import csv
import hashlib
from pathlib import Path
import subprocess
import sys
import time


def digest(path):
    with path.open('rb') as source:
        return hashlib.file_digest(source, 'sha256').hexdigest()


def process_seconds(process):
    if sys.platform != 'win32':
        return None
    import ctypes
    from ctypes import wintypes
    query = ctypes.WinDLL('kernel32', use_last_error=True).GetProcessTimes
    query.argtypes = [wintypes.HANDLE] + [ctypes.POINTER(wintypes.FILETIME)] * 4
    query.restype = wintypes.BOOL
    values = [wintypes.FILETIME() for _ in range(4)]
    if not query(wintypes.HANDLE(int(process._handle)), *(ctypes.byref(value) for value in values)):
        raise ctypes.WinError(ctypes.get_last_error())
    return sum((value.dwHighDateTime << 32) | value.dwLowDateTime for value in values[2:]) / 1e7


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--rom', type=Path, action='append', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--steps', type=int, default=1200000000)
    parser.add_argument('--ratio', type=int, default=2, choices=range(1, 17))
    parser.add_argument('--timing', choices=('legacy', 'auto'), default='legacy')
    parser.add_argument('--repeat', type=int, default=2)
    parser.add_argument('--input-script', type=Path)
    parser.add_argument('--nvram', type=Path)
    args = parser.parse_args()
    if args.steps < 2 or args.repeat < 1:
        parser.error('steps must exceed one and repeat must be positive')
    args.exe = args.exe.resolve(strict=True)
    args.rom = [path.resolve(strict=True) for path in args.rom]
    for name in ('input_script', 'nvram'):
        path = getattr(args, name)
        if path:
            setattr(args, name, path.resolve(strict=True))
    args.output = args.output.resolve()
    args.output.mkdir(parents=True, exist_ok=False)
    rows = []
    for rom in args.rom:
        for repeat in range(args.repeat):
            pair = []
            # Reverse order on alternate repetitions to reduce order bias.
            for batch in ((1, 4096) if repeat % 2 == 0 else (4096, 1)):
                directory = args.output / rom.stem / f'{repeat}-{batch}'
                directory.mkdir(parents=True)
                command = [str(args.exe), str(rom), '--steps', str(args.steps),
                           '--timing', args.timing, '--execution-batch', str(batch),
                           '--snapshot-interval', str(args.steps - 1)]
                if args.timing == 'legacy':
                    command += ['--cpu-steps-per-tick', str(args.ratio)]
                if args.input_script:
                    command += ['--input-script', str(args.input_script)]
                if args.nvram:
                    (directory / 'board.nvram').write_bytes(args.nvram.read_bytes())
                    command += ['--nvram', str(directory / 'board.nvram')]
                started = time.monotonic()
                with (directory / 'run.log').open('w', encoding='utf-8') as log:
                    with subprocess.Popen(command, cwd=directory, stdout=log, stderr=subprocess.STDOUT) as process:
                        try:
                            exit_code = process.wait(timeout=600)
                        except subprocess.TimeoutExpired:
                            process.kill()
                            process.wait()
                            raise
                        cpu_seconds = process_seconds(process)
                elapsed = time.monotonic() - started
                if exit_code:
                    raise RuntimeError(f'{rom.name}: batch {batch} exited {exit_code}')
                text = (directory / 'run.log').read_text(encoding='utf-8', errors='replace')
                final_cpu = text.split('[OceanBlast] Execution finished after ', 1)[1].split(
                    '--- [Linux Kernel dmesg / Log Buffer] ---', 1)[0]
                row = dict(rom=rom.name, repeat=repeat, batch=batch, timing=args.timing,
                           ratio=args.ratio if args.timing == 'legacy' else 1,
                           steps=args.steps, seconds=round(elapsed, 4),
                           mips=round(args.steps / elapsed / 1e6, 3),
                           cpu_seconds=round(cpu_seconds, 4) if cpu_seconds is not None else '',
                           executable_sha256=digest(args.exe), rom_sha256=digest(rom),
                           sdram_sha256=digest(directory / 'sdram.bin'),
                           state_sha256=digest(directory / f'snapshot_{args.steps-1}.txt'),
                           final_cpu_sha256=hashlib.sha256(final_cpu.encode()).hexdigest(),
                           framebuffer_sha256=digest(directory / 'fb_active.raw'))
                pair.append(row)
                print(f'{rom.name}: batch={batch} {elapsed:.2f}s {row["mips"]} MIPS', flush=True)
            same = all(pair[0][key] == pair[1][key] for key in
                       ('sdram_sha256', 'state_sha256', 'final_cpu_sha256', 'framebuffer_sha256'))
            for row in pair:
                row['identical_guest_state'] = same
                rows.append(row)
            with (args.output / 'results.csv').open('w', newline='', encoding='utf-8') as output:
                writer = csv.DictWriter(output, fieldnames=list(rows[0]))
                writer.writeheader()
                writer.writerows(rows)
            if not same:
                raise RuntimeError(f'{rom.name}: scalar and batched guest state differ')


if __name__ == '__main__':
    main()
