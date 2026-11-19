"""fp L profile wiring against the shared core, with fake MMIO/getters."""
import ctypes as ct
import pathlib
import shutil
import subprocess
import tempfile
import unittest

HERE = pathlib.Path(__file__).resolve().parent
CORE = HERE.parents[1] / 'screenflip/core'
VT = 0x00C0CCE65C
NO_KEY = 0xFFFF

_LIB = None


def lib():
    global _LIB
    if _LIB is not None:
        return _LIB
    directory = pathlib.Path(tempfile.mkdtemp(prefix='flip-fpl-profile-'))
    out = directory / 'profile.dylib'
    subprocess.run([shutil.which('clang') or 'clang', '-shared', '-fPIC', '-O1',
                    '-std=c11', '-Wall', '-Wextra', '-Werror', '-DFPLFLIP_HOST_TEST',
                    '-I', str(HERE), '-I', str(CORE),
                    str(CORE / 'flip_core.c'), str(HERE / 'profile.c'),
                    str(HERE / 'host.c'), '-o', str(out)],
                   check=True, capture_output=True, text=True)
    library = ct.CDLL(str(out))
    library.host_reset.argtypes = []
    library.host_init.argtypes = []
    library.host_init.restype = ct.c_uint32
    library.host_changed.argtypes = [ct.c_uint32, ct.c_uint32]
    library.host_set.argtypes = [ct.c_uint32] * 5
    library.host_set_mode.argtypes = [ct.c_uint32] * 3
    library.host_set_store_result.argtypes = [ct.c_uint32]
    for name in ('host_layer', 'host_touch', 'host_relayouts', 'host_stores'):
        getattr(library, name).restype = ct.c_uint32
    library.host_stored.argtypes = [ct.c_uint32]
    library.host_stored.restype = ct.c_uint32
    library.host_queued.argtypes = [ct.c_uint32]
    library.host_queued.restype = ct.c_uint32
    library.host_state.argtypes = [ct.c_uint32]
    library.host_state.restype = ct.c_uint32
    _LIB = library
    return library


class BuildTests(unittest.TestCase):
    def test_arm_objects_build_freestanding(self):
        directory = pathlib.Path(tempfile.mkdtemp(prefix='flip-fpl-arm-'))
        flags = [shutil.which('clang') or 'clang', '-target', 'armv7-none-eabi', '-marm',
                 '-mfloat-abi=soft', '-mfpu=none', '-ffreestanding', '-fno-builtin',
                 '-fno-jump-tables', '-fno-asynchronous-unwind-tables',
                 '-fno-unwind-tables', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                 '-I', str(HERE), '-I', str(CORE)]
        for source in (CORE / 'flip_core.c', HERE / 'profile.c'):
            out = directory / (source.stem + '.o')
            subprocess.run(flags + ['-c', str(source), '-o', str(out)],
                           check=True, capture_output=True, text=True)
            self.assertGreater(out.stat().st_size, 0, source.name)


class ProfileTests(unittest.TestCase):
    def setUp(self):
        self.L = lib()
        self.L.host_reset()
        self.L.host_set(0, 0, VT, NO_KEY, 0)
        self.L.host_set_mode(0, 0, 0)
        self.assertEqual(self.L.host_init(), 0)

    def poll(self, n=1):
        for _ in range(n):
            self.L.host_poll()

    def state(self, field):
        return self.L.host_state(field)

    def test_boot_applies_and_a_switch_applies_the_next_custom(self):
        self.L.host_changed(0, 1)
        self.poll()
        self.assertEqual(self.L.host_layer(), 0)
        self.assertEqual(self.L.host_touch(), 0)
        self.assertEqual(self.state(9), 2)
        self.assertEqual(self.L.host_relayouts(), 0)       # rearm writes the layer
        self.assertEqual((self.state(1), self.state(2)), (1, 1))  # registers directly,
        self.poll()                                        # not the relayout call
        self.assertEqual(self.state(1), 1)
        self.L.host_set_mode(1, 0, 0)
        self.poll()
        self.assertEqual(self.L.host_layer(), 0)
        self.assertEqual((self.state(1), self.state(2)), (2, 2))

    def test_lcd_off_and_shooting_style_use_the_right_set(self):
        self.L.host_changed(0, 1)
        self.L.host_changed(4, 2)
        self.L.host_set_mode(4, 1, 0)
        self.poll()
        self.assertEqual(self.L.host_layer(), 0)
        self.L.host_set_mode(0, 1, 0)
        self.poll()
        self.assertEqual(self.L.host_layer(), 0)          # mirror: software (v15)
        self.L.host_set_mode(0, 1, 1)
        self.poll()
        self.assertEqual(self.state(9), 2)                # STILL set, style 1: 180

    def test_picture_turn_does_not_need_a_touch_binding_at_apply(self):
        self.L.host_changed(0, 1)
        self.L.host_set(0, 0, 0xDEADBEEF, NO_KEY, 0)
        self.poll()
        self.assertEqual(self.L.host_layer(), 0)
        self.assertEqual(self.L.host_relayouts(), 0)
        self.assertEqual((self.state(2), self.state(3)), (1, 0))
        self.assertEqual(self.state(9), 2)

    def test_mirror_never_reflects_the_osd_or_changes_touch_orientation(self):
        self.L.host_set(0x11, 0, VT, NO_KEY, 0)  # unrelated display bits
        for key, mode in enumerate((1, 3, 2, 0)):
            self.L.host_changed(key, mode)
            self.L.host_set_mode(key, 0, 0)
            self.poll()
            self.assertEqual(self.state(9), (0, 2, 1, 3)[mode])
            self.assertEqual(self.L.host_layer(), 0x11)
            self.assertEqual(self.L.host_touch(), 0)

    def test_editing_change_queues_the_edited_custom(self):
        self.L.host_changed(0, 1)
        self.L.host_changed(1, 3)
        self.L.host_set(0, 0, VT, 0, 0)
        self.poll()
        self.assertEqual([self.L.host_queued(0), self.L.host_queued(1)], [1, 0])
        self.L.host_set(0, 0, VT, 1, 0)
        self.poll()
        self.assertEqual(self.L.host_queued(1), 3)
        self.L.host_set(0, 0, VT, NO_KEY, 0)
        self.poll()
        self.assertEqual(self.state(4), 2)

    def test_save_waits_while_recording_and_after_one_second(self):
        self.L.host_changed(0, 3)
        self.L.host_set(0, 0, VT, NO_KEY, 1)
        self.poll(30)
        self.assertEqual(self.L.host_stores(), 0)
        self.L.host_set(0, 0, VT, NO_KEY, 0)
        self.poll(19)
        self.assertEqual(self.L.host_stores(), 0)
        self.poll(1)
        self.assertEqual(self.L.host_stores(), 1)
        self.assertEqual(self.L.host_stored(0), 3)
        self.assertEqual((self.state(6), self.state(7)), (1, 0))

    def test_store_failure_is_counted_and_retried(self):
        self.L.host_changed(0, 2)
        self.L.host_set_store_result(0)
        self.poll(20)
        self.assertEqual((self.state(6), self.state(7)), (0, 1))
        self.L.host_set_store_result(1)
        self.poll(20)
        self.assertEqual((self.state(6), self.state(7)), (1, 1))
        self.assertEqual(self.L.host_stored(0), 2)

    def test_only_final_values_are_saved_after_a_burst_of_edits(self):
        self.L.host_changed(1, 2)
        self.poll(19)
        self.L.host_changed(1, 3)
        self.L.host_changed(6, 1)
        self.poll(19)
        self.assertEqual(self.L.host_stores(), 0)
        self.poll(21)
        self.assertEqual(self.L.host_stores(), 1)
        self.assertEqual([self.L.host_stored(i) for i in range(8)], [0,3,0,0,0,0,1,0])


if __name__ == '__main__':
    unittest.main()
