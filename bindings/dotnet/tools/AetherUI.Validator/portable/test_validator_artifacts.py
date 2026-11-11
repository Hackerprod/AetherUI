import unittest
from validator_artifacts import compare_rgba

class PixelComparisonTests(unittest.TestCase):
    def test_identical(self):
        pixels = bytes([0, 128, 255, 255] * 2)
        result = compare_rgba(2, 1, pixels, pixels)
        self.assertEqual(result['different_pixels'], 0)
        self.assertEqual(result['mae'], 0)
        self.assertEqual(result['rmse'], 0)
    def test_one_channel(self):
        result = compare_rgba(1, 1, bytes([0, 0, 0, 255]), bytes([12, 0, 0, 255]))
        self.assertEqual(result['mae'], 3)
        self.assertEqual(result['rmse'], 6)
        self.assertEqual(result['different_fraction'], 1)
    def test_tolerance_is_strictly_greater(self):
        r, c = bytes([0, 0, 0, 255]), bytes([10, 0, 0, 255])
        self.assertEqual(compare_rgba(1, 1, r, c, 10)['different_pixels'], 0)
        self.assertEqual(compare_rgba(1, 1, r, c, 9)['different_pixels'], 1)
    def test_alpha_is_compared(self):
        result = compare_rgba(1, 1, bytes([0, 0, 0, 0]), bytes([0, 0, 0, 255]))
        self.assertEqual(result['different_pixels'], 1)
        self.assertEqual(result['diff_bytes'], bytes([255, 0, 255, 255]))
    def test_bad_buffer(self):
        with self.assertRaises(ValueError): compare_rgba(2, 1, b'1234', b'1234')
    def test_invalid_tolerance(self):
        for tolerance in [-1, 256]:
            with self.assertRaises(ValueError): compare_rgba(1, 1, b'1234', b'1234', tolerance)
    def test_empty_dimension(self):
        with self.assertRaises(ValueError): compare_rgba(0, 1, b'', b'')
    def test_fraction_denominator_is_pixels(self):
        result = compare_rgba(2, 1, bytes(8), bytes([0, 1, 0, 0, 0, 0, 0, 0]))
        self.assertEqual(result['different_fraction'], .5)

if __name__ == '__main__': unittest.main()
