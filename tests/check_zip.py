import pathlib
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
