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

    def test_rejects_invalid_members_and_content(self):
        plugin = './usr/local/Kobo/imageformats/libkobrowse.so'
        menu = './mnt/onboard/.adds/nm/kobrowse'
        marker = './mnt/onboard/.adds/kobrowse/uninstall'
        cases = ('extra', 'fat_fixture', 'missing', 'missing_marker', 'empty_marker', 'duplicate', 'symlink', 'mode', 'owner',
                 'wrong_binary', 'wrong_arch', 'wrong_menu')
        for case in cases:
            with self.subTest(case=case), tempfile.TemporaryDirectory() as tmp:
                path = pathlib.Path(tmp) / 'bad.tgz'
                with tarfile.open(path, 'w:gz') as archive:
                    files = [(plugin, ARM_SO), (menu, b'touch /tmp/kobrowse.open'), (marker, b'remove to uninstall')]
                    if case == 'extra':
                        files.append(('./unexpected', b'x'))
                    elif case == 'fat_fixture':
                        files.append(('./build/fat-test/fixture.img', b'fixture'))
                    elif case == 'missing':
                        files.pop(1)
                    elif case == 'missing_marker':
                        files.pop()
                    elif case == 'empty_marker':
                        files[2] = (marker, b'')
                    elif case == 'duplicate':
                        files.append((menu[2:], b'touch /tmp/kobrowse.open'))
                    elif case == 'wrong_binary':
                        files[0] = (plugin, b'wrong')
                    elif case == 'wrong_arch':
                        files[0] = (plugin, ARM_SO[:18] + b'\x3e\x00')
                    elif case == 'wrong_menu':
                        files[1] = (menu, b'other menu')
                    for name, data in files:
                        info = tarfile.TarInfo(name)
                        info.mode = 0o600 if case == 'mode' and name == plugin else 0o644
                        info.uid = 1 if case == 'owner' and name == plugin else 0
                        if case == 'symlink' and name == plugin:
                            info.type = tarfile.SYMTYPE
                            info.linkname = '/tmp/target'
                            archive.addfile(info)
                        else:
                            info.size = len(data)
                            archive.addfile(info, io.BytesIO(data))
                with self.assertRaises(ValueError):
                    checker.check(path)

    def test_rejects_truncated_package(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = pathlib.Path(tmp) / 'bad.tgz'
            path.write_bytes(b'not an archive')
            with self.assertRaises(tarfile.TarError):
                checker.check(path)


if __name__ == '__main__':
    unittest.main()
