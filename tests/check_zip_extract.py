import io
import os
import pathlib
import resource
import signal
import stat
import subprocess
import sys
import tempfile
import unittest
import zipfile

DRIVER = sys.argv.pop(1)


def zip_bytes(entries):
    out = io.BytesIO()
    with zipfile.ZipFile(out, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
        for name, data in entries:
            archive.writestr(name, data)
    return out.getvalue()


class ExtractTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = pathlib.Path(self.tmp.name) / 'onboard'
        self.root.mkdir()
        self.source = self.root / 'test.zip'

    def run_extract(self, data, mode=None, good=True):
        self.source.write_bytes(data)
        args = [DRIVER, str(self.root), str(self.source)] + ([mode] if mode else [])
        result = subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.assertEqual(result.returncode == 0, good, result.stderr.decode('utf-8', 'replace'))
        self.assertEqual(self.source.read_bytes(), data)
        return result

    def stages(self):
        return list(self.root.glob('.kobrowse-extract-*'))

    def test_extract_preserves_zip_and_existing_folders(self):
        prior = self.root / 'Распаковано-test'
        prior.mkdir()
        (prior / 'keep').write_bytes(b'original')
        data = zip_bytes([('Книги/книга.fb2', b'hello' * 10000), ('Книги/пустая/', b''), ('x', b'')])
        result = self.run_extract(data)
        folder = pathlib.Path(result.stdout.decode().strip())
        self.assertNotEqual(folder, prior)
        self.assertEqual((folder / 'Книги/книга.fb2').read_bytes(), b'hello' * 10000)
        self.assertTrue((folder / 'Книги/пустая').is_dir())
        self.assertEqual((folder / 'x').read_bytes(), b'')
        self.assertEqual((prior / 'keep').read_bytes(), b'original')
        self.assertFalse(self.stages())

    def test_old_kernel_fallback_and_empty_archive(self):
        result = self.run_extract(zip_bytes([]), mode='old-kernel')
        folder = pathlib.Path(result.stdout.decode().strip())
        self.assertEqual(folder.name, 'Содержимое')
        self.assertTrue(folder.is_dir())
        self.assertFalse(self.stages())

    def test_dangerous_and_corrupted_archives_are_not_published(self):
        link = zipfile.ZipInfo('link')
        link.create_system = 3
        link.external_attr = (stat.S_IFLNK | 0o777) << 16
        cases = [zip_bytes([('../escape', b'x')]), zip_bytes([('.kobo/x', b'x')]),
                 zip_bytes([(link, b'../outside')]), zip_bytes([('ok', b'x'), ('OK', b'y')])]
        valid = zip_bytes([('item', b'valid contents')])
        bad = bytearray(valid)
        central = bad.index(b'PK\x01\x02')
        bad[central + 16] ^= 0x80
        cases.append(bytes(bad))
        cases.append(valid[:-5])
        for data in cases:
            with self.subTest(size=len(data)):
                self.run_extract(data, good=False)
                self.assertFalse(self.stages())
                self.assertFalse(list(self.root.glob('Распаковано-*')))

    def test_injected_write_publish_and_cleanup_failure(self):
        data = zip_bytes([('dir/a', b'hello' * 5000)])
        for mode in ('write', 'publish'):
            result = self.run_extract(data, mode=mode, good=False)
            self.assertTrue(result.stderr)
            self.assertFalse(self.stages())
            self.assertFalse(list(self.root.glob('Распаковано-*')))
        result = self.run_extract(data, mode='cleanup', good=False)
        self.assertEqual(len(self.stages()), 1)
        self.assertIn(str(self.stages()[0]), result.stderr.decode())
        self.assertFalse(list(self.root.glob('Распаковано-*')))

    def test_failed_disk_write_no_publication(self):
        data = zip_bytes([('big', os.urandom(32768))])
        self.source.write_bytes(data)
        def limit_file_size():
            signal.signal(signal.SIGXFSZ, signal.SIG_IGN)
            resource.setrlimit(resource.RLIMIT_FSIZE, (1024, 1024))
        result = subprocess.run([DRIVER, str(self.root), str(self.source)], stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, preexec_fn=limit_file_size)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.source.read_bytes(), data)
        self.assertFalse(self.stages())
        self.assertFalse(list(self.root.glob('Распаковано-*')))

    def test_protected_directories_and_symlink_source(self):
        data = zip_bytes([('safe', b'data')])
        for name in ('.kobo', '.adds', '.kobrowse_trash'):
            folder = self.root / name
            folder.mkdir()
            source = folder / 'a.zip'
            source.write_bytes(data)
            result = subprocess.run([DRIVER, str(self.root), str(source)], stderr=subprocess.PIPE)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(source.read_bytes(), data)
            self.assertEqual(sorted(p.name for p in folder.iterdir()), ['a.zip'])
        external = pathlib.Path(self.tmp.name) / 'external.zip'
        external.write_bytes(data)
        self.source.symlink_to(external)
        result = subprocess.run([DRIVER, str(self.root), str(self.source)], stderr=subprocess.PIPE)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(external.read_bytes(), data)
        self.assertFalse(self.stages())


if __name__ == '__main__':
    unittest.main()
