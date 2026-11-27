import hashlib
import json
import os
import pathlib
import shutil
import subprocess
import tarfile
import tempfile
import unittest
import io

ROOT = pathlib.Path(__file__).resolve().parents[1]
ARM_SO = b'\x7fELF\x01\x01' + bytes(10) + b'\x03\x00\x28\x00fresh'
DIGEST = 'sha256:' + 'a' * 64
IMAGE_ID = 'sha256:' + 'b' * 64
IMAGE = 'example.test/sdk@' + DIGEST


class BuildTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        temp = pathlib.Path(self.tmp.name)
        self.root = temp / 'repo'
        self.root.mkdir()
        (self.root / 'scripts').mkdir()
        for name in ('build-kobo.sh', 'check-package.py', 'write-build-info.py'):
            shutil.copy(ROOT / 'scripts' / name, self.root / 'scripts' / name)
        (self.root / '.gitignore').write_text('/build/\n/libkobrowse.so\n')
        hook = self.root / 'NickelHook'
        hook.mkdir()
        (hook / 'NickelHook.mk').write_text('# test fixture\n')
        self.git(hook, 'init', '-q')
        self.git(hook, 'add', '.')
        self.commit(hook)
        self.git(self.root, 'init', '-q')
        self.git(self.root, 'add', '.')
        self.commit(self.root)
        self.revision = self.git(self.root, 'rev-parse', 'HEAD')
        self.hook_revision = self.git(hook, 'rev-parse', 'HEAD')
        self.plugin = temp / 'plugin'
        self.plugin.write_bytes(ARM_SO)
        self.package = temp / 'package.tgz'
        files = {
            'usr/local/Kobo/imageformats/libkobrowse.so': ARM_SO,
            'mnt/onboard/.adds/nm/kobrowse': b'touch /tmp/kobrowse.open',
            'mnt/onboard/.adds/kobrowse/uninstall': b'marker',
            'mnt/onboard/.adds/kobrowse/KOBALT-LICENSE.txt': (ROOT / 'res/KOBALT-LICENSE.txt').read_bytes(),
        }
        with tarfile.open(self.package, 'w:gz') as tar:
            for name, contents in files.items():
                info = tarfile.TarInfo(name)
                info.mode, info.uid, info.gid, info.size = 0o644, 0, 0, len(contents)
                tar.addfile(info, io.BytesIO(contents))
        self.engine = temp / 'engine'
        self.engine.write_text('''#!/usr/bin/env python3
import json, os, pathlib, shutil, sys
if sys.argv[1] == 'run':
    if os.environ.get('FAKE_FAIL'): sys.exit(1)
    shutil.copyfile(os.environ['FAKE_PLUGIN'], 'libkobrowse.so')
    shutil.copyfile(os.environ['FAKE_PACKAGE'], 'build/KoboRoot.tgz')
elif sys.argv[1:3] == ['image', 'inspect']:
    print(json.dumps([{'Id': os.environ['FAKE_IMAGE_ID'], 'RepoDigests': [os.environ['FAKE_IMAGE']]}]))
else:
    sys.exit(2)
''')
        self.engine.chmod(0o755)
        self.env = dict(os.environ, CONTAINER_ENGINE=str(self.engine), KOBROWSE_SDK_IMAGE=IMAGE,
                        FAKE_PLUGIN=str(self.plugin), FAKE_PACKAGE=str(self.package),
                        FAKE_IMAGE_ID=IMAGE_ID, FAKE_IMAGE=IMAGE, KOBROWSE_REQUIRE_CLEAN='1',
                        GITHUB_SHA=self.revision)

    @staticmethod
    def git(path, *args):
        return subprocess.check_output(['git', '-C', str(path), *args], text=True, stderr=subprocess.PIPE).strip()

    def commit(self, path):
        self.git(path, '-c', 'user.name=Test', '-c', 'user.email=test@example.invalid', 'commit', '-qm', 'fixture')

    def run_build(self, **env):
        return subprocess.run(['bash', 'scripts/build-kobo.sh'], cwd=self.root, env=dict(self.env, **env),
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)

    def test_build_metadata_and_failed_rebuild_remove_old_outputs(self):
        result = self.run_build()
        self.assertEqual(result.returncode, 0, result.stderr)
        info = json.loads((self.root / 'build/build-info.json').read_text())
        self.assertEqual(info['source']['commit'], self.revision)
        self.assertEqual(info['source']['nickelhook_commit'], self.hook_revision)
        self.assertFalse(info['source']['dirty'])
        self.assertEqual(info['sdk']['reference'], IMAGE)
        self.assertEqual(info['sdk']['repo_digests'], [IMAGE])
        self.assertEqual(info['files']['KoboRoot.tgz']['sha256'], hashlib.sha256(self.package.read_bytes()).hexdigest())
        self.assertEqual(info['files']['libkobrowse.so']['sha256'], hashlib.sha256(ARM_SO).hexdigest())
        result = self.run_build(FAKE_FAIL='1')
        self.assertNotEqual(result.returncode, 0)
        for path in ('libkobrowse.so', 'build/KoboRoot.tgz', 'build/build-info.json'):
            self.assertFalse((self.root / path).exists(), path)

    def test_mismatched_package_image_or_dirty_tree_cannot_produce_metadata(self):
        self.plugin.write_bytes(ARM_SO + b'other')
        self.assertNotEqual(self.run_build().returncode, 0)
        self.assertFalse((self.root / 'build/build-info.json').exists())
        self.plugin.write_bytes(ARM_SO)
        self.assertNotEqual(self.run_build(FAKE_IMAGE='example.test/sdk@sha256:' + 'c' * 64).returncode, 0)
        self.assertFalse((self.root / 'build/build-info.json').exists())
        (self.root / '.gitignore').write_text('modified\n')
        self.assertNotEqual(self.run_build().returncode, 0)
        self.assertFalse((self.root / 'build/build-info.json').exists())


if __name__ == '__main__':
    unittest.main()
