"""Check the standalone fp L row against the camera-tested payload facts."""
import os
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'uishare'))
import menu
from ui import fpui

MAIN = Path(os.environ.get('FPSUP_FPL_MAIN', Path.home()/'.local/share/fpsup/firmware/FPL_V302/MAIN.bin'))


@unittest.skipUnless(MAIN.is_file(), 'set FPSUP_FPL_MAIN to run firmware tests')
class MenuTests(unittest.TestCase):
    def test_rebuild_and_reject_changed_donor(self):
        image = menu.image(MAIN)
        row, _ = menu.build(image)
        strings, ops, _ = fpui.decode(row)
        self.assertEqual(len(row), 11444)
        self.assertIn(('MV_fpScreenFlip', 0xffffffff), strings)
        self.assertFalse(any(code in (fpui.OP_FILE, fpui.OP_HOOK_FV) for code, _ in ops))
        bad = bytearray(image)
        bad[0x2b4ee03+0xfd40] ^= 1
        with self.assertRaisesRegex(ValueError, 'wrong stock menu donor'):
            menu.build(bad)
