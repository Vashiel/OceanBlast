"""Compare bounded GUI/audio replays with identical guest input and EEPROM state."""
import argparse
import csv
import hashlib
import os
from pathlib import Path
import subprocess
import time
from compare_execution import process_seconds


def digest(path):
    with path.open('rb') as source:
        return hashlib.file_digest(source, 'sha256').hexdigest()


def write_results(rows, path):
    comparable = {row['variant'] for row in rows} == {'baseline', 'candidate'}
    same = all(rows[0][key] == row[key] for row in rows[1:] for key in
               ('sdram_sha256', 'framebuffer_sha256', 'final_cpu_sha256'))
    for row in rows:
        row['identical_guest_state'] = same if comparable else 'pending'
    with path.open('w', newline='', encoding='utf-8') as output:
        writer = csv.DictWriter(output, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)
    return same


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
    parser.add_argument('--repeat', type=int, default=1)
    parser.add_argument('--unpaced', action='store_true', help='Disable host pacing to measure throughput')
    parser.add_argument('--host-cost-start', type=int, help='Measure a fixed scene after this instruction step')
    parser.add_argument('--baseline-batch', type=int, default=4096)
    parser.add_argument('--candidate-batch', type=int, default=4096)
    args = parser.parse_args()
    if args.steps < 2 or args.repeat < 1:
        parser.error('steps must exceed one and repeat must be positive')
    if args.host_cost_start is not None and not 0 <= args.host_cost_start < args.steps:
        parser.error('host-cost-start must precede the step limit')
    if min(args.baseline_batch, args.candidate_batch) < 1:
        parser.error('execution batches must be positive')
    for name in ('exe', 'baseline_exe', 'rom', 'input_script', 'nvram'):
        setattr(args, name, getattr(args, name).resolve(strict=True))
    args.output = args.output.resolve()
    args.output.mkdir(parents=True, exist_ok=False)
    rows = []
    runs = []
    for repeat in range(args.repeat):
        pair = [('baseline', args.baseline_exe), ('candidate', args.exe)]
        if repeat % 2:
            pair.reverse()
        runs.extend((repeat, variant, executable) for variant, executable in pair)
    for repeat, variant, executable in runs:
        directory = args.output / (variant if args.repeat == 1 else f'{repeat}-{variant}')
        directory.mkdir()
        image = directory / 'board.nvram'
        image.write_bytes(args.nvram.read_bytes())
        command = [str(executable), str(args.rom), '--gui', '--sound', '--profile',
                   '--steps', str(args.steps), '--cpu-steps-per-tick', str(args.ratio),
                   '--host-wait', 'timer', '--exit-on-limit', '--snapshot-interval',
                   str(args.steps - 1), '--nvram', str(image), '--input-script', str(args.input_script)]
        batch = args.baseline_batch if variant == 'baseline' else args.candidate_batch
        command += ['--execution-batch', str(batch)]
        if args.unpaced:
            command += ['--clock-mips', '0']
        if args.host_cost_start is not None:
            command += ['--host-cost-profile', str(args.host_cost_start)]
        # Use fresh defaults rather than inheriting the user's live preferences.
        environment = os.environ.copy()
        environment['OCEANBLAST_SETTINGS_PATH'] = str(directory / 'settings.ini')
        started = time.monotonic()
        with (directory / 'run.log').open('w', encoding='utf-8') as log:
            with subprocess.Popen(command, cwd=directory, stdout=log,
                                  stderr=subprocess.STDOUT, env=environment) as process:
                try:
                    exit_code = process.wait(timeout=600)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
                    raise
                cpu_seconds = process_seconds(process)
        if exit_code:
            raise RuntimeError(f'{variant} exited {exit_code}')
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
        row = dict(rom=args.rom.name, repeat=repeat, variant=variant, ratio=args.ratio, steps=args.steps,
                   batch=batch, host_pacing=not args.unpaced,
                   cpu_seconds=round(cpu_seconds, 4) if cpu_seconds is not None else '',
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
        if args.host_cost_start is not None:
            costs = list(csv.DictReader((directory / 'host_cost.csv').open(encoding='utf-8')))
            if len(costs) != 1:
                raise RuntimeError('expected one host-cost interval')
            row.update({f'scene_{key}': value for key, value in costs[0].items()})
            row['scene_mips'] = round((args.steps - args.host_cost_start) /
                                      float(costs[0]['host_total_ms']) / 1000, 4)
        rows.append(row)
        if not write_results(rows, args.output / 'results.csv'):
            raise RuntimeError('GUI replays differ in guest state')
        print(f'{variant}: {mips:.2f} MIPS; {row["changed_per_second"]} sampled changes/s; '
              f'{row["audio_empty_queue_events"]} empty queues', flush=True)
        if args.host_cost_start is not None:
            print(f'  fixed scene: {row["scene_mips"]:.2f} MIPS; '
                  f'{row["scene_ui_excluding_wait_ms"]} ms GUI time', flush=True)


if __name__ == '__main__':
    main()
