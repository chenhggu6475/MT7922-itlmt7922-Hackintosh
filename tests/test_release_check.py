#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause-Clear
"""Adversarial checks for public archive paths and private metadata rejection."""
import importlib.util
from pathlib import Path
import plistlib
import stat
import tempfile
import sys
import warnings
import unittest
import zipfile

sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location('release_check', Path(__file__).resolve().parents[1] / 'scripts/check-release.py')
check = importlib.util.module_from_spec(spec)
spec.loader.exec_module(check)


class ReleaseChecks(unittest.TestCase):
    def test_public_source_examples(self):
        check.check_blob('src/Info.plist', plistlib.dumps({'itl_ssid': '', 'itl_psk2': ''}))
        check.check_name('docs/OpenCore-merge-only.plist')
        check.check_name('scripts/collect-logs.sh')
        check.check_blob('tests/example.cpp', b'const char *password = "public test vector";')

    def test_private_paths_and_keys(self):
        # Construct synthetic strings; fixtures themselves contain no private data.
        for value in (b'/' + b'Users/example/private/file',
                      b'-----BEGIN ' + b'PRIVATE KEY-----',
                      b'gh' + b'p_' + b'x' * 40):
            with self.assertRaises(check.CheckFailure):
                check.check_blob('example', value)

    def test_nonempty_credentials_and_machine_identity(self):
        for value in ({'itl_ssid': 'synthetic-network'}, {'itl_psk4': 'synthetic-key'},
                      {'nested': {'SystemSerialNumber': 'synthetic'}}):
            with self.assertRaises(check.CheckFailure):
                check.check_blob('example.plist', plistlib.dumps(value))

    def test_private_files_and_traversal(self):
        for name in ('../outside', '/absolute', 'dir/../outside', 'dir\\outside',
                     'EFI/OC/config.plist', '.env', 'logs/capture.txt',
                     'driver-ioreg.txt', 'backup.bak', 'app/.DS_Store'):
            with self.assertRaises(check.CheckFailure):
                check.check_name(name)

    def test_archive_symlink_and_incomplete_package(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'bad.zip'
            with zipfile.ZipFile(path, 'w') as archive:
                link = zipfile.ZipInfo('itlmt7922.kext/link')
                link.create_system = 3
                link.external_attr = (stat.S_IFLNK | 0o777) << 16
                archive.writestr(link, '../outside')
            with self.assertRaises(check.CheckFailure):
                check.check_archive(path, '0.2.1')
            with zipfile.ZipFile(path, 'w') as archive:
                archive.writestr('itlmt7922.kext/Contents/Info.plist', plistlib.dumps({}))
                archive.writestr('itlmt7922.kext/Contents/MacOS/itlmt7922', b'synthetic')
            with self.assertRaises(check.CheckFailure):
                check.check_archive(path, '0.2.1')


    def test_archive_valid_bundle_then_duplicate(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'bundle.zip'
            info = plistlib.dumps({'CFBundleShortVersionString': '0.2.1', 'CFBundleVersion': '0.2.1',
                                   'itl_ssid': '', 'itl_psk': ''})
            with zipfile.ZipFile(path, 'w') as archive:
                archive.writestr('itlmt7922.kext/Contents/Info.plist', info)
                archive.writestr('itlmt7922.kext/Contents/MacOS/itlmt7922', b'synthetic')
                for name in check.COMMON_FILES:
                    archive.writestr(name, b'Public release documentation')
                for name in ('LICENCE.mediatek', 'mt76-LICENSE.txt'):
                    archive.writestr('licenses/' + name, b'Public redistribution license')
                sums = ''.join(digest + '  ' + name + '\n' for name, digest in check.FIRMWARE.items())
                archive.writestr('licenses/FIRMWARE-SHA256SUMS', sums)
                for name in ('PORTING.md', 'RELEASE-0.2.1.md'):
                    archive.writestr('docs/' + name, b'Public port and release notes')
                archive.writestr('docs/OpenCore-merge-only.plist', plistlib.dumps({'Kernel': {'Add': []}}))
            check.check_archive(path, '0.2.1')
            with warnings.catch_warnings():
                warnings.simplefilter('ignore', UserWarning)
                with zipfile.ZipFile(path, 'a') as archive:
                    archive.writestr('itlmt7922.kext/Contents/Info.plist', info)
            with self.assertRaises(check.CheckFailure):
                check.check_archive(path, '0.2.1')


if __name__ == '__main__':
    unittest.main()
