"""Guard the temporary fail-closed ZIP UI until a tested extractor replaces it."""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class ZipDisabledTest(unittest.TestCase):
    def test_zip_click_reports_unavailable_and_returns(self):
        source = (ROOT / 'src/kobrowse.cc').read_text(encoding='utf-8')
        start = source.index('if (path.endsWith(u8(".zip"), Qt::CaseInsensitive))')
        end = source.index('// Файл — открыть читалкой', start)
        branch = source[start:end]
        self.assertIn('Распаковка ZIP временно недоступна', branch)
        self.assertIn('return;', branch)
        self.assertNotIn('unzipFile', source)
        self.assertNotIn('/usr/bin/unzip', source)
        self.assertNotIn('m_zipPath', source)


if __name__ == '__main__':
    unittest.main()
