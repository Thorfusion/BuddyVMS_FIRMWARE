import sys
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from bms_protocol import BmsStatus, frame


class CodecTests(unittest.TestCase):
    def test_known_frame(self):
        status = BmsStatus(1, 19, 24, 7920, 3290, 3310, 200, 250, 100, 500)
        self.assertEqual(status.encode(), "$BMS,1,1,19,24,7920,3290,3310,0,200,250,100,500*4EDB#")

    def test_negative_current(self):
        status = BmsStatus(65535, 18, 24, 7920, 3290, 3310, -100, 250, -1000, 500)
        self.assertIn(",-100,250,-1000,500*", status.encode())

    def test_unknown_soc(self):
        self.assertIn(",65535*", BmsStatus(1, 19, 24, 7920, 3290, 3310, 200, 250, 0, 65535).encode())

    def test_invalid_frame_characters(self):
        with self.assertRaises(ValueError):
            frame("BMS,1,invalid#")

    def test_no_permission_by_default(self):
        status = BmsStatus(1, 16, 24, 7920, 3290, 3310, 200, 250, 0, 500)
        self.assertIn("BMS,1,1,16,", status.encode())

    def test_invalid_pack(self):
        with self.assertRaises(ValueError):
            BmsStatus(1, 19, 24, 1000, 3290, 3310, 200, 250, 0, 500).encode()

    def test_unknown_flag(self):
        with self.assertRaises(ValueError):
            BmsStatus(1, 32, 24, 7920, 3290, 3310, 200, 250, 0, 500).encode()


if __name__ == "__main__":
    unittest.main()
