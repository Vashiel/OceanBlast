"""Compare bounded GUI/audio replays with identical guest input and EEPROM state."""
import argparse
import csv
import hashlib
from pathlib import Path
import subprocess
import time


def digest(path):
    with path.open('rb') as source:
        return hashlib.file_digest(source, 'sha256').hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--baseline-exe', type=Path, required=True)
    parser.add_argument('--rom', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--steps', type=int, required=True)
    parser.add_argument('--ratio', type=int, choices=range(1, 17), required=True)
    parser.add_argument('--input-script', type=Path, required=True)
    parser.add_argument('--nvram', type=Path, required=True)
    args = parser.parse_args()
    if args.steps < 2:
        parser.error('steps must exceed one')
    for name in ('exe', 'baseline_exe', 'rom', 'input_script', 'nvram'):
        setattr(args, name, getattr(args, name).resolve(strict=True))
    args.output = args.output.resolve()
    args.output.mkdir(parents=True, exist_ok=False)
    rows = []
    for variant, executable in (('baseline', args.baseline_exe), ('candidate', args.exe)):
        directory = args.output / variant
        directory.mkdir()
        image = directory / 'board.nvram'
        image.write_bytes(args.nvram.read_bytes())
        command = [str(executable), str(args.rom), '--gui', '--sound', '--profile',
                   '--steps', str(args.steps), '--cpu-steps-per-tick', str(args.ratio),
                   '--host-wait', 'timer', '--exit-on-limit', '--snapshot-interval',
                   str(args.steps - 1), '--nvram', str(image), '--input-script', str(args.input_script)]
        started = time.monotonic()
        with (directory / 'run.log').open('w', encoding='utf-8') as log:
            process = subprocess.run(command, cwd=directory, stdout=log,
                                     stderr=subprocess.STDOUT, timeout=600)
        if process.returncode:
            raise RuntimeError(f'{variant} exited {process.returncode}')
        elapsed = time.monotonic() - started
        metrics = list(csv.DictReader((directory / 'performance.csv').open(encoding='utf-8')))
        if len(metrics) < 2:
            raise RuntimeError('insufficient performance intervals')
        first, last = metrics[0], metrics[-1]
        # Each seconds field is one reporting interval, not a cumulative clock.
        interval = sum(float(row['seconds']) for row in metrics[1:])
        if interval <= 0 or int(last['steps']) <= int(first['steps']):
            raise RuntimeError('invalid performance interval')
        mips = (int(last['steps']) - int(first['steps'])) / interval / 1e6
        weighted_changes = 0
        for current in metrics[1:]:
            weighted_changes += float(current['changed_fps']) * float(current['seconds'])
        snapshot = dict(line.split('=', 1) for line in
                        (directory / f'snapshot_{args.steps-1}.txt').read_text(encoding='utf-8').splitlines())
        log = (directory / 'run.log').read_text(encoding='utf-8', errors='replace')
        cpu = log.split('[OceanBlast] Execution finished after ', 1)[1].split(
            '--- [Linux Kernel dmesg / Log Buffer] ---', 1)[0]
        row = dict(rom=args.rom.name, variant=variant, ratio=args.ratio, steps=args.steps,
                   elapsed_seconds=round(elapsed, 4), measured_seconds=round(interval, 4),
                   mips=round(mips, 4), changed_per_second=round(weighted_changes / interval, 4),
                   mean_modeled_speed=round(mips / (20 * args.ratio) * 100, 4),
                   audio_empty_queue_events=snapshot['audio_empty_queue_events'],
                   dropped_samples=snapshot['dropped_samples'],
                   audio_queued_frames=snapshot['audio_queued_frames'],
                   executable_sha256=digest(executable), rom_sha256=digest(args.rom),
                   input_sha256=digest(args.input_script), initial_eeprom_sha256=digest(args.nvram),
                   sdram_sha256=digest(directory / 'sdram.bin'),
                   framebuffer_sha256=digest(directory / 'fb_active.raw'),
                   final_cpu_sha256=hashlib.sha256(cpu.encode()).hexdigest())
        rows.append(row)
        print(f'{variant}: {mips:.2f} MIPS; {row["changed_per_second"]} sampled changes/s; '
              f'{row["audio_empty_queue_events"]} empty queues', flush=True)
    same = all(rows[0][key] == rows[1][key] for key in
               ('sdram_sha256', 'framebuffer_sha256', 'final_cpu_sha256'))
    for row in rows:
        row['identical_guest_state'] = same
    with (args.output / 'results.csv').open('w', newline='', encoding='utf-8') as output:
        writer = csv.DictWriter(output, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)
    if not same:
        raise RuntimeError('GUI replays differ in guest state')


if __name__ == '__main__':
    main()
