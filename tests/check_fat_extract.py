"""Run only on the disposable FAT image mounted by run-fat-policy-tests.sh."""
import io
import os
import pathlib
import shutil
import subprocess
import sys
import zipfile

exe = sys.argv[1]
root = pathlib.Path(sys.argv[2]) / 'zip-fixture'
root.mkdir()
probe = root / 'probe'
probe.mkdir()
(probe / 'Long file name.txt').write_bytes(b'probe')
alias = next((f'LONGFI~{n}.TXT' for n in range(1, 10) if (probe / f'LONGFI~{n}.TXT').exists()), None)
if alias is None:
    raise AssertionError('could not identify actual FAT short alias')
shutil.rmtree(probe)


def make_zip(entries):
    out = io.BytesIO()
    with zipfile.ZipFile(out, 'w', compression=zipfile.ZIP_DEFLATED) as z:
        for name, data in entries:
            z.writestr(name, data)
    return out.getvalue()

source = root / 'test.zip'
original = make_zip([('Long file name.txt', b'first'), (alias, b'second')])
source.write_bytes(original)
result = subprocess.run([exe, str(root), str(source)], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
assert result.returncode != 0, 'FAT short alias collision must be rejected'
assert source.read_bytes() == original
assert not list(root.glob('.kobrowse-extract-*'))
assert not list(root.glob('Распаковано-*'))

# A pre-existing output folder with different case cannot be replaced on FAT.
existing = root / 'распаковано-test'
existing.mkdir()
(existing / 'keep').write_bytes(b'keep')
original = make_zip([('книги/file.txt', b'content')])
source.write_bytes(original)
result = subprocess.run([exe, str(root), str(source)], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
assert result.returncode == 0, result.stderr.decode('utf-8', 'replace')
output = pathlib.Path(os.fsdecode(result.stdout).strip())
assert (output / 'книги/file.txt').read_bytes() == b'content'
assert (existing / 'keep').read_bytes() == b'keep'
assert source.read_bytes() == original
assert not list(root.glob('.kobrowse-extract-*'))
print('FAT ZIP extraction tests passed')
