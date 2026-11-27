#!/usr/bin/env python3
"""Validate the Kobo test update without extracting it."""
import sys
import tarfile

EXPECTED = {
    'usr/local/Kobo/imageformats/libkobrowse.so',
    'mnt/onboard/.adds/nm/kobrowse',
    'mnt/onboard/.adds/kobrowse/uninstall',
    'mnt/onboard/.adds/kobrowse/KOBALT-LICENSE.txt',
}


def check(path, plugin_path=None):
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
                if plugin_path is not None:
                    with open(plugin_path, 'rb') as plugin:
                        if data != plugin.read():
                            raise ValueError('packaged plugin differs from freshly built plugin')
            elif name.endswith('/nm/kobrowse'):
                if b'touch /tmp/kobrowse.open' not in data:
                    raise ValueError('missing NickelMenu entry')
            elif name.endswith('/KOBALT-LICENSE.txt'):
                if b'MIT License' not in data or b'Copyright (c) 2026 Nico Verbruggen' not in data:
                    raise ValueError('missing Kobalt license notice')
            elif not data:
                raise ValueError('empty uninstall marker')


if __name__ == '__main__':
    try:
        if len(sys.argv) not in (2, 3):
            raise ValueError('usage: check-package.py PACKAGE [BUILT_PLUGIN]')
        check(sys.argv[1], sys.argv[2] if len(sys.argv) == 3 else None)
    except (OSError, tarfile.TarError, ValueError) as exc:
        print('Package check failed:', exc, file=sys.stderr)
        sys.exit(1)
    print('Package OK:', sys.argv[1])
