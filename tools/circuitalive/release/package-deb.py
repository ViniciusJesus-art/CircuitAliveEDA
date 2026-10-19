#!/usr/bin/env python3
"""Package and exercise the native Ubuntu 24.04 amd64 installation."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


def debian_version(version):
    if not re.fullmatch(r'[0-9]+\.[0-9]+\.[0-9]+-alpha\.[0-9]+', version):
        raise ValueError('Expected an alpha version without v')
    # Debian sorts ~alpha before the corresponding final version.
    return version.replace('-alpha.', '~alpha.') + '-1'


def prepare_launcher(app):
    launcher = app / 'CircuitAliveEDA'
    launcher.write_text('''#!/usr/bin/env bash
set -Eeuo pipefail
launcher="$(readlink -f -- "${BASH_SOURCE[0]}")"
root="$(cd -- "$(dirname -- "$launcher")" && pwd -P)"
export LD_LIBRARY_PATH="$root/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export PYTHONPATH="$root/lib/python3/dist-packages${PYTHONPATH:+:$PYTHONPATH}"
export APPDIR="$root" KICAD_STOCK_DATA_HOME="$root/share/kicad"
export KICAD10_SYMBOL_DIR="$root/share/kicad/symbols"
export KICAD10_FOOTPRINT_DIR="$root/share/kicad/footprints"
export KICAD10_3DMODEL_DIR="$root/share/kicad/3dmodels"
export KICAD10_TEMPLATE_DIR="$root/share/kicad/template"
export CIRCUITALIVE_MODEL_DIR="$root/share/circuitalive/models"
export CIRCUITALIVE_BLOCK_DIR="$root/share/circuitalive/blocks"
export SPICE_SCRIPTS="${XDG_CACHE_HOME:-$HOME/.cache}/circuitaliveeda/ngspice"
mkdir -p "$SPICE_SCRIPTS"
{
 printf '* CircuitAliveEDA bundled ngspice 47\\nunset osdi_enabled\\nset num_threads=2\\n'
 for model in "$root"/bin/ngspice/*.cm; do printf "codemodel '%s'\\n" "$model"; done
} > "$SPICE_SCRIPTS/spinit"
app=kicad
case "${1:-}" in
 cli) app=kicad-cli; shift ;;
 eeschema|pcbnew|gerbview|bitmap2component|pcb_calculator|pl_editor|cvpcb) app="$1"; shift ;;
esac
exec "$root/bin/$app" "$@"
''')
    launcher.chmod(0o755)
    return launcher


def smoke(app, root):
    with tempfile.TemporaryDirectory(prefix='circuitalive-deb-smoke-') as temporary:
        env = dict(os.environ, XDG_CACHE_HOME=temporary + '/cache', XDG_DATA_HOME=temporary + '/data',
                   KICAD_CONFIG_HOME=temporary + '/config', KICAD_CACHE_HOME=temporary + '/cache',
                   KICAD_DOCUMENTS_HOME=temporary + '/documents')
        for key in ('LD_LIBRARY_PATH', 'PYTHONPATH', 'PYTHONHOME', 'KICAD_RUN_FROM_BUILD_DIR', 'SPICE_SCRIPTS'):
            env.pop(key, None)
        subprocess.run([str(app / 'CircuitAliveEDA'), 'cli', 'version'], check=True, env=env, timeout=60)
        env['LD_LIBRARY_PATH'] = str(app / 'lib')
        env['PYTHONPATH'] = str(app / 'lib/python3/dist-packages')
        subprocess.run(['python3', '-c', 'import wx, pcbnew; print(wx.version())'],
                       check=True, env=env, timeout=60)
        subprocess.run(['python3', str(root / 'tools/circuitalive/release/smoke-worker.py'),
                        str(app / 'bin/circuitalive-sim-worker'), '--scripts',
                        temporary + '/cache/circuitaliveeda/ngspice'], check=True, env=env, timeout=150)


def package(stage, libraries, ngspice, version, commit, output):
    package_version = debian_version(version)
    if not re.fullmatch(r'[0-9a-f]{40}', commit):
        raise ValueError('Expected the full source commit SHA')
    root = Path(__file__).resolve().parents[3]
    stage, libraries, ngspice, output = (Path(p).resolve() for p in (stage, libraries, ngspice, output))
    app = stage / 'opt/circuitaliveeda'
    for name in ('kicad', 'kicad-cli', 'eeschema', 'pcbnew', 'circuitalive-sim-worker'):
        if not (app / 'bin' / name).is_file():
            raise FileNotFoundError(app / 'bin' / name)
    ignore = shutil.ignore_patterns('.git', '__pycache__', '*.pyc', '.history')
    for folder in ('symbols', 'footprints', '3dmodels', 'template'):
        if not (libraries / folder).is_dir():
            raise FileNotFoundError(libraries / folder)
        shutil.copytree(libraries / folder, app / 'share/kicad' / folder, dirs_exist_ok=True, ignore=ignore)
    for pattern in ('libngspice.so*', 'libnng.so*'):
        sources = list((ngspice / 'lib').glob(pattern))
        if not sources:
            raise FileNotFoundError(f'Missing native runtime: {pattern}')
        for source in sources:
            dest = app / 'lib' / source.name
            dest.parent.mkdir(parents=True, exist_ok=True)
            if source.is_symlink():
                dest.unlink(missing_ok=True)
                dest.symlink_to(source.resolve().name)
            else:
                shutil.copy2(source, dest)
    if not (app / 'lib/libngspice.so.0').is_file():
        raise FileNotFoundError('Missing private ngspice 47 runtime')
    subprocess.run(['python3', str(root / 'tools/circuitalive/release/prepare-runtime.py'),
                    str(app / 'bin'), str(ngspice / 'lib/ngspice')], check=True)
    for alias, target in ((app / 'usr/bin', app / 'bin'), (app / 'usr/share', app / 'share'),
                          (app / 'usr/lib/x86_64-linux-gnu', app / 'lib')):
        alias.parent.mkdir(parents=True, exist_ok=True)
        if not alias.exists():
            alias.symlink_to(os.path.relpath(target, alias.parent), target_is_directory=True)
    prepare_launcher(app)
    for license in ('LICENSE', 'LICENSE.README', 'LICENSE.GPLv3', 'AUTHORS.txt'):
        shutil.copy2(root / license, app / license)
    ng_license = app / 'share/licenses/ngspice47'
    ng_license.mkdir(parents=True, exist_ok=True)
    ng_source = root / '.circuitalive/ngspice-ngspice-47'
    for name in ('COPYING', 'AUTHORS'):
        shutil.copy2(ng_source / name, ng_license / name)
    nng_license = app / 'share/licenses/nng190'
    nng_license.mkdir(parents=True, exist_ok=True)
    shutil.copy2(root / '.circuitalive/nng-1.9.0/LICENSE.txt', nng_license / 'LICENSE.txt')
    shutil.copy2(libraries / 'component-commits.json', app / 'component-commits.json')
    (app / 'PACKAGE-INFO.json').write_text(json.dumps(
        {'product': 'CircuitAliveEDA', 'version': version, 'commit': commit,
         'platform': 'ubuntu24.04-amd64', 'ngspice': '47', 'nng': '1.9.0'}, indent=2) + '\n')
    for binary in app.rglob('*'):
        if binary.is_file() and not binary.is_symlink():
            with binary.open('rb') as stream:
                elf = stream.read(4) == b'\x7fELF'
            if elf:
                subprocess.run(['strip', '--strip-unneeded', str(binary)], check=True)
    (stage / 'usr/bin').mkdir(parents=True, exist_ok=True)
    (stage / 'usr/bin/circuitalive-eda').symlink_to('/opt/circuitaliveeda/CircuitAliveEDA')
    desktop = stage / 'usr/share/applications/circuitaliveeda.desktop'
    desktop.parent.mkdir(parents=True, exist_ok=True)
    desktop.write_text('[Desktop Entry]\nType=Application\nName=CircuitAliveEDA\n'
                       'Exec=/usr/bin/circuitalive-eda %F\nIcon=circuitaliveeda\n'
                       'Categories=Development;Electronics;\nTerminal=false\n')
    icon = stage / 'usr/share/icons/hicolor/128x128/apps/circuitaliveeda.png'
    icon.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(app / 'share/icons/hicolor/128x128/apps/circuitaliveeda.png', icon)
    output.mkdir(parents=True, exist_ok=True)
    # Resolve the native Ubuntu dependency versions, including libraries used by ngspice.
    with tempfile.TemporaryDirectory(prefix='circuitalive-deb-control-') as temporary:
        work = Path(temporary)
        (work / 'debian').mkdir()
        (work / 'debian/control').write_text(
            'Source: circuitaliveeda\nMaintainer: CircuitAliveEDA contributors <noreply@github.com>\n\n'
            'Package: circuitaliveeda\nArchitecture: amd64\nDescription: CircuitAliveEDA\n')
        elf_files = []
        for file in app.rglob('*'):
            if file.is_file() and not file.is_symlink():
                with file.open('rb') as stream:
                    if stream.read(4) == b'\x7fELF':
                        elf_files.append(file)
        result = subprocess.check_output(
            ['dpkg-shlibdeps', '-O', '--ignore-missing-info', '-l' + str(app / 'lib'),
             *('-e' + str(file) for file in elf_files)], cwd=work, text=True)
        dependencies = next(line.split('=', 1)[1] for line in result.splitlines()
                            if line.startswith('shlibs:Depends='))
    metadata = stage / 'DEBIAN'
    metadata.mkdir(exist_ok=True)
    (metadata / 'control').write_text(
        f'Package: circuitaliveeda\nVersion: {package_version}\nArchitecture: amd64\n'
        'Maintainer: CircuitAliveEDA contributors <noreply@github.com>\nSection: electronics\nPriority: optional\n'
        f'Depends: {dependencies}, python3 (>= 3.12), python3 (<< 3.13), python3-wxgtk4.0\n'
        'Homepage: https://github.com/ViniciusJesus-art/CircuitAliveEDA\n'
        'Description: CircuitAliveEDA with interactive circuit simulation\n'
        ' Native Ubuntu 24.04 build with component libraries and ngspice 47.\n')
    target = output / f'CircuitAliveEDA-{version}-ubuntu24.04-amd64.deb'
    subprocess.run(['dpkg-deb', '--root-owner-group', '-Zxz', '-z6', '--build', str(stage), str(target)], check=True)
    subprocess.run(['dpkg-deb', '--info', str(target)], check=True)
    # Exercise the files actually shipped, in a fresh extraction without installing on the runner.
    with tempfile.TemporaryDirectory(prefix='circuitalive-deb-extract-') as temporary:
        subprocess.run(['dpkg-deb', '--extract', str(target), temporary], check=True)
        smoke(Path(temporary) / 'opt/circuitaliveeda', root)
    with target.open('rb') as stream:
        digest = hashlib.file_digest(stream, 'sha256').hexdigest()
    target.with_suffix('.deb.sha256').write_text(digest + '  ' + target.name + '\n')
    return target


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('stage', 'libraries', 'ngspice', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--version', required=True)
    parser.add_argument('--commit', required=True)
    args = parser.parse_args()
    print(package(args.stage, args.libraries, args.ngspice, args.version, args.commit, args.output))
