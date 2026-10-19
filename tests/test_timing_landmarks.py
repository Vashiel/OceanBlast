import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location('timing_landmarks', Path(__file__).resolve().parents[1] / 'tools/compare_timing_landmarks.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class TimingLandmarks(unittest.TestCase):
    def setUp(self):
        self.reference = {'platform': 'digiBLAST', 'kind': 'hardware_video', 'source_url': 'https://example.invalid/reference',
                          'reference_uncertainty_seconds': 0.02, 'modeled_uncertainty_ticks': 20000, 'modeled_ticks_per_second': 20000000}
        self.events = [{'event': str(i), 'reference_seconds': 100 + i * 10, 'modeled_ticks': 40000000 + i * 250000000} for i in range(3)]

    def test_spans_ignore_boot_offsets_and_do_not_choose_presets(self):
        result = module.compare(self.reference, self.events)
        self.assertEqual([r['modeled_per_reference'] for r in result['intervals']], [1.25, 1.25])
        shifted = [{**e, 'reference_seconds': e['reference_seconds'] + 37, 'modeled_ticks': e['modeled_ticks'] + 123456789} for e in self.events]
        self.assertEqual(result['intervals'], module.compare(self.reference, shifted)['intervals'])
        self.assertFalse(result['hardware_timing_calibrated'])
        self.assertNotIn('cpu_ratio', result)
        for interval in result['intervals']:
            self.assertLess(interval['ratio_lower_bound'], 1.25)
            self.assertGreater(interval['ratio_upper_bound'], 1.25)

    def test_rejects_other_platforms_and_asset_conversions(self):
        for changed in ({'platform': 'GBA'}, {'kind': 'converted_soundtrack'}, {'source_url': ''}):
            with self.assertRaises(ValueError):
                module.compare({**self.reference, **changed}, self.events)

    def test_modeled_units_are_explicit_and_not_assumed_from_host_speed(self):
        result = module.compare({**self.reference, 'modeled_ticks_per_second': 10000000}, self.events)
        self.assertEqual(result['intervals'][0]['modeled_per_reference'], 2.5)
        with self.assertRaises(ValueError):
            module.compare({**self.reference, 'modeled_ticks_per_second': 0}, self.events)

    def test_rejects_invalid_or_ambiguous_markers(self):
        for changed in ({'reference_seconds': 'nan'}, {'reference_seconds': 99}, {'modeled_ticks': -1}, {'event': '0'}):
            with self.assertRaises(ValueError):
                module.compare(self.reference, [self.events[0], {**self.events[1], **changed}, self.events[2]])
        with self.assertRaises(ValueError):
            module.compare(self.reference, self.events[:2])
        with self.assertRaises(ValueError):
            module.compare({**self.reference, 'reference_uncertainty_seconds': 5}, self.events)


if __name__ == '__main__':
    unittest.main()
