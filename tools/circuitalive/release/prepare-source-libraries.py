#!/usr/bin/env python3
"""Prepare KiCad 10.0.7 component data for the native Ubuntu package."""
import argparse
import json
from pathlib import Path
import subprocess


def prepare(destination):
    destination = Path(destination).resolve()
    destination.mkdir(parents=True, exist_ok=True)
    manifest = {}
    for folder, repo in {'symbols': 'kicad-symbols', 'footprints': 'kicad-footprints',
                         '3dmodels': 'kicad-packages3D', 'template': 'kicad-templates'}.items():
        checkout = destination / folder
        if not checkout.exists():
            subprocess.run(['git', 'clone', '--depth', '1', '--branch', '10.0.7',
                            f'https://gitlab.com/kicad/libraries/{repo}.git', str(checkout)], check=True)
        commit = subprocess.check_output(['git', '-C', str(checkout), 'rev-parse', 'HEAD'], text=True).strip()
        tag = subprocess.check_output(['git', '-C', str(checkout), 'rev-parse', '10.0.7^{commit}'], text=True).strip()
        if commit != tag:
            raise RuntimeError(f'Component checkout is not KiCad 10.0.7: {folder}')
        manifest[folder] = commit
    (destination / 'component-commits.json').write_text(json.dumps(manifest, indent=2) + '\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', type=Path, required=True)
    prepare(parser.parse_args().directory)
