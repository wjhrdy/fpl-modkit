"""flip_core against the screenflip reference semantics (host C, no firmware)."""
import ctypes as ct
import pathlib
import shutil
import subprocess
import tempfile
import unittest

HERE = pathlib.Path(__file__).resolve().parent
SOURCE = HERE / 'flip_core.c'

U32 = ct.CFUNCTYPE(ct.c_uint32, ct.c_void_p)
VOID_U32 = ct.CFUNCTYPE(None, ct.c_void_p, ct.c_uint32)


class Panel(ct.Structure):
    _fields_ = [('layer_read', U32), ('layer_write', VOID_U32),
                ('touch_read', U32), ('touch_write', VOID_U32),
                ('touch_supported', U32), ('relayout', ct.CFUNCTYPE(None, ct.c_void_p))]


class Mode(ct.Structure):
    _fields_ = [('display', U32), ('cinema', U32), ('style', U32), ('editing', U32)]


class Ops(ct.Structure):
    _fields_ = [('panel', Panel), ('mode', Mode),
                ('recording', ct.CFUNCTYPE(ct.c_uint32, ct.c_void_p)),
                ('store', ct.CFUNCTYPE(ct.c_uint32, ct.c_void_p, ct.POINTER(ct.c_uint8))),
                ('queue', ct.CFUNCTYPE(None, ct.c_void_p, ct.c_uint32)),
                ('user', ct.c_void_p)]


class State(ct.Structure):
    _fields_ = [('table', ct.c_uint8 * 8), ('saved', ct.c_uint8 * 8),
                ('live_key', ct.c_uint32), ('edit_key', ct.c_uint32),
                ('last_apply', ct.c_uint32), ('save_pending', ct.c_uint32),
                ('polls', ct.c_uint32), ('switches', ct.c_uint32),
                ('applies', ct.c_uint32), ('refused', ct.c_uint32),
                ('pushes', ct.c_uint32), ('edits', ct.c_uint32),
                ('saves', ct.c_uint32), ('save_failed', ct.c_uint32)]


def build():
    directory = pathlib.Path(tempfile.mkdtemp(prefix='flip-core-'))
    out = directory / 'core.dylib'
    subprocess.run([shutil.which('clang') or 'clang', '-shared', '-fPIC', '-O1',
                    '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-I', str(HERE), str(SOURCE), '-o', str(out)],
                   check=True, capture_output=True, text=True)
    lib = ct.CDLL(str(out))
    lib.fpf_state_init.argtypes = [ct.POINTER(State)]
    lib.fpf_want.argtypes = [ct.c_uint32]
    lib.fpf_want.restype = ct.c_uint32
    lib.fpf_touch.argtypes = [ct.c_uint32]
    lib.fpf_touch.restype = ct.c_uint32
    lib.fpf_apply.argtypes = [ct.POINTER(Panel), ct.c_void_p, ct.c_uint32]
    lib.fpf_apply.restype = ct.c_uint32
    lib.fpf_set_in_use.argtypes = [ct.c_uint32, ct.c_uint32]
    lib.fpf_set_in_use.restype = ct.c_uint32
    lib.fpf_key.argtypes = [ct.c_uint32, ct.c_uint32]
    lib.fpf_key.restype = ct.c_uint32
    lib.fpf_editing_key.argtypes = [ct.c_uint32, ct.c_uint32]
    lib.fpf_editing_key.restype = ct.c_uint32
    lib.fpf_changed.argtypes = [ct.POINTER(State), ct.c_uint32, ct.c_uint32]
    lib.fpf_changed.restype = ct.c_uint32
    lib.fpf_poll.argtypes = [ct.POINTER(State), ct.POINTER(Ops)]
    lib.fpf_unsaved.argtypes = [ct.POINTER(State)]
    lib.fpf_unsaved.restype = ct.c_uint32
    lib.fpf_record_check.argtypes = [ct.c_uint32, ct.c_uint32]
    lib.fpf_record_check.restype = ct.c_uint32
    lib.fpf_record_decode.argtypes = [ct.POINTER(ct.c_uint32), ct.POINTER(ct.c_uint8)]
    lib.fpf_record_decode.restype = ct.c_uint32
    lib.fpf_record_encode.argtypes = [ct.POINTER(ct.c_uint8), ct.POINTER(ct.c_uint32)]
    return lib, directory


_LIB = None


def lib():
    global _LIB
    if _LIB is None:
        _LIB, _DIR = build()
    return _LIB


class Fixture:
    """Panel, mode and callbacks the core sees as firmware/platform operations."""

    def __init__(self):
        L = lib()
        self.lib = L
        self.layer = 0
        self.touch = 0
        self.touch_ok = 1
        self.relayouts = 0
        self.display = 0
        self.cinema = 0
        self.style = 0
        self.editing = 0xFFFFFFFF
        self.recording_flag = 0
        self.store_result = 1
        self.stores = []
        self.queued = []
        self._keep = []
        def keep(fn):
            self._keep.append(fn)
            return fn
        self.panel = Panel(
            keep(U32(lambda _u: self.layer)),
            keep(VOID_U32(lambda _u, v: setattr(self, 'layer', v))),
            keep(U32(lambda _u: self.touch)),
            keep(VOID_U32(lambda _u, v: setattr(self, 'touch', v))),
            keep(U32(lambda _u: self.touch_ok)),
            keep(ct.CFUNCTYPE(None, ct.c_void_p)(
                lambda _u: setattr(self, 'relayouts', self.relayouts + 1))))
        self.mode = Mode(
            keep(U32(lambda _u: self.display)),
            keep(U32(lambda _u: self.cinema)),
            keep(U32(lambda _u: self.style)),
            keep(U32(lambda _u: self.editing)))
        def store(_user, table):
            self.stores.append([table[n] for n in range(8)])
            return self.store_result
        self.ops = Ops(self.panel, self.mode,
                       keep(ct.CFUNCTYPE(ct.c_uint32, ct.c_void_p)(lambda _user: self.recording_flag)),
                       keep(ct.CFUNCTYPE(ct.c_uint32, ct.c_void_p, ct.POINTER(ct.c_uint8))(store)),
                       keep(ct.CFUNCTYPE(None, ct.c_void_p, ct.c_uint32)(
                           lambda _user, value: self.queued.append(value))),
                       None)
        self.state = State()
        L.fpf_state_init(ct.byref(self.state))

    def poll(self, n=1):
        for _ in range(n):
            self.lib.fpf_poll(ct.byref(self.state), ct.byref(self.ops))


class CoreTests(unittest.TestCase):
    def test_want_and_touch(self):
        L = lib()
        self.assertEqual({v: L.fpf_want(v) for v in range(5)},
                         {0: 0x000000, 1: 0x333000, 2: 0x001000, 3: 0x332000, 4: 0x000000})
        self.assertEqual([L.fpf_touch(v) for v in range(5)], [0, 1, 0, 1, 0])

    def test_apply_written_preserves_other_layers(self):
        f = Fixture()
        f.layer = 0x12345000
        self.assertEqual(f.lib.fpf_apply(ct.byref(f.panel), None, 1), 1)
        self.assertEqual(f.layer, 0x12333000)
        self.assertEqual(f.touch, 1)
        self.assertEqual(f.relayouts, 1)

    def test_apply_is_idempotent(self):
        f = Fixture()
        f.layer = 0x333000
        f.touch = 1
        self.assertEqual(f.lib.fpf_apply(ct.byref(f.panel), None, 1), 0)
        self.assertEqual(f.relayouts, 0)

    def test_mirror_leaves_touch_upright(self):
        f = Fixture()
        self.assertEqual(f.lib.fpf_apply(ct.byref(f.panel), None, 2), 1)
        self.assertEqual(f.layer, 0x001000)
        self.assertEqual(f.touch, 0)

    def test_touch_unknown_refuses_a_turn(self):
        f = Fixture()
        f.touch_ok = 0
        self.assertEqual(f.lib.fpf_apply(ct.byref(f.panel), None, 1), 2)
        self.assertEqual(f.layer, 0)
        self.assertEqual(f.touch, 0)
        self.assertEqual(f.relayouts, 0)

    def test_set_and_editing_keys(self):
        L = lib()
        self.assertEqual([L.fpf_set_in_use(c, s) for c, s in
                          ((0, 0), (0, 1), (1, 0), (1, 1), (2, 0))], [0, 0, 1, 0, 1])
        self.assertEqual(L.fpf_key(0, 3), 3)
        self.assertEqual(L.fpf_key(1, 0), 4)
        self.assertEqual(L.fpf_key(1, 4), 0x101)
        self.assertEqual(L.fpf_editing_key(0, 0), 0)
        self.assertEqual(L.fpf_editing_key(3, 1), 7)
        self.assertEqual(L.fpf_editing_key(4, 1), 0xFFFFFFFF)

    def test_changed_only_records_a_valid_edit(self):
        f = Fixture()
        f.lib.fpf_changed(ct.byref(f.state), 5, 2)
        self.assertEqual(f.state.table[5], 2)
        self.assertEqual(f.state.edits, 1)
        f.lib.fpf_changed(ct.byref(f.state), 5, 2)
        f.lib.fpf_changed(ct.byref(f.state), 5, 4)
        f.lib.fpf_changed(ct.byref(f.state), 8, 1)
        self.assertEqual(f.state.table[5], 2)
        self.assertEqual(f.state.edits, 1)

    def test_boot_applies_and_a_switch_applies_once(self):
        f = Fixture()
        f.state.table[0] = 1
        f.state.saved[0] = 1
        f.poll()
        self.assertEqual((f.layer, f.state.switches, f.state.applies), (0x333000, 1, 1))
        f.poll()
        self.assertEqual(f.state.switches, 1)
        f.display = 1
        f.poll()
        self.assertEqual(f.layer, 0)
        self.assertEqual((f.state.switches, f.state.applies), (2, 2))

    def test_lcd_off_applies_nothing(self):
        f = Fixture()
        f.state.table[4] = 1
        f.display = 4
        f.poll()
        self.assertEqual(f.layer, 0)
        self.assertEqual(f.state.last_apply, 0)
        self.assertEqual(f.state.applies, 0)

    def test_editing_change_queues_the_custom(self):
        f = Fixture()
        f.state.table[0], f.state.table[1] = 1, 2
        f.editing = 0
        f.poll()
        self.assertEqual(f.queued, [1])
        f.poll()
        self.assertEqual(f.queued, [1])
        f.editing = 1
        f.poll()
        self.assertEqual(f.queued, [1, 2])
        f.editing = 0xFFFFFFFF
        f.poll()
        self.assertEqual(f.queued, [1, 2])

    def test_save_waits_one_second_of_polls_and_recording(self):
        f = Fixture()
        f.state.table[0] = 1
        f.recording_flag = 1
        f.poll(30)
        self.assertEqual(f.stores, [])
        f.recording_flag = 0
        f.poll(19)
        self.assertEqual(f.stores, [])
        f.poll(1)
        self.assertEqual(f.stores, [[1, 0, 0, 0, 0, 0, 0, 0]])
        self.assertEqual(f.state.saves, 1)
        self.assertEqual(f.state.saved[0], 1)
        self.assertEqual(f.lib.fpf_unsaved(ct.byref(f.state)), 0)

    def test_save_failure_keeps_the_table_dirty_and_retries(self):
        f = Fixture()
        f.state.table[0] = 3
        f.store_result = 0
        f.poll(20)
        self.assertEqual((f.state.saves, f.state.save_failed), (0, 1))
        self.assertEqual(f.state.saved[0], 0)
        f.store_result = 1
        f.poll(20)
        self.assertEqual((f.state.saves, f.state.save_failed), (1, 1))
        self.assertEqual(f.state.saved[0], 3)

    def test_record_roundtrip_and_corruption(self):
        L = lib()
        table = (ct.c_uint8 * 8)(0, 1, 2, 3, 3, 2, 1, 0)
        record = (ct.c_uint32 * 5)()
        L.fpf_record_encode(table, record)
        self.assertEqual(record[0], 0x54534646)
        self.assertEqual(record[1], 1)
        self.assertEqual(record[4], L.fpf_record_check(record[2], record[3]))
        out = (ct.c_uint8 * 8)()
        self.assertEqual(L.fpf_record_decode(record, out), 1)
        self.assertEqual(list(out), list(table))
        record[4] ^= 1
        self.assertEqual(L.fpf_record_decode(record, out), 0)
        self.assertEqual(list(out), [0] * 8)
        bad = (ct.c_uint32 * 5)(0x54534646, 1, 0x00000004, 0, 0)
        bad[4] = L.fpf_record_check(bad[2], bad[3])
        self.assertEqual(L.fpf_record_decode(bad, out), 0)


if __name__ == '__main__':
    unittest.main()
