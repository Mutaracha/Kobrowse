"""Keep the ZIP action on the internal reader, never an external shell tool."""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class ZipUiTest(unittest.TestCase):
    def test_zip_click_uses_internal_extractor_and_preserves_archive(self):
        source = (ROOT / 'src/kobrowse.cc').read_text(encoding='utf-8')
        start = source.index('if (path.endsWith(u8(".zip"), Qt::CaseInsensitive))')
        end = source.index('// Файл — открыть читалкой', start)
        branch = source[start:end]
        self.assertIn('extractZipArchive(', branch)
        self.assertIn('load();', branch)
        self.assertIn('return;', branch)
        self.assertNotIn('moveToTrash', branch)
        self.assertNotIn('/usr/bin/unzip', source)
        self.assertNotIn('unzipFile', source)


if __name__ == '__main__':
    unittest.main()
