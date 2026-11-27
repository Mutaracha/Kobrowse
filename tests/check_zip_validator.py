import io
import pathlib
import stat
import struct
import subprocess
import sys
import tempfile
import unittest
import zipfile

VALIDATOR = sys.argv.pop(1)
WRITER = sys.argv.pop(1)


def make_zip(entries, compression=zipfile.ZIP_DEFLATED):
    out = io.BytesIO()
    with zipfile.ZipFile(out, 'w', compression=compression) as archive:
        for name, data in entries:
            archive.writestr(name, data)
    return out.getvalue()


def patch(data, offset, fmt, value):
    result = bytearray(data)
    struct.pack_into(fmt, result, offset, value)
    return bytes(result)


class NonSeekable(io.BytesIO):
    def seekable(self):
        return False

    def seek(self, *args):
        raise io.UnsupportedOperation('not seekable')


class ZipValidatorTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = pathlib.Path(self.tmp.name)
        self.archive = self.root / 'input.zip'

    def check(self, data, valid):
        self.archive.write_bytes(data)
        result = subprocess.run([VALIDATOR, str(self.archive)], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.assertEqual(result.returncode == 0, valid, result.stderr.decode('utf-8', 'replace'))
        self.assertEqual(self.archive.read_bytes(), data)

    def test_supported_files_and_writer(self):
        self.check(make_zip([]), True)
        self.check(make_zip([('books/', b''), ('books/книга.fb2', b'hello' * 20000), ('empty', b'')]), True)
        self.check(make_zip([('plain.txt', b'stored')], zipfile.ZIP_STORED), True)
        (self.root / 'example').write_bytes(b'hello' * 5000)
        result = subprocess.run([WRITER, str(self.root), str(self.archive), str(self.root / 'example')], stderr=subprocess.PIPE)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.check(self.archive.read_bytes(), True)

    def test_streamed_data_descriptor_and_corruption(self):
        out = NonSeekable()
        with zipfile.ZipFile(out, 'w', compression=zipfile.ZIP_DEFLATED) as z:
            z.writestr('data', b'abcdef' * 1000)
        valid = out.getvalue()
        self.check(valid, True)
        descriptor = valid.index(b'PK\x07\x08')
        broken = bytearray(valid)
        broken[descriptor + 4] ^= 1
        self.check(bytes(broken), False)
        # A descriptor without its optional signature is valid as well.
        central = valid.index(b'PK\x01\x02')
        unsigned = bytearray(valid[:descriptor] + valid[descriptor + 4:])
        end = unsigned.rfind(b'PK\x05\x06')
        struct.pack_into('<I', unsigned, end + 16, central - 4)
        self.check(bytes(unsigned), True)

    def test_unsafe_names_and_collisions(self):
        for name in ('../escape', '/absolute', 'x/../bad', 'x\\bad', 'CON.txt', 'a//b',
                     '.kobo/file', '.ADDS/file', '.kobrowse_trash/file', 'trailing.', 'bad?name'):
            with self.subTest(name=name):
                self.check(make_zip([(name, b'data')]), False)
        for entries in ([('a', b'x'), ('a', b'y')], [('Books/a', b'x'), ('books/b', b'y')],
                        [('a', b'x'), ('a/b', b'y')], [('é.txt', b'x'), ('e\u0301.txt', b'y')]):
            with self.subTest(entries=entries):
                self.check(make_zip(entries), False)

    def test_symlinks_specials_encryption_and_unsupported_methods(self):
        for mode in (stat.S_IFLNK | 0o777, stat.S_IFIFO | 0o600, stat.S_IFCHR | 0o600):
            entry = zipfile.ZipInfo('special')
            entry.create_system = 3
            entry.external_attr = mode << 16
            self.check(make_zip([(entry, b'target')]), False)
        original = make_zip([('name', b'data')])
        central = original.index(b'PK\x01\x02')
        self.check(patch(original, central + 8, '<H', 1), False)
        self.check(patch(original, central + 10, '<H', 99), False)
        self.check(patch(original, central + 34, '<H', 1), False)
        self.check(patch(original, central + 24, '<I', 0xffffffff), False)
        for extra in (b'\x01\x00\x00\x00', b'\x34\x12\x05\x00\x00'):
            entry = zipfile.ZipInfo('with-extra')
            entry.extra = extra
            self.check(make_zip([(entry, b'data')]), False)

    def test_bad_headers_crc_and_truncation(self):
        original = make_zip([('book.txt', b'Hello' * 1000)])
        central = original.index(b'PK\x01\x02')
        cases = [patch(original, central + 16, '<I', 0),
                 patch(original, 14, '<I', 0),
                 patch(original, 6, '<H', 8),
                 patch(original, 8, '<H', 0),
                 patch(original, central + 42, '<I', central),
                 original[:-1], original[:50], b'not zip']
        changed_name = bytearray(original); changed_name[30] ^= 1; cases.append(bytes(changed_name))
        changed_data = bytearray(original); changed_data[30 + len('book.txt')] ^= 0x40; cases.append(bytes(changed_data))
        for case in cases:
            with self.subTest(length=len(case)):
                self.check(case, False)

    def test_unflagged_non_ascii_and_invalid_utf8(self):
        valid = make_zip([('книга', b'data')])
        central = valid.index(b'PK\x01\x02')
        unflagged = patch(patch(valid, 6, '<H', 0), central + 8, '<H', 0)
        self.check(unflagged, False)
        bad = bytearray(valid); bad[30] = 0xff; bad[central + 46] = 0xff
        self.check(bytes(bad), False)

    def test_archive_size_entry_count_and_overlap(self):
        original = make_zip([('a', b'one'), ('b', b'two')])
        first = original.index(b'PK\x01\x02')
        second = original.index(b'PK\x01\x02', first + 4)
        overlap = patch(original, second + 42, '<I', 0)
        self.check(overlap, False)
        # A valid second local record embedded inside the first file's payload:
        # both names and CRCs match, but the two local ranges overlap.
        standalone = make_zip([('b', b'two')], zipfile.ZIP_STORED)
        embedded = standalone[:standalone.index(b'PK\x01\x02')]
        crafted = make_zip([('a', embedded), ('b', b'two')], zipfile.ZIP_STORED)
        second_central = crafted.index(b'PK\x01\x02', crafted.index(b'PK\x01\x02') + 4)
        self.check(patch(crafted, second_central + 42, '<I', 31), False)
        end = original.rfind(b'PK\x05\x06')
        self.check(patch(original, end + 12, '<I', 5 * 1024 * 1024), False)
        self.check(patch(patch(original, end + 8, '<H', 10001), end + 10, '<H', 10001), False)
        self.check(patch(original, first + 24, '<I', 256 * 1024 * 1024 + 1), False)


if __name__ == '__main__':
    unittest.main()
