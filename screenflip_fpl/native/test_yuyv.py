"""Execute one production Thumb line kernel; check each Y/U/V byte and guards."""
import pathlib
import random
import struct
import subprocess
import sys
import tempfile
import unittest

from unicorn import Uc, UC_ARCH_ARM, UC_MODE_THUMB
from unicorn.arm_const import UC_ARM_REG_LR, UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_SP

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parents[1] / 'uishare'))
from build_section import CFLAGS


def run_line(body, src, instruction_hook=None):
    with tempfile.TemporaryDirectory() as tmp:
        obj = pathlib.Path(tmp) / 'line.o'
        subprocess.run(['clang', *CFLAGS, '-I', str(HERE), '-I',
                        str(HERE.parents[1] / 'screenflip/core'), '-x', 'c', '-',
                        '-c', '-o', str(obj)], input='#include "yuyv.h"\n'
                       'void line(unsigned *s, unsigned *d){' + body + '}',
                       text=True, check=True)
        elf = obj.read_bytes()
    # Single exported function with no external calls or literal addresses.
    shoff, = struct.unpack_from('<I', elf, 32)
    size, num, names = struct.unpack_from('<HHH', elf, 46)
    sections = [struct.unpack_from('<10I', elf, shoff + size*i) for i in range(num)]
    strings = sections[names][4]
    text, = [elf[h[4]:h[4]+h[5]] for h in sections
             if elf[strings+h[0]:].startswith(b'.text\0')]
    cpu = Uc(UC_ARCH_ARM, UC_MODE_THUMB)
    cpu.mem_map(0x1000, 0x6000)
    cpu.mem_write(0x1000, text)
    cpu.mem_write(0x3000, bytes([0xee])*4096)
    cpu.mem_write(0x2004, src)
    for reg, value in ((UC_ARM_REG_R0, 0x2004), (UC_ARM_REG_R1, 0x3004),
                       (UC_ARM_REG_SP, 0x7000), (UC_ARM_REG_LR, 0x4001)):
        cpu.reg_write(reg, value)
    if instruction_hook:
        from unicorn import UC_HOOK_CODE
        cpu.hook_add(UC_HOOK_CODE, instruction_hook)
    cpu.emu_start(0x1001, 0x4000, count=30000)
    return bytes(cpu.mem_read(0x3000, len(src)+8))


class PixelTests(unittest.TestCase):
    def test_overlay_half_turn_preserves_index_and_argb_pixels(self):
        src = random.Random(682).randbytes(4*123)
        for indexed, step in ((1, 1), (0, 2)):
            expected = b''.join(src[i:i+step] for i in range(len(src)-step, -1, -step))
            self.assertEqual(run_line(f'turn_osd(s,d,123,{indexed});', src),
                             b'\xee'*4 + expected + b'\xee'*4)

    def test_thumb_line_including_two_pair_tail(self):
        src = random.Random(180).randbytes(3240)
        reversed_line = b''.join(bytes((src[i+2], src[i+1], src[i], src[i+3]))
                                 for i in range(len(src)-4, -1, -4))
        for fn, expected in [('xform_word_rev', reversed_line), ('xform_copy', src)]:
            with self.subTest(fn=fn):
                self.assertEqual(run_line(fn+'(s,d);', src),
                                 b'\xee'*4 + expected + b'\xee'*4)
