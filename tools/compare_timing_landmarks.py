"""Compare marked original-console video events with modeled emulator time.

Input markers must describe the same uninterrupted sequence. Results never
modify CPU presets and do not certify the source recording or hardware timing.
"""
import argparse
import csv
import json
import math
from pathlib import Path

def compare(reference, events):
    if reference.get('platform', '').casefold() != 'digiblast':
        raise ValueError('Reference must show digiBLAST; a GBA port cannot calibrate its timing.')
    if reference.get('kind') != 'hardware_video':
        raise ValueError('Reference must be hardware video, not a soundtrack or converted assets.')
    if not reference.get('source_url'):
        raise ValueError('Reference source URL is required.')
    ticks_per_second = int(reference['modeled_ticks_per_second'])
    if not 0 < ticks_per_second < 2**64:
        raise ValueError('Modeled tick rate must be copied from the emulator manifest.')
    ref_error = float(reference['reference_uncertainty_seconds'])
    model_error = int(reference['modeled_uncertainty_ticks']) / ticks_per_second
    if not math.isfinite(ref_error) or min(ref_error, model_error) < 0:
        raise ValueError('Event-marker uncertainties must be finite and nonnegative.')
    if len(events) < 3:
        raise ValueError('At least three corresponding markers are required.')
    parsed = []
    seen = set()
    for event in events:
        name = event['event']
        seconds = float(event['reference_seconds'])
        ticks = int(event['modeled_ticks'])
        if not name or name in seen or not math.isfinite(seconds) or seconds < 0 or not 0 <= ticks < 2**64:
            raise ValueError('Markers require unique names, finite video times and unsigned 64-bit ticks.')
        if parsed and (seconds <= parsed[-1][1] or ticks <= parsed[-1][2]):
            raise ValueError('Both event timelines must increase strictly.')
        seen.add(name)
        parsed.append((name, seconds, ticks))
    intervals = []
    for before, after in zip(parsed, parsed[1:]):
        ref_span = after[1] - before[1]
        model_span = (after[2] - before[2]) / ticks_per_second
        if ref_span <= 2 * ref_error or model_span <= 2 * model_error:
            raise ValueError('Intervals must exceed the combined marker uncertainty.')
        intervals.append({
            'from': before[0], 'to': after[0], 'reference_seconds': ref_span,
            'modeled_seconds': model_span, 'modeled_per_reference': model_span / ref_span,
            'ratio_lower_bound': (model_span - 2 * model_error) / (ref_span + 2 * ref_error),
            'ratio_upper_bound': (model_span + 2 * model_error) / (ref_span - 2 * ref_error),
        })
    return {
        'status': 'marked-event comparison only', 'hardware_timing_calibrated': False,
        'source': reference, 'ticks_per_second': ticks_per_second, 'intervals': intervals,
        'limits': 'Recording speed, edits, marker identity and capture latency require review. No preset correction is inferred.',
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reference', type=Path, required=True)
    parser.add_argument('--events', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    try:
        reference = json.loads(args.reference.read_text(encoding='utf-8-sig'))
        with args.events.open(encoding='utf-8-sig', newline='') as file:
            result = compare(reference, list(csv.DictReader(file)))
        args.output.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    except (ValueError, KeyError, OSError, TypeError) as error:
        parser.exit(2, str(error) + '\n')
    print(f"Compared {len(result['intervals'])} intervals; no automatic timing calibration.")


if __name__ == '__main__':
    main()
