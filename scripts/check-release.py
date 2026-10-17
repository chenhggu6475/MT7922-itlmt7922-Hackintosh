#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause-Clear
"""Check public source and packaged artifacts without printing private contents."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import plistlib
import re
import stat
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]
FIRMWARE = {
    'WIFI_MT7922_patch_mcu_1_1_hdr.bin': '7fc9075d6d54b31e0539734614ca971fa52caf41b642a3000c8a4501591a4d98',
    'WIFI_RAM_CODE_MT7922_1.bin': 'd100e5e04c324fc8da6302112d1e0b4fac952286447d2419eabcd8fb0a1f5b93',
}
SOURCE_DIRS = {'src', 'codec', 'app', 'tests', 'fw', 'scripts', 'docs', 'licenses', '.github'}
SOURCE_FILES = {
    'README.md', 'README.zh-CN.md', 'LICENSE', 'NOTICE.md', 'CHANGELOG.md',
    'CONTRIBUTING.md', 'SECURITY.md', '.gitignore', '.gitattributes',
}
EXCLUDED_DIRS = {'.git', 'build', 'dist'}
PRIVATE_FIELDS = {'SystemSerialNumber', 'SystemUUID', 'MLB', 'ROM', 'BoardSerialNumber'}
PRIVATE_PATTERNS = (
    ('personal absolute path', re.compile(rb'/(?:Users|home|Volumes)/[^/\s\x00"\x27]+/')),
    ('private key', re.compile(rb'-----BEGIN (?:[A-Z0-9]+ )?PRIVATE KEY-----')),
    ('GitHub token', re.compile(rb'(?:gh[pousr]_[A-Za-z0-9_]{30,}|github_pat_[A-Za-z0-9_]{30,})')),
)
COMMON_FILES = {'README.md', 'README.zh-CN.md', 'CONTRIBUTING.md', 'LICENSE', 'NOTICE.md', 'collect-logs.sh'}
COMMON_ROOTS = COMMON_FILES | {'docs', 'licenses'}
BUNDLES = {'itlmt7922.kext', 'ItlWiFi7922.app'}
CAPTURE_NAMES = {
    'driver-ioreg.txt', 'ethernet-ioreg.txt', 'loaded-kexts.txt', 'network-ports.txt',
    'interfaces.txt', 'unified-log.txt', 'system.txt',
}


class CheckFailure(Exception):
    pass


def require(condition, message):
    if not condition:
        raise CheckFailure(message)


def check_name(name):
    parts = name.rstrip('/').split('/')
    require(bool(name) and not name.startswith('/') and '\\' not in name and '\x00' not in name,
            'Unsafe path in release input')
    require(all(part not in ('', '.', '..') for part in parts), 'Path traversal in release input')
    for part in parts:
        lower = part.lower()
        require(lower not in {'.git', '.ssh', 'efi', 'logs', 'backups', '__macosx', '__pycache__'},
                'Private or unrelated directory in release input')
        require(not lower.startswith(('.env', '._', 'id_rsa', 'id_ed25519')),
                'Private or filesystem metadata file in release input')
        require(lower != '.ds_store' and lower != 'config.plist' and lower not in CAPTURE_NAMES,
                'Machine configuration or captured logs in release input')
        require(not lower.endswith(('.log', '.bak', '.backup', '.pem', '.key', '.p12', '.pfx', '.pyc')),
                'Private key, log or backup file in release input')


def check_plist(value):
    if isinstance(value, dict):
        for key, item in value.items():
            require(key not in PRIVATE_FIELDS, 'Machine identity field in a plist')
            if isinstance(key, str) and re.fullmatch(r'itl_(?:ssid|psk)[0-9]*', key):
                require(item == '', 'Nonempty saved Wi-Fi credential in a plist')
            check_plist(item)
    elif isinstance(value, list):
        for item in value:
            check_plist(item)


def check_blob(name, content):
    for label, pattern in PRIVATE_PATTERNS:
        require(pattern.search(content) is None, label + ' in ' + name)
    if name.lower().endswith('.plist'):
        try:
            value = plistlib.loads(content)
        except (ValueError, plistlib.InvalidFileException) as error:
            raise CheckFailure('Invalid plist: ' + name) from error
        check_plist(value)


def manifest_entries(content):
    result = {}
    for line in content.decode('ascii').splitlines():
        if not line.strip():
            continue
        match = re.fullmatch(r'([0-9a-f]{64})  ([^/\\]+)', line)
        require(match is not None, 'Malformed checksum manifest')
        digest, name = match.groups()
        require(name not in result, 'Duplicate checksum manifest entry')
        result[name] = digest
    return result


def check_source(root):
    for path in sorted(root.iterdir()):
        if path.name in EXCLUDED_DIRS:
            continue
        require(not path.is_symlink(), 'Symlink at repository root')
        require(path.name in (SOURCE_DIRS if path.is_dir() else SOURCE_FILES),
                'Unexpected repository entry: ' + path.name)
        paths = path.rglob('*') if path.is_dir() else [path]
        for item in paths:
            name = item.relative_to(root).as_posix()
            check_name(name)
            require(not item.is_symlink(), 'Symlink in source: ' + name)
            if item.is_file():
                check_blob(name, item.read_bytes())
    actual = manifest_entries((root / 'fw/SHA256SUMS').read_bytes())
    require(actual == FIRMWARE, 'Firmware manifest differs from pinned MT7922 files')
    require({p.name for p in (root / 'fw').glob('*.bin')} == set(FIRMWARE), 'Unexpected firmware binary')
    for name, digest in FIRMWARE.items():
        require(hashlib.sha256((root / 'fw' / name).read_bytes()).hexdigest() == digest,
                'Firmware checksum mismatch: ' + name)
    info = plistlib.loads((root / 'src/Info.plist').read_bytes())
    version = info['CFBundleVersion']
    require(re.fullmatch(r'[0-9]+\.[0-9]+\.[0-9]+', version) is not None, 'Invalid release version')
    require(info['CFBundleShortVersionString'] == version, 'Kext version fields differ')
    app = plistlib.loads((root / 'app/Info.plist').read_bytes())
    require(app['CFBundleShortVersionString'] == version, 'App and kext release versions differ')
    kmod = (root / 'src/kmod_info.c').read_text()
    require(re.search(r'KMOD_EXPLICIT_DECL\([^,]+,\s*"' + re.escape(version) + r'"', kmod) is not None,
            'Kernel module version differs')
    return version


def check_archive(path, version):
    with zipfile.ZipFile(path) as archive:
        infos = archive.infolist()
        require(sum(item.file_size for item in infos) <= 512 * 1024 * 1024, 'Release ZIP is unexpectedly large')
        names = set()
        roots = set()
        for item in infos:
            check_name(item.filename)
            require(item.filename not in names, 'Duplicate ZIP entry')
            names.add(item.filename)
            roots.add(PurePosixPath(item.filename).parts[0])
            require(not stat.S_ISLNK(item.external_attr >> 16), 'Symlink in release ZIP')
            require(item.file_size <= 64 * 1024 * 1024, 'Oversized file in release ZIP')
            if not item.is_dir():
                check_blob(item.filename, archive.read(item))
        release = 'itlmt7922-' + version
        if roots == {release}:
            base = release + '/'
            bundles = BUNDLES
            children = {PurePosixPath(name).parts[1] for name in names if len(PurePosixPath(name).parts) > 1}
            require(children == COMMON_ROOTS | BUNDLES, 'Unexpected combined ZIP contents')
        else:
            base = ''
            bundles = roots & BUNDLES
            require(len(bundles) == 1 and roots == COMMON_ROOTS | bundles,
                    'Component ZIP must contain exactly one bundle and public documentation/licenses')
        required = COMMON_FILES | {
            'licenses/LICENCE.mediatek', 'licenses/FIRMWARE-SHA256SUMS', 'licenses/mt76-LICENSE.txt',
            'docs/PORTING.md', 'docs/OpenCore-merge-only.plist', 'docs/RELEASE-' + version + '.md',
        }
        require({base + name for name in required} <= names, 'ZIP is missing required documentation/licenses')
        require(manifest_entries(archive.read(base + 'licenses/FIRMWARE-SHA256SUMS')) == FIRMWARE,
                'Packaged firmware hashes differ')
        for bundle in bundles:
            prefix = base + bundle + '/Contents/'
            require(prefix + 'Info.plist' in names and prefix + 'MacOS/' + bundle.split('.')[0] in names,
                    'Bundle is missing its plist or executable')
            info = plistlib.loads(archive.read(prefix + 'Info.plist'))
            require(info.get('CFBundleShortVersionString') == version, 'Packaged bundle version differs')
            if bundle.endswith('.kext'):
                require(info.get('CFBundleVersion') == version, 'Packaged kext version differs')


def check_dist(directory, version):
    expected = {
        'itlmt7922-' + version + '.zip',
        'itlmt7922.kext-' + version + '.zip',
        'ItlWiFi7922.app-' + version + '.zip',
    }
    archives = {path.name: path for path in directory.glob('*.zip')}
    require(set(archives) == expected, 'Release directory ZIP set differs from this version')
    checksums = manifest_entries((directory / 'SHA256SUMS.txt').read_bytes())
    actual = {name: hashlib.sha256(path.read_bytes()).hexdigest() for name, path in archives.items()}
    require(checksums == actual, 'Release checksums differ')
    manifest = json.loads((directory / 'build-manifest.json').read_text())
    require(manifest.get('version') == version and manifest.get('architecture') == 'x86_64',
            'Release manifest version or architecture differs')
    require(manifest.get('assets') == actual and manifest.get('firmware_embedded') is True,
            'Release manifest assets differ')
    for path in archives.values():
        require(not path.is_symlink(), 'Symlink in release directory')
        check_archive(path, version)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=ROOT)
    parser.add_argument('--dist', type=Path, help='Check all versioned release ZIPs and their manifests')
    parser.add_argument('--archive', type=Path, action='append', default=[], help='Check one release ZIP')
    args = parser.parse_args()
    try:
        version = check_source(args.root)
        if args.dist:
            check_dist(args.dist, version)
        for archive in args.archive:
            check_archive(archive, version)
    except (CheckFailure, OSError, ValueError, KeyError, zipfile.BadZipFile) as error:
        print('Release check failed: ' + str(error), file=sys.stderr)
        return 1
    print('Release checks passed: source privacy, empty credentials, pinned firmware and version ' + version)
    if args.dist or args.archive:
        print('Release ZIP contents, bundle metadata and integrity checks passed')
    return 0


if __name__ == '__main__':
    sys.exit(main())
