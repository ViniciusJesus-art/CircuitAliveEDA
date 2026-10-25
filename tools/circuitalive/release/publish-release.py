#!/usr/bin/env python3
"""Verify every selected package before creating and publishing a GitHub release."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import unicodedata


PACKAGE_SUFFIXES = {
    'rpm': 'fedora44-x86_64.rpm',
    'appimage': 'linux-x86_64.AppImage',
    'deb': 'ubuntu24.04-amd64.deb',
    'exe': 'windows-x86_64-setup.exe',
}


def release_slug(label):
    """Turn a human-friendly release label into a portable asset/tag identifier."""
    if (not isinstance(label, str) or not label.strip() or len(label) > 100
            or any(ord(char) < 32 or ord(char) == 127 for char in label)):
        raise ValueError('Release label must be 1–100 printable characters')
    normalized = unicodedata.normalize('NFKD', label.strip())
    normalized = ''.join(char for char in normalized if not unicodedata.combining(char))
    slug = re.sub(r'[^A-Za-z0-9._-]+', '-', normalized).strip('._-')
    slug = re.sub(r'\.{2,}', '.', slug)[:80].rstrip('._-')
    if not slug:
        raise ValueError('Release label needs at least one letter or number')
    return slug


def release_tag(slug):
    return slug if re.match(r'^v[0-9]', slug) else 'v' + slug


def selected_formats(formats):
    if formats == 'all':
        return tuple(PACKAGE_SUFFIXES)
    if formats == 'linux':
        return ('rpm', 'appimage', 'deb')
    values = formats.split(',') if isinstance(formats, str) else tuple(formats)
    if not values or len(set(values)) != len(values) or any(f not in PACKAGE_SUFFIXES for f in values):
        raise ValueError('Select all, linux, or a nonempty list of rpm, appimage, deb, exe')
    return tuple(f for f in PACKAGE_SUFFIXES if f in values)


def verify_packages(directory, version, commit, formats='all', release_label=None):
    if not re.fullmatch(r'[0-9]+\.[0-9]+\.[0-9]+-alpha\.[0-9]+', version):
        raise ValueError('Expected an internal alpha package version without the v prefix')
    if not re.fullmatch(r'[0-9a-f]{40}', commit):
        raise ValueError('Expected the full source commit SHA')
    label = version if release_label is None else release_label
    slug = release_slug(label)
    directory = Path(directory).resolve()
    formats = selected_formats(formats)
    packages, checksums, sources = [], {}, {}
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
        public_name = f'CircuitAliveEDA-{slug}.{format if format != "appimage" else "AppImage"}'
        public_package = directory / public_name
        packages.append((package, public_package))
        checksums[public_name] = digest
        sources[public_name] = package.name
    # All source packages and their sidecars must pass verification before creating any aliases.
    assets = []
    for source, public_package in packages:
        if source != public_package:
            if public_package.exists():
                if not public_package.samefile(source):
                    raise ValueError(f'Public package filename already exists: {public_package.name}')
            else:
                os.link(source, public_package)  # No second multi-GB copy on the Actions runner.
        assets.append(public_package)
    sums = directory / 'SHA256SUMS.txt'
    sums.write_text(''.join(f'{digest}  {name}\n' for name, digest in checksums.items()))
    manifest = directory / 'release-manifest.json'
    manifest.write_text(json.dumps({'version': version, 'release_label': label,
                                    'tag': release_tag(slug), 'commit': commit,
                                    'formats': formats, 'sha256': checksums,
                                    'source_packages': sources}, indent=2) + '\n')
    notes = directory / 'RELEASE_NOTES.md'
    descriptions = {
        'rpm': '- Fedora 44 x86_64: `.rpm`, installable with `sudo dnf install ./CircuitAliveEDA-*.rpm`.\n',
        'appimage': '- Linux x86_64: `.AppImage`; requires Fedora 44 / glibc 2.43 or newer compatible runtime.\n',
        'deb': '- Ubuntu 24.04 amd64: native `.deb`; install with `sudo apt install ./CircuitAliveEDA-*.deb`.\n',
        'exe': '- Windows x86_64: `-setup.exe` installer (MSVC).\n',
    }
    notes.write_text(
        f'CircuitAliveEDA {label}\n\nSource commit: `{commit}`. '
        f'Internal package version: `{version}`.\n\n'
        + ''.join(descriptions[f] for f in formats) + '\n'
        'Only the selected formats are included in this release. All packages use the same source commit. '
        'All selected build jobs and package smoke tests passed before publication. '
        'SHA-256 checksums are included in `SHA256SUMS.txt`.\n')
    return [*assets, sums], notes


def publish(directory, version, commit, repository, formats='all', release_label=None):
    assets, notes = verify_packages(directory, version, commit, formats, release_label)
    label = version if release_label is None else release_label
    tag = release_tag(release_slug(label))
    # Remain a draft until every asset has been uploaded successfully.
    subprocess.run(['gh', 'release', 'create', tag, '--repo', repository, '--target', commit,
                    '--title', 'CircuitAliveEDA ' + label, '--draft', '--prerelease',
                    '--notes-file', str(notes), *(str(asset) for asset in assets)], check=True)
    subprocess.run(['gh', 'release', 'edit', tag, '--repo', repository,
                    '--draft=false', '--prerelease'], check=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', type=Path, required=True)
    parser.add_argument('--version', required=True)
    parser.add_argument('--release-label', help='Human-friendly release name; defaults to --version')
    parser.add_argument('--commit', required=True)
    parser.add_argument('--repository')
    parser.add_argument('--formats', default='all', help='all, linux, or a comma-separated package selection')
    parser.add_argument('--publish', action='store_true', help='Create and publish the remote release')
    args = parser.parse_args()
    if args.publish:
        if not args.repository:
            parser.error('--repository is required with --publish')
        publish(args.directory, args.version, args.commit, args.repository, args.formats, args.release_label)
    else:
        assets, _ = verify_packages(args.directory, args.version, args.commit, args.formats, args.release_label)
        print('Verified packages:', *(asset.name for asset in assets), sep='\n')
