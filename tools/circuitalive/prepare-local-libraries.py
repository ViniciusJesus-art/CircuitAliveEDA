#!/usr/bin/env python3
"""Restore component libraries locally without installing the upstream application."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
PREFIX = ROOT / '.circuitalive/libraries'
VERSION = '10.0.7'
RELEASE = '1.fc44'
PACKAGES = (('kicad', 'x86_64'), ('kicad-packages3d', 'noarch'))


def prepare(download=False):
    archives = ROOT / '.circuitalive/rpms'
    archives.mkdir(parents=True, exist_ok=True)
    PREFIX.mkdir(parents=True, exist_ok=True)
    manifest = {'version': VERSION, 'packages': []}
    for name, architecture in PACKAGES:
        filename = f'{name}-{VERSION}-{RELEASE}.{architecture}.rpm'
        archive = archives / filename
        url = (f'https://kojipkgs.fedoraproject.org/packages/kicad/{VERSION}/'
               f'{RELEASE}/{architecture}/{filename}')
        if not archive.is_file():
            if not download:
                raise SystemExit(f'Missing {archive}; rerun with --download.')
            partial = archive.with_suffix('.rpm.part')
            print(f'Downloading {url}', flush=True)
            with urllib.request.urlopen(url, timeout=60) as response, partial.open('wb') as output:
                while chunk := response.read(1024 * 1024):
                    output.write(chunk)
            partial.rename(archive)
        # Verify the package before unpacking. Extract only library data and its
        # licenses: binaries, desktop entries and upstream application UI stay out.
        subprocess.run(['rpm', '--checksig', str(archive)], check=True)
        print(f'Extracting library data from {filename}', flush=True)
        producer = subprocess.Popen(['rpm2cpio', str(archive)], stdout=subprocess.PIPE)
        consumer = subprocess.run([
            'cpio', '-idmu', '--quiet', '--no-absolute-filenames', '--no-preserve-owner',
            './usr/share/kicad/symbols/*', './usr/share/kicad/footprints/*',
            './usr/share/kicad/3dmodels/*', './usr/share/kicad/template/*',
            './usr/share/licenses/kicad*/*',
        ], cwd=PREFIX, stdin=producer.stdout)
        producer.stdout.close()
        if producer.wait() or consumer.returncode:
            raise SystemExit(f'Cannot extract {filename}')
        with archive.open('rb') as stream:
            digest = hashlib.file_digest(stream, 'sha256').hexdigest()
        manifest['packages'].append({'file': filename, 'url': url, 'sha256': digest})
    libraries = PREFIX / 'usr/share/kicad'
    for directory, required in (('symbols', 'Device.kicad_sym'),
                                ('footprints', 'Resistor_SMD.pretty'),
                                ('3dmodels', 'Resistor_SMD.3dshapes'),
                                ('template', 'sym-lib-table')):
        if not (libraries / directory / required).exists():
            raise SystemExit(f'Incomplete library package: missing {directory}/{required}')
    manifest['counts'] = {
        'symbol_libraries': len(list((libraries / 'symbols').glob('*.kicad_sym'))),
        'footprints': len(list((libraries / 'footprints').rglob('*.kicad_mod'))),
        '3d_models': sum(1 for path in (libraries / '3dmodels').rglob('*')
                         if path.suffix.lower() in {'.step', '.stp', '.wrl'}),
    }
    (PREFIX / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(f'Local libraries: {libraries}')
    print(json.dumps(manifest['counts']))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--download', action='store_true', help='Download missing RPMs from Fedora Koji')
    prepare(parser.parse_args().download)
