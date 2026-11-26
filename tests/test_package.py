import importlib.util
import io
import pathlib
import shutil
import subprocess
import tarfile
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('package_check', ROOT / 'scripts/check-package.py')
checker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checker)

# Minimal ELF header for exercising package checks; never shipped.
ARM_SO = b'\x7fELF\x01\x01' + bytes(10) + b'\x03\x00\x28\x00'


class PackageTest(unittest.TestCase):
    def test_make_package_and_checker(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            shutil.copy(ROOT / 'Makefile', root)
            shutil.copytree(ROOT / 'NickelHook', root / 'NickelHook')
            shutil.copytree(ROOT / 'res', root / 'res')
            (root / 'libkobrowse.so').write_bytes(ARM_SO)
            (root / 'libkobrowse.so').chmod(0o755)
            (root / 'KoboRoot.tgz').write_bytes(b'existing update')
            subprocess.run(['make', 'package'], cwd=root, check=True, stdout=subprocess.PIPE)
            self.assertEqual((root / 'KoboRoot.tgz').read_bytes(), b'existing update')
            checker.check(root / 'build/KoboRoot.tgz')

    def test_runtime_names_match_package(self):
        source = (ROOT / 'src/kobrowse.cc').read_text(encoding='utf-8')
        menu = (ROOT / 'res/nm_kobrowse').read_text(encoding='utf-8')
        makefile = (ROOT / 'Makefile').read_text(encoding='utf-8')
        for expected in ('/tmp/kobrowse.open', '/mnt/onboard/.adds/kobrowse/kobrowse.ini',
                         '/mnt/onboard/.kobrowse_trash', '[kobrowse]',
                         'kobrowse/open_books', '.name           = "kobrowse"'):
            self.assertIn(expected, source)
        self.assertIn('touch /tmp/kobrowse.open', menu)
        self.assertIn('LIBRARY  := libkobrowse.so', makefile)
        self.assertIn('res/nm_kobrowse:/mnt/onboard/.adds/nm/kobrowse', makefile)

    def test_rejects_extra_files_and_wrong_binary(self):
        for extra, binary in [(True, ARM_SO), (False, b'wrong')]:
            with self.subTest(extra=extra), tempfile.TemporaryDirectory() as tmp:
                path = pathlib.Path(tmp) / 'bad.tgz'
                with tarfile.open(path, 'w:gz') as archive:
                    files = {'./usr/local/Kobo/imageformats/libkobrowse.so': binary,
                             './mnt/onboard/.adds/nm/kobrowse': b'touch /tmp/kobrowse.open'}
                    if extra:
                        files['./unexpected'] = b'x'
                    for name, data in files.items():
                        info = tarfile.TarInfo(name)
                        info.mode = 0o644
                        info.size = len(data)
                        archive.addfile(info, io.BytesIO(data))
                with self.assertRaises(ValueError):
                    checker.check(path)


if __name__ == '__main__':
    unittest.main()
