import os
import pathlib
import resource
import signal
import subprocess
import sys
import tempfile
import unittest
import zipfile

DRIVER = sys.argv.pop(1)


class ZipTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = pathlib.Path(self.temp.name) / 'onboard'
        self.root.mkdir()
        self.dest = self.root / 'result.zip'

    def run_archive(self, *sources, success=True):
        result = subprocess.run([DRIVER, str(self.root), str(self.dest), *(str(s) for s in sources)],
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.assertEqual(result.returncode == 0, success, result.stderr.decode('utf-8', 'replace'))
        self.assertFalse(list(self.root.glob('.kobrowse_zip_*')))

    def test_folders_unicode_hidden_empty_and_multiple_files(self):
        folder = self.root / 'Книги'
        (folder / 'пусто').mkdir(parents=True)
        (folder / '.скрытый').write_bytes(b'hidden')
        (folder / 'readme.txt').write_bytes(b'compressible data' * 20000)
        other = self.root / 'other.txt'
        other.write_bytes(b'')
        self.run_archive(folder, other)
        with zipfile.ZipFile(self.dest) as archive:
            self.assertIsNone(archive.testzip())
            self.assertEqual(archive.read('Книги/.скрытый'), b'hidden')
            self.assertEqual(archive.read('Книги/readme.txt'), b'compressible data' * 20000)
            self.assertEqual(archive.read('other.txt'), b'')
            self.assertIn('Книги/пусто/', archive.namelist())
            self.assertEqual(archive.getinfo('Книги/readme.txt').compress_type, zipfile.ZIP_DEFLATED)
        self.assertTrue(other.exists())

    def test_reject_links_and_protected_and_existing_destination(self):
        folder = self.root / 'books'
        folder.mkdir()
        (folder / 'link').symlink_to('/etc/passwd')
        self.run_archive(folder, success=False)
        self.assertFalse(self.dest.exists())
        protected = self.root / '.kobo'
        protected.mkdir()
        self.run_archive(protected, success=False)
        file = self.root / 'book'
        file.write_text('data')
        self.dest.write_bytes(b'existing')
        self.run_archive(file, success=False)
        self.assertEqual(self.dest.read_bytes(), b'existing')

    def test_unrepresentable_filename_is_not_silently_omitted(self):
        folder = self.root / 'books'
        folder.mkdir()
        (folder / 'valid').write_bytes(b'keep')
        raw = os.fsencode(folder) + b'/\xff'
        fd = os.open(raw, os.O_CREAT | os.O_WRONLY | os.O_EXCL, 0o600)
        os.close(fd)
        self.run_archive(folder, success=False)
        self.assertFalse(self.dest.exists())
        self.assertEqual((folder / 'valid').read_bytes(), b'keep')
        self.assertTrue(os.path.exists(raw))

    def test_write_failure_leaves_source_and_no_partial_archive(self):
        source = self.root / 'data'
        source.write_bytes(os.urandom(128 * 1024))

        def limit_output():
            signal.signal(signal.SIGXFSZ, signal.SIG_IGN)
            resource.setrlimit(resource.RLIMIT_FSIZE, (1024, 1024))

        result = subprocess.run([DRIVER, str(self.root), str(self.dest), str(source)],
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE, preexec_fn=limit_output)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(self.dest.exists())
        self.assertFalse(list(self.root.glob('.kobrowse_zip_*')))
        self.assertEqual(source.stat().st_size, 128 * 1024)

    def test_missing_and_oversized_source_leave_no_archive(self):
        self.run_archive(self.root / 'missing', success=False)
        big = self.root / 'large'
        with big.open('wb') as stream:
            stream.truncate(0xffffffff)
        self.run_archive(big, success=False)
        self.assertFalse(self.dest.exists())
        self.assertTrue(big.exists())


if __name__ == '__main__':
    unittest.main()
