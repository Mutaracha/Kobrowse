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

    def test_extract_preserves_existing_and_renames_only_top_level(self):
        prior = self.root / 'Книги'
        prior.mkdir()
        (prior / 'keep').write_bytes(b'original')
        (self.root / 'x.txt').write_bytes(b'old')
        (self.root / 'x_1.txt').write_bytes(b'old1')
        data = zip_bytes([('Книги/книга.fb2', b'hello' * 10000), ('Книги/пустая/', b''), ('x.txt', b'new')])
        result = self.run_extract(data)
        self.assertEqual(pathlib.Path(result.stdout.decode().strip()), self.root)
        self.assertEqual((self.root / 'Книги_1/книга.fb2').read_bytes(), b'hello' * 10000)
        self.assertTrue((self.root / 'Книги_1/пустая').is_dir())
        self.assertEqual((self.root / 'x_2.txt').read_bytes(), b'new')
        self.assertEqual((prior / 'keep').read_bytes(), b'original')
        self.assertEqual((self.root / 'x.txt').read_bytes(), b'old')
        self.assertFalse(self.stages())
        self.run_extract(data)
        self.assertEqual((self.root / 'Книги_2/книга.fb2').read_bytes(), b'hello' * 10000)
        self.assertEqual((self.root / 'x_3.txt').read_bytes(), b'new')

    def test_old_kernel_fallback_and_empty_archive(self):
        self.run_extract(zip_bytes([]), mode='old-kernel')
        self.assertEqual(sorted(p.name for p in self.root.iterdir()), ['test.zip'])
        data = zip_bytes([('nested/a.txt', b'hello'), ('top.txt', b'top')])
        self.run_extract(data, mode='old-kernel')
        self.assertEqual((self.root / 'nested/a.txt').read_bytes(), b'hello')
        self.assertEqual((self.root / 'top.txt').read_bytes(), b'top')
        self.run_extract(data, mode='old-kernel')
        self.assertEqual((self.root / 'nested_1/a.txt').read_bytes(), b'hello')
        self.assertEqual((self.root / 'top_1.txt').read_bytes(), b'top')
        self.assertFalse(self.stages())

    def test_case_and_symlink_conflicts(self):
        (self.root / 'HELLO.txt').write_bytes(b'old')
        (self.root / 'link').symlink_to(self.root / 'missing')
        self.run_extract(zip_bytes([('hello.txt', b'new'), ('link', b'new link')]))
        self.assertEqual((self.root / 'hello_1.txt').read_bytes(), b'new')
        self.assertEqual((self.root / 'HELLO.txt').read_bytes(), b'old')
        self.assertTrue((self.root / 'link').is_symlink())
        self.assertEqual((self.root / 'link_1').read_bytes(), b'new link')

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
                self.assertEqual({p.name for p in self.root.iterdir() if p.name != 'test.zip'}, set())

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

    def test_partial_publication_is_reported_and_staging_kept(self):
        data = zip_bytes([('a.txt', b'first'), ('b.txt', b'second')])
        result = self.run_extract(data, mode='partial', good=False)
        self.assertEqual((self.root / 'a.txt').read_bytes(), b'first')
        self.assertFalse((self.root / 'b.txt').exists())
        self.assertEqual(len(self.stages()), 1)
        self.assertEqual((self.stages()[0] / 'b.txt').read_bytes(), b'second')
        message = result.stderr.decode('utf-8')
        self.assertIn(str(self.root / 'a.txt'), message)
        self.assertIn(str(self.stages()[0]), message)

    def test_old_kernel_copy_failure_reports_partial_and_retains_stage(self):
        data = zip_bytes([('dir/a.txt', b'important')])
        result = self.run_extract(data, mode='copy-write', good=False)
        self.assertTrue((self.root / 'dir').is_dir())
        self.assertEqual(len(self.stages()), 1)
        self.assertEqual((self.stages()[0] / 'dir/a.txt').read_bytes(), b'important')
        message = result.stderr.decode('utf-8')
        self.assertIn(str(self.root / 'dir'), message)
        self.assertIn(str(self.stages()[0]), message)

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
