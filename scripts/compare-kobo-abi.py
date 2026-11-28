#!/usr/bin/env python3
"""Conservative comparison of ELF requirements; not proof of device compatibility."""
import re
import subprocess
import sys


def inspect(path):
    header = subprocess.check_output(['readelf', '-Wh', path], text=True)
    dynamic = subprocess.check_output(['readelf', '-Wd', path], text=True)
    attributes = subprocess.check_output(['readelf', '-WA', path], text=True)
    versions = subprocess.check_output(['readelf', '-WV', path], text=True)
    def header_value(name):
        match = re.search(r'^\s*' + re.escape(name) + r':\s*(.*)$', header, re.MULTILINE)
        if not match:
            raise ValueError('missing ELF header ' + name + ': ' + path)
        return match.group(1).strip()
    needed = set(re.findall(r'\(NEEDED\).*?\[(.*?)\]', dynamic))
    paths = set(re.findall(r'\((?:RPATH|RUNPATH)\).*?\[(.*?)\]', dynamic))
    soname = set(re.findall(r'\(SONAME\).*?\[(.*?)\]', dynamic))
    vfp = re.findall(r'^\s*Tag_ABI_VFP_args:\s*(.*)$', attributes, re.MULTILINE)
    needs_part = versions.split('Version needs section', 1)[-1] if 'Version needs section' in versions else ''
    needs = set(re.findall(r'Name:\s*(\S+)', needs_part))
    return {'class': header_value('Class'), 'data': header_value('Data'), 'machine': header_value('Machine'),
            'type': header_value('Type'), 'flags': header_value('Flags'), 'vfp_args': sorted(vfp),
            'soname': sorted(soname), 'needed': sorted(needed), 'search_paths': sorted(paths), 'versions': sorted(needs)}


def compare(old, new):
    failures = []
    for key in ('class', 'data', 'machine', 'type', 'flags', 'vfp_args', 'soname', 'search_paths'):
        if old[key] != new[key]:
            failures.append(key + ' differs')
    for key in ('needed', 'versions'):
        added = sorted(set(new[key]) - set(old[key]))
        if added:
            failures.append('additional ' + key + ': ' + ', '.join(added))
    return failures


def main():
    if len(sys.argv) != 3:
        raise ValueError('usage: compare-kobo-abi.py NICKELTC_PLUGIN KOBUILD_PLUGIN')
    baseline, candidate = inspect(sys.argv[1]), inspect(sys.argv[2])
    print('ELF compatibility comparison (static only; not proof of device compatibility)')
    for key in baseline:
        print(key + ':')
        print('  NickelTC:', baseline[key])
        print('  kobuild: ', candidate[key])
    failures = compare(baseline, candidate)
    if failures:
        raise ValueError('; '.join(failures))
    print('No new ELF dependencies or version requirements; key ABI fields match.')


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, subprocess.CalledProcessError) as exc:
        print('ELF comparison failed:', exc, file=sys.stderr)
        sys.exit(1)
