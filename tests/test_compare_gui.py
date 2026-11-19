"""Check comparison acceptance failures without running a cartridge or GUI."""
import csv
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


TOOL = Path(__file__).resolve().parents[1] / 'tools' / 'compare_gui.py'
FAKE_ROM = r'''
from pathlib import Path
import sys
import time
args = sys.argv
steps = int(args[args.index('--steps') + 1])
{setup}
Path('performance.csv').write_text(
    'steps,seconds,changed_fps\n0,1,1\n10,1,1\n')
Path(f'snapshot_{{steps-1}}.txt').write_text(
    'audio_empty_queue_events={empty}\ndropped_samples=0\naudio_queued_frames=1024\n')
Path('sdram.bin').write_bytes(b'ram')
Path('fb_active.raw').write_bytes(b'frame')
print(f'[OceanBlast] Execution finished after {{steps}} instructions.')
print('fixed CPU state')
print('--- [Linux Kernel dmesg / Log Buffer] ---')
'''


class CompareGuiAcceptance(unittest.TestCase):
    def run_case(self, setup='', empty=0, timeout=5):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        root = Path(temporary.name)
        rom = root / 'fixture.py'
        rom.write_text(FAKE_ROM.format(setup=setup, empty=empty), encoding='utf-8')
        (root / 'input.txt').write_text('0 0\n')
        (root / 'blank.nvram').write_bytes(bytes(2048))
        output = root / 'result'
        result = subprocess.run(
            [sys.executable, str(TOOL), '--exe', sys.executable,
             '--baseline-exe', sys.executable, '--rom', str(rom),
             '--input-script', str(root / 'input.txt'), '--nvram', str(root / 'blank.nvram'),
             '--output', str(output), '--steps', '20', '--ratio', '3',
             '--timeout', str(timeout), '--max-empty-queues', '0', '--max-dropped-samples', '0'],
            capture_output=True, text=True, timeout=15)
        return result, output

    def test_matching_pair_is_accepted(self):
        result, output = self.run_case()
        self.assertEqual(result.returncode, 0, result.stderr)
        with (output / 'results.csv').open() as source:
            rows = list(csv.DictReader(source))
        self.assertEqual(len(rows), 2)
        self.assertTrue(all(row['identical_guest_state'] == 'True' for row in rows))

    def test_early_close_prevents_candidate(self):
        result, output = self.run_case(setup='steps -= 1')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('did not reach the requested instruction limit', result.stderr)
        self.assertFalse((output / 'candidate').exists())

    def test_audio_failure_prevents_candidate(self):
        result, output = self.run_case(empty=1)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('audio_empty_queue_events=1 exceeds 0', result.stderr)
        self.assertFalse((output / 'candidate').exists())

    def test_timeout_prevents_candidate(self):
        result, output = self.run_case(setup='time.sleep(2)', timeout=0.1)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('TimeoutExpired', result.stderr)
        self.assertFalse((output / 'candidate').exists())


if __name__ == '__main__':
    unittest.main()
