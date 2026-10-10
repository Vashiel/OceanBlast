"""Summarize fixed modeled-time intervals from scene_work_probe output."""
import argparse
import collections
import csv
import json
from pathlib import Path

TICKS_PER_SECOND = 20_000_000


def rows(directory, name):
    with (directory / name).open(encoding='utf-8', newline='') as stream:
        return list(csv.DictReader(stream))


def summarize(directory, start, end):
    lower, upper = start * TICKS_PER_SECOND, end * TICKS_PER_SECOND
    timing = {int(row['ticks']): row for row in rows(directory, 'timing.csv')}
    if lower not in timing or upper not in timing or start >= end:
        raise ValueError('start and end must be recorded, increasing modeled seconds')
    selected = [timing[t] for t in sorted(timing) if lower < t <= upper]
    if len(selected) != end-start:
        raise ValueError('missing one-second timing intervals')
    first, last = timing[lower], timing[upper]
    counters = ['steps', 'timer4_expirations', 'timer4_requests', 'timer4_already_pending',
                'timer4_irq_entries', 'timer4_source_clears', 'timer4_selected_clears', 'lcd_irq_entries']
    delta = {name: int(last[name])-int(first[name]) for name in counters}
    jiffies = [int(row['jiffies_delta']) for row in selected if row['jiffies_delta']]
    intervals = [r for r in rows(directory, 'intervals.csv') if lower < int(r['ticks']) <= upper]
    observations = sum(int(r['sampled_instructions']) for r in intervals)
    pcs = collections.Counter()
    for row in rows(directory, 'pcs.csv'):
        if lower < int(row['ticks']) <= upper:
            pcs[(row['ttb'], row['pc'])] += int(row['samples'])
    mmio = collections.defaultdict(lambda: [0, 0])
    for row in rows(directory, 'mmio.csv'):
        if lower < int(row['ticks']) <= upper:
            target = mmio[row['physical_address']]
            target[0] += int(row['guest_reads'])
            target[1] += int(row['guest_writes'])
    ioctls = collections.Counter()
    for row in rows(directory, 'syscalls.csv'):
        if row['number'] == '54' and lower < int(row['ticks']) <= upper:
            ioctls[row['r1']] += 1
    return dict(start_modeled_second=start, end_modeled_second=end,
                host_elapsed_seconds=float(last['host_seconds'])-float(first['host_seconds']),
                counter_deltas=delta, jiffies_intervals=len(jiffies),
                jiffies_delta_min=min(jiffies) if jiffies else None,
                jiffies_delta_max=max(jiffies) if jiffies else None,
                framebuffer_changes=sum(int(r['framebuffer_changes']) for r in intervals),
                total_pc_samples=observations,
                user_pc_samples=sum(int(r['user_samples']) for r in intervals),
                top_pcs=[dict(ttb=key[0], pc=key[1], samples=count,
                              sample_percent=100*count/observations if observations else None)
                         for key, count in pcs.most_common(10)],
                mmio={key: dict(reads=value[0], writes=value[1]) for key, value in sorted(mmio.items())},
                ioctl_commands=dict(sorted(ioctls.items())))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    parser.add_argument('--start', type=int, required=True)
    parser.add_argument('--end', type=int, required=True)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    try:
        result = summarize(args.directory, args.start, args.end)
    except (OSError, ValueError, KeyError) as error:
        parser.error(str(error))
    text = json.dumps(result, indent=2) + '\n'
    if args.output:
        args.output.write_text(text, encoding='utf-8')
    print(text, end='')


if __name__ == '__main__':
    main()
