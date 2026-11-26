#!/usr/bin/env python3
"""Validate the Kobo test update without extracting it."""
import sys
import tarfile

EXPECTED = {
    'usr/local/Kobo/imageformats/libkobrowse.so',
    'mnt/onboard/.adds/nm/kobrowse',
    'mnt/onboard/.adds/kobrowse/uninstall',
}


def check(path):
    with tarfile.open(path, 'r:gz') as archive:
        members = archive.getmembers()
        names = [m.name.removeprefix('./') for m in members]
        if len(names) != len(set(names)) or set(names) != EXPECTED:
            raise ValueError('unexpected or duplicate archive members: ' + repr(names))
        for member in members:
            if not member.isfile() or member.uid != 0 or member.gid != 0 or member.mode != 0o644:
                raise ValueError('invalid type, ownership or mode: ' + member.name)
            data = archive.extractfile(member).read()
            name = member.name.removeprefix('./')
            if name.endswith('libkobrowse.so'):
                # ELF32, little-endian, ARM, shared object.
                if (len(data) < 20 or data[:6] != b'\x7fELF\x01\x01'
                        or data[16:20] != b'\x03\x00\x28\x00'):
                    raise ValueError('plugin is not an ARM ELF32 shared object')
            elif name.endswith('/nm/kobrowse'):
                if b'touch /tmp/kobrowse.open' not in data:
                    raise ValueError('missing NickelMenu entry')
            elif not data:
                raise ValueError('empty uninstall marker')


if __name__ == '__main__':
    try:
        if len(sys.argv) != 2:
            raise ValueError('usage: check-package.py PACKAGE')
        check(sys.argv[1])
    except (OSError, tarfile.TarError, ValueError) as exc:
        print('Package check failed:', exc, file=sys.stderr)
        sys.exit(1)
    print('Package OK:', sys.argv[1])
