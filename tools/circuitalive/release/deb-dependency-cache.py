#!/usr/bin/env python3
"""Validate the installed Ubuntu dependencies; never restore a CMake build tree."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import subprocess

ROOT = Path(__file__).resolve().parents[3]
MANIFEST = '.circuitalive-dependency-cache.json'
REQUIRED = ('lib/libnng.so', 'lib/libngspice.so',
            'include/nng/nng.h', 'include/ngspice/sharedspice.h',
            'lib/ngspice/analog.cm', 'share/ngspice/scripts/spinit')


def profile(root=ROOT, run=subprocess.check_output):
    digest = hashlib.sha256()
    # pkg-config/libtool metadata embeds the installation prefix.
    digest.update(str(root.resolve()).encode())
    for name in ('CC', 'CXX', 'CFLAGS', 'CXXFLAGS', 'CPPFLAGS', 'LDFLAGS'):
        digest.update(name.encode() + b'=' + os.environ.get(name, '').encode() + b'\0')
    for name in ('linux-toolchain.json', 'build-deb.sh', 'deb-dependency-cache.py'):
        digest.update(name.encode() + b'\0')
        digest.update((root / 'tools/circuitalive/release' / name).read_bytes())
    digest.update(platform.machine().encode())
    # Include installed development headers, runtime libraries, compiler and build
    # tools, rather than assuming every ubuntu-24.04 runner image has the same ABI.
    for command in (['gcc', '-v'], ['g++', '-v'], ['cmake', '--version'],
                    ['dpkg-query', '-W', '-f=${binary:Package}=${Version}\n']):
        digest.update(run(command, stderr=subprocess.STDOUT))
    return digest.hexdigest()


def inventory(directory):
    entries = {}
    for path in sorted(directory.rglob('*')):
        name = path.relative_to(directory).as_posix()
        if name == MANIFEST:
            continue
        if path.is_symlink():
            # Installed symlinks must resolve within this prefix.
            path.resolve(strict=True).relative_to(directory.resolve())
            entries[name] = {'link': str(path.readlink())}
        elif path.is_file():
            with path.open('rb') as stream:
                entries[name] = {'sha256': hashlib.file_digest(stream, 'sha256').hexdigest()}
    return entries


def record(directory, expected):
    if not all((directory / name).is_file() for name in REQUIRED):
        raise ValueError('Incomplete native dependency installation')
    contents = {'profile': expected, 'files': inventory(directory)}
    temporary = directory / (MANIFEST + '.tmp')
    temporary.write_text(json.dumps(contents, sort_keys=True) + '\n')
    temporary.replace(directory / MANIFEST)


def verify(directory, expected):
    try:
        manifest = json.loads((directory / MANIFEST).read_text())
        return (manifest['profile'] == expected
                and all((directory / name).is_file() for name in REQUIRED)
                and manifest['files'] == inventory(directory))
    except (OSError, ValueError, KeyError, TypeError, RuntimeError):
        return False


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('operation', choices=('profile', 'verify', 'record'))
    parser.add_argument('directory', nargs='?', type=Path)
    parser.add_argument('expected', nargs='?')
    args = parser.parse_args()
    if args.operation == 'profile':
        print(profile())
    else:
        if args.directory is None or args.expected is None:
            parser.error('directory and profile are required')
        if args.operation == 'verify':
            raise SystemExit(0 if verify(args.directory, args.expected) else 1)
        record(args.directory, args.expected)
