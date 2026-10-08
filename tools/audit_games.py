"""Bounded headless cartridge audit; observations are not gameplay acceptance."""
import argparse
import concurrent.futures
import csv
import hashlib
import json
from pathlib import Path
import re
import subprocess
import time


def digest(path):
    with path.open('rb') as source:
        return hashlib.file_digest(source, 'sha256').hexdigest()


def run_one(rom, args):
    directory = args.output / rom.stem
    directory.mkdir(parents=True, exist_ok=False)
    started = time.monotonic()
    timed_out = False
    with (directory / 'run.log').open('w', encoding='utf-8') as log:
        try:
            command = [str(args.exe), str(rom), '--steps', str(args.steps)]
            if args.pc_profile:
                command += ['--pc-profile', str(args.pc_profile)]
            if args.snapshots:
                command += ['--snapshot-interval', str(args.snapshots)]
            if args.input_script:
                command += ['--input-script', str(args.input_script)]
            if args.debug:
                command += ['--debug']
            if args.fault_log:
                command += ['--fault-log']
            if args.display_format != 'lcd':
                command += ['--display-format', args.display_format]
            if args.display_stride:
                command += ['--display-stride', str(args.display_stride)]
            completed = subprocess.run(command,
                                       cwd=directory, stdout=log, stderr=subprocess.STDOUT,
                                       timeout=args.timeout)
            exit_code = completed.returncode
        except subprocess.TimeoutExpired:
            timed_out, exit_code = True, None
    text = (directory / 'run.log').read_text(encoding='utf-8', errors='replace')
    def last(pattern):
        matches = re.findall(pattern, text)
        return matches[-1] if matches else ''
    frame = last(r'Active framebuffer nonzero bytes: (\d+/\d+)')
    result = dict(rom=rom.name, sha256=digest(rom), seconds=round(time.monotonic()-started, 2),
                  exit_code=exit_code, timed_out=timed_out,
                  display_format=args.display_format, display_stride=args.display_stride,
                  steps=last(r'Execution finished after (\d+) instructions'),
                  pc=last(r'\[CPU\] PC: (0x[0-9a-f]+)'),
                  framebuffer=last(r'Active framebuffer PA: (0x[0-9a-f]+)'), nonzero_bytes=frame,
                  linux_boot='Linux version 2.6.11' in text,
                  eeprom_error='No I2C adapter attached' in text,
                  invalid_pcm_pointer='res cfcb0000' in text,
                  kernel_panic='Kernel panic' in text,
                  guest_segfaults=text.count('Segmentation fault'),
                  squashfs_error_messages=text.count('SQUASHFS error:'),
                  assessment='headless observation; gameplay/input/audio unverified')
    (directory / 'result.json').write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
    print(f"{rom.name}: PC={result['pc']} frame={frame} timeout={timed_out}", flush=True)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--roms', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--steps', type=int, default=1200000000)
    parser.add_argument('--timeout', type=float, default=180)
    parser.add_argument('--jobs', type=int, default=1)
    parser.add_argument('--match', default='*.bin', help='ROM filename glob')
    parser.add_argument('--pc-profile', type=int, default=0, help='execution-page sampling interval')
    parser.add_argument('--snapshots', type=int, default=0, help='snapshot instruction interval')
    parser.add_argument('--input-script', type=Path)
    parser.add_argument('--debug', action='store_true')
    parser.add_argument('--fault-log', action='store_true')
    parser.add_argument('--display-format', choices=['lcd', 'rgb444', 'rgb565'], default='lcd')
    parser.add_argument('--display-stride', type=int, default=0)
    args = parser.parse_args()
    if args.steps <= 0 or args.timeout <= 0 or args.jobs < 1:
        parser.error('steps, timeout and jobs must be positive')
    args.exe, args.roms, args.output = args.exe.resolve(), args.roms.resolve(), args.output.resolve()
    if args.input_script:
        args.input_script = args.input_script.resolve()
        if not args.input_script.is_file():
            parser.error('input script must exist')
    roms = sorted(path for path in args.roms.glob(args.match) if path.suffix.lower()=='.bin')
    if not args.exe.is_file() or not roms:
        parser.error('executable and at least one .bin cartridge are required')
    args.output.mkdir(parents=True, exist_ok=False)
    metadata = dict(exe=str(args.exe), exe_sha256=digest(args.exe), steps=args.steps,
                    timeout=args.timeout, jobs=args.jobs, sound=False, gui=False, debug=args.debug,
                    snapshots=args.snapshots, pc_profile=args.pc_profile, fault_log=args.fault_log, display_format=args.display_format,
                    display_stride=args.display_stride, input_script=str(args.input_script) if args.input_script else None,
                    input_sha256=digest(args.input_script) if args.input_script else None)
    (args.output / 'audit.json').write_text(json.dumps(metadata, indent=2)+'\n', encoding='utf-8')
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        results = list(pool.map(lambda rom: run_one(rom, args), roms))
    with (args.output / 'results.csv').open('w', newline='', encoding='utf-8') as output:
        writer = csv.DictWriter(output, fieldnames=results[0].keys())
        writer.writeheader()
        writer.writerows(results)


if __name__ == '__main__':
    main()
