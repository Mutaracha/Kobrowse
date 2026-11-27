#!/usr/bin/env python3
"""Record the checked package, Git state and image Docker actually used."""
import hashlib
import json
import os
import pathlib
import re
import subprocess
import sys
from datetime import datetime, timezone

ROOT = pathlib.Path(__file__).resolve().parents[1]
DIGEST = re.compile(r'^sha256:[0-9a-f]{64}$')


def git(*args):
    return subprocess.check_output(['git', *args], cwd=ROOT, text=True).strip()


def sha256(path):
    digest = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def build_info(image, inspection, require_clean=False):
    if not isinstance(inspection, list) or len(inspection) != 1 or not isinstance(inspection[0], dict):
        raise ValueError('expected one inspected SDK image')
    detail = inspection[0]
    image_id = detail.get('Id')
    repo_digests = detail.get('RepoDigests') or []
    if not isinstance(image_id, str) or not DIGEST.fullmatch(image_id):
        raise ValueError('invalid SDK image ID')
    if not isinstance(repo_digests, list) or not all(isinstance(item, str) for item in repo_digests):
        raise ValueError('invalid SDK repository digests')
    if '@sha256:' in image and image not in repo_digests:
        raise ValueError('inspected image does not match requested digest')
    revision = git('rev-parse', 'HEAD')
    hook_revision = git('-C', 'NickelHook', 'rev-parse', 'HEAD')
    status = git('status', '--porcelain=v1', '--untracked-files=normal')
    hook_status = git('-C', 'NickelHook', 'status', '--porcelain=v1', '--untracked-files=normal')
    dirty = bool(status or hook_status)
    if require_clean and dirty:
        raise ValueError('source tree or NickelHook has uncommitted changes')
    expected_revision = os.environ.get('GITHUB_SHA') if require_clean else None
    if expected_revision and expected_revision != revision:
        raise ValueError('checked-out commit does not match GITHUB_SHA')
    return {
        'format': 1,
        'created_utc': datetime.now(timezone.utc).isoformat(),
        'source': {'commit': revision, 'dirty': dirty, 'status': status.splitlines(),
                   'nickelhook_commit': hook_revision, 'nickelhook_status': hook_status.splitlines()},
        'sdk': {'reference': image, 'image_id': image_id, 'repo_digests': sorted(repo_digests)},
        'files': {'KoboRoot.tgz': {'sha256': sha256(ROOT / 'build/KoboRoot.tgz')},
                  'libkobrowse.so': {'sha256': sha256(ROOT / 'libkobrowse.so')}},
    }


def main():
    if len(sys.argv) != 3:
        raise ValueError('usage: write-build-info.py SDK_REFERENCE INSPECT_JSON')
    info = build_info(sys.argv[1], json.loads(sys.argv[2]), os.environ.get('KOBROWSE_REQUIRE_CLEAN') == '1')
    path = ROOT / 'build/build-info.json'
    path.write_text(json.dumps(info, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print('Build metadata:', path)
    print('SDK:', ', '.join(info['sdk']['repo_digests']) or info['sdk']['image_id'])


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, subprocess.CalledProcessError) as exc:
        print('Build metadata failed:', exc, file=sys.stderr)
        sys.exit(1)
