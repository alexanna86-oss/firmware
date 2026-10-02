"""Regression: a failed objcopy must never remove the framework WLAN archive."""
import os
import tempfile
import types
import unittest
from pathlib import Path
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
PATCH_SETUP = (ROOT / 'patch.py').read_text().split('\n\ndef hash_file', 1)[0]

class FakeEnv:
    def __init__(self, root):
        self.root = root
    def PioPlatform(self):
        return self
    def get_package_dir(self, name):
        return str(self.root / ('framework' if name == 'framework-arduinoespressif32-libs' else 'toolchain'))
    def BoardConfig(self):
        return {'build.mcu': 'esp32s3'}
    def Exit(self, code):
        raise SystemExit(code)

class PatchTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.lib = self.root / 'framework/esp32s3/lib'
        self.lib.mkdir(parents=True)
        self.original = self.lib / 'libnet80211.a'
        self.original.write_bytes(b'original archive')
        tool = self.root / 'toolchain/bin' / ('xtensa-esp-elf-objcopy' + ('.exe' if os.name == 'nt' else ''))
        tool.parent.mkdir(parents=True)
        tool.touch()
    def tearDown(self):
        self.temp.cleanup()
    def run_setup(self, success):
        def invoke(args, **kwargs):
            if success:
                Path(args[-1]).write_bytes(b'checked archive')
            return types.SimpleNamespace(returncode=0 if success else 1)
        with patch('subprocess.run', side_effect=invoke):
            exec(compile(PATCH_SETUP, 'patch.py', 'exec'), {'Import': lambda *a: None, 'env': FakeEnv(self.root)})
    def test_failed_tool_preserves_original(self):
        with self.assertRaises(SystemExit):
            self.run_setup(False)
        self.assertEqual(self.original.read_bytes(), b'original archive')
        self.assertFalse((self.lib / '.patched').exists())
    def test_success_keeps_backup_and_marks(self):
        self.run_setup(True)
        self.assertEqual(self.original.read_bytes(), b'checked archive')
        self.assertEqual((self.lib / 'libnet80211.a.old').read_bytes(), b'original archive')
        self.assertTrue((self.lib / '.patched').exists())
    def test_recovers_old_failed_install(self):
        self.original.rename(self.lib / 'libnet80211.a.old')
        (self.lib / '.patched').touch()
        self.run_setup(True)
        self.assertEqual(self.original.read_bytes(), b'checked archive')
        self.assertEqual((self.lib / 'libnet80211.a.old').read_bytes(), b'original archive')

if __name__ == '__main__':
    unittest.main()
