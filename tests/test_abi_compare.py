import importlib.util
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('compare_kobo_abi', ROOT / 'scripts/compare-kobo-abi.py')
abi = importlib.util.module_from_spec(spec)
spec.loader.exec_module(abi)


class AbiCompareTest(unittest.TestCase):
    def test_detects_changed_abi_and_added_dependencies(self):
        baseline = {'class': 'ELF32', 'data': 'little', 'machine': 'ARM', 'type': 'DYN',
                    'flags': 'hard float', 'vfp_args': ['VFP registers'], 'soname': ['libkobrowse.so'],
                    'search_paths': ['/usr/local/Kobo'], 'needed': ['libc.so.6', 'libQt5Core.so.5'],
                    'versions': ['GLIBC_2.4', 'GLIBCXX_3.4']}
        self.assertEqual(abi.compare(baseline, dict(baseline)), [])
        candidate = dict(baseline, needed=baseline['needed'] + ['libnew.so'],
                         versions=baseline['versions'] + ['GLIBC_2.30'], vfp_args=[])
        failures = abi.compare(baseline, candidate)
        self.assertTrue(any('vfp_args' in value for value in failures))
        self.assertTrue(any('libnew.so' in value for value in failures))
        self.assertTrue(any('GLIBC_2.30' in value for value in failures))
        self.assertEqual(abi.compare(baseline, dict(baseline, needed=['libc.so.6'], versions=['GLIBC_2.4'])), [])


if __name__ == '__main__':
    unittest.main()
