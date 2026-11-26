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
            (root / 'libkofm.so').write_bytes(ARM_SO)
            (root / 'libkofm.so').chmod(0o755)
            subprocess.run(['make', 'package'], cwd=root, check=True, stdout=subprocess.PIPE)
            checker.check(root / 'kofm_KoboRoot.tgz')

    def test_rejects_extra_files_and_wrong_binary(self):
        for extra, binary in [(True, ARM_SO), (False, b'wrong')]:
            with self.subTest(extra=extra), tempfile.TemporaryDirectory() as tmp:
                path = pathlib.Path(tmp) / 'bad.tgz'
                with tarfile.open(path, 'w:gz') as archive:
                    files = {'./usr/local/Kobo/imageformats/libkofm.so': binary,
                             './mnt/onboard/.adds/nm/kofm': b'touch /tmp/kofm.open'}
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
