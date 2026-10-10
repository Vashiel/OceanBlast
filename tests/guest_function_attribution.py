"""ROM-free checks for function bounds and captured-code verification."""
from pathlib import Path
import struct
import sys
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from attribute_guest_functions import attribute, functions


def fixture():
    data = bytearray(320)
    identity = b'\x7fELF\x01\x01' + bytes(10)
    struct.pack_into('<16sHHIIIIIHHHHHH', data, 0,
                     identity, 3, 40, 1, 0, 0, 52, 0, 52, 32, 0, 40, 4, 0)
    # Null section, executable bytes, a symbol table, its string table.
    struct.pack_into('<IIIIIIIIII', data, 92, 0, 1, 6, 0x100, 240, 4, 0, 0, 4, 0)
    struct.pack_into('<IIIIIIIIII', data, 132, 0, 2, 0, 0, 256, 32, 3, 0, 4, 16)
    struct.pack_into('<IIIIIIIIII', data, 172, 0, 3, 0, 0, 288, 5, 0, 0, 1, 0)
    data[240:244] = b'abcd'
    struct.pack_into('<IIIBBH', data, 272, 1, 0x101, 4, 2, 0, 1)
    data[288:293] = b'\0foo\0'
    return bytes(data)


class Capture:
    def __init__(self, code=b'abcd'):
        self.code = code

    def read(self, address, size):
        if self.code is None:
            raise ValueError('not mapped')
        assert (address, size) == (0x1100, 4)
        return self.code


class AttributionTests(unittest.TestCase):
    def rows(self):
        return [dict(ticks='20000000', ttb='0x30004000', pc=pc, samples='3')
                for pc in ('0x1101', '0x1104', '0x1200')]

    def test_thumb_symbol_and_function_end(self):
        symbol = functions(fixture(), 0x1000)[0]
        self.assertEqual((symbol['address'], symbol['file_offset'], symbol['size']), (0x1100, 240, 4))
        result = attribute(fixture(), Capture(), 0x1000, self.rows(), 0, 1, 0x30004000)
        self.assertEqual((result['total_samples'], result['attributed_samples']), (9, 3))
        self.assertEqual(result['functions'][0]['capture_comparison'], 'identical')

    def test_mismatch_and_unmapped_are_explicit(self):
        for code, status in ((b'abce', 'different'), (None, 'unmapped_in_final_capture')):
            result = attribute(fixture(), Capture(code), 0x1000, self.rows(), 0, 1, 0x30004000)
            self.assertEqual(result['functions'][0]['capture_comparison'], status)

    def test_other_address_space_is_not_attributed(self):
        result = attribute(fixture(), Capture(), 0x1000, self.rows(), 0, 1, 0x30008000)
        self.assertEqual((result['total_samples'], result['attributed_samples']), (9, 0))

    def test_wrong_architecture_rejected(self):
        data = bytearray(fixture())
        struct.pack_into('<H', data, 18, 3)
        with self.assertRaises(ValueError):
            functions(data, 0x1000)


if __name__ == '__main__':
    unittest.main()
