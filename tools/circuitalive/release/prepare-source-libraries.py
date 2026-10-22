#!/usr/bin/env python3
"""Build and install KiCad 10.0.7 component data for Windows and Linux packages."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import shutil
import sys
from component_libraries import validate_component_libraries

VERSION = '10.0.7'
REPOSITORIES = ('kicad-symbols', 'kicad-footprints', 'kicad-packages3D', 'kicad-templates')


def install_repository(checkout, build, prefix):
    # Install rules pack symbols and place their tables into template/. A checkout is not
    # an installed library tree, even when the component files are already present.
    subprocess.run(['cmake', '-S', str(checkout), '-B', str(build), '-G', 'Ninja',
                    '-DCMAKE_BUILD_TYPE=Release', f'-DCMAKE_INSTALL_PREFIX={prefix}',
                    '-DKICAD_DATA=share/kicad', '-DKICAD_PACK_SYM_LIBRARIES=ON',
                    f'-DPython3_EXECUTABLE={sys.executable}'], check=True)
    subprocess.run(['cmake', '--build', str(build), '--parallel', '2'], check=True,
                   env=dict(os.environ, PYTHONUTF8='1'))
    subprocess.run(['cmake', '--install', str(build)], check=True)


def prepare(destination):
    destination = Path(destination).resolve()
    destination.mkdir(parents=True, exist_ok=True)
    work = destination.parent / 'component-sources'
    prefix = work / 'installed'
    if prefix.exists():
        shutil.rmtree(prefix)
    manifest = {}
    for repo in REPOSITORIES:
        checkout = work / repo
        if not checkout.exists():
            checkout.parent.mkdir(parents=True, exist_ok=True)
            subprocess.run(['git', 'clone', '--depth', '1', '--branch', VERSION,
                            f'https://gitlab.com/kicad/libraries/{repo}.git', str(checkout)], check=True)
        commit = subprocess.check_output(['git', '-C', str(checkout), 'rev-parse', 'HEAD'], text=True).strip()
        tag = subprocess.check_output(['git', '-C', str(checkout), 'rev-parse', VERSION + '^{commit}'], text=True).strip()
        if commit != tag:
            raise RuntimeError(f'Component checkout is not KiCad {VERSION}: {repo}')
        install_repository(checkout, work / (repo + '-build'), prefix)
        licenses = prefix / 'share/kicad/licenses' / repo
        licenses.mkdir(parents=True, exist_ok=True)
        for pattern in ('LICENSE*', 'COPYING*', 'README*'):
            for file in checkout.glob(pattern):
                if file.is_file():
                    shutil.copy2(file, licenses / file.name)
        manifest[repo] = commit
    installed = prefix / 'share/kicad'
    validate_component_libraries(installed)
    # Move the verified install tree rather than keeping a third copy of the large 3D payload.
    # Remove previous generated checkouts so source files and stale tables cannot leak in.
    for source in installed.iterdir():
        target = destination / source.name
        if target.is_dir():
            shutil.rmtree(target)
        elif target.exists():
            target.unlink()
        shutil.move(str(source), str(target))
    (destination / 'component-commits.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    validate_component_libraries(destination)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--directory', type=Path, required=True)
    prepare(parser.parse_args().directory)
