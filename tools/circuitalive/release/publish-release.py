#!/usr/bin/env python3
"""Verify every selected package before creating and publishing a GitHub release."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess


PACKAGE_SUFFIXES = {
    'rpm': 'fedora44-x86_64.rpm',
    'appimage': 'linux-x86_64.AppImage',
    'deb': 'ubuntu24.04-amd64.deb',
    'exe': 'windows-x86_64-setup.exe',
}


def selected_formats(formats):
    if formats == 'all':
        return tuple(PACKAGE_SUFFIXES)
    if formats == 'linux':
        return ('rpm', 'appimage', 'deb')
    values = formats.split(',') if isinstance(formats, str) else tuple(formats)
    if not values or len(set(values)) != len(values) or any(f not in PACKAGE_SUFFIXES for f in values):
        raise ValueError('Select all, linux, or a nonempty list of rpm, appimage, deb, exe')
    return tuple(f for f in PACKAGE_SUFFIXES if f in values)


def verify_packages(directory, version, commit, formats='all'):
    if not re.fullmatch(r'[0-9]+\.[0-9]+\.[0-9]+-alpha\.[0-9]+', version):
        raise ValueError('Expected an alpha version without the v prefix')
    if not re.fullmatch(r'[0-9a-f]{40}', commit):
        raise ValueError('Expected the full source commit SHA')
    directory = Path(directory).resolve()
    formats = selected_formats(formats)
    assets, checksums = [], {}
    for format in formats:
        suffix = PACKAGE_SUFFIXES[format]
        package = directory / f'CircuitAliveEDA-{version}-{suffix}'
        checksum = package.with_suffix(package.suffix + '.sha256')
        with package.open('rb') as stream:
            header = stream.read(12)
            stream.seek(0)
            digest = hashlib.file_digest(stream, 'sha256').hexdigest()
        valid_format = (header.startswith(b'\xed\xab\xee\xdb') if suffix.endswith('.rpm') else
                        header.startswith(b'\x7fELF') and header[8:11] == b'AI\x02'
                        if suffix.endswith('.AppImage') else header.startswith(b'!<arch>\n')
                        if suffix.endswith('.deb') else header.startswith(b'MZ'))
        if not valid_format:
            raise ValueError(f'Invalid package format: {package.name}')
        fields = checksum.read_text().strip().split()
        if len(fields) != 2 or fields[0] != digest or fields[1].lstrip('*') != package.name:
            raise ValueError(f'Checksum mismatch: {package.name}')
        assets.extend((package, checksum))
        checksums[package.name] = digest
    manifest = directory / 'release-manifest.json'
    manifest.write_text(json.dumps({'version': version, 'commit': commit,
                                    'formats': formats, 'sha256': checksums}, indent=2) + '\n')
    notes = directory / 'RELEASE_NOTES.md'
    descriptions = {
        'rpm': '- Fedora 44 x86_64: `.rpm`, installable with `sudo dnf install ./CircuitAliveEDA-*.rpm`.\n',
        'appimage': '- Linux x86_64: `.AppImage`; requires Fedora 44 / glibc 2.43 or newer compatible runtime.\n',
        'deb': '- Ubuntu 24.04 amd64: native `.deb`; install with `sudo apt install ./CircuitAliveEDA-*.deb`.\n',
        'exe': '- Windows x86_64: `-setup.exe` installer (MSVC).\n',
    }
    notes.write_text(
        f'CircuitAliveEDA {version}\n\nSource commit: `{commit}`.\n\n'
        + ''.join(descriptions[f] for f in formats) + '\n'
        'Only the selected formats are included in this release. All packages use the same source commit. '
        'All selected build jobs and package smoke tests passed before publication. '
        'SHA-256 checksums and the source manifest are attached.\n')
    return [*assets, manifest], notes


def publish(directory, version, commit, repository, formats='all'):
    assets, notes = verify_packages(directory, version, commit, formats)
    tag = 'v' + version
    # Remain a draft until every asset has been uploaded successfully.
    subprocess.run(['gh', 'release', 'create', tag, '--repo', repository, '--target', commit,
                    '--title', 'CircuitAliveEDA ' + version, '--draft', '--prerelease',
                    '--notes-file', str(notes), *(str(asset) for asset in assets)], check=True)
    subprocess.run(['gh', 'release', 'edit', tag, '--repo', repository,
                    '--draft=false', '--prerelease'], check=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', type=Path, required=True)
    parser.add_argument('--version', required=True)
    parser.add_argument('--commit', required=True)
    parser.add_argument('--repository')
    parser.add_argument('--formats', default='all', help='all, linux, or a comma-separated package selection')
    parser.add_argument('--publish', action='store_true', help='Create and publish the remote release')
    args = parser.parse_args()
    if args.publish:
        if not args.repository:
            parser.error('--repository is required with --publish')
        publish(args.directory, args.version, args.commit, args.repository, args.formats)
    else:
        assets, _ = verify_packages(args.directory, args.version, args.commit, args.formats)
        print('Verified packages:', *(asset.name for asset in assets), sep='\n')
