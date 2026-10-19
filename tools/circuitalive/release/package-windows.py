"""Preserve a self-contained MSVC runtime, then build and test its x64 installer."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
CHECKPOINT = 'RUNTIME-CHECKPOINT.json'
LIBRARIES = ('symbols', 'footprints', '3dmodels', 'template')


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def runtime_files(stage):
    return {path.relative_to(stage).as_posix(): path for path in sorted(stage.rglob('*'))
            if path.is_file() and path.name != CHECKPOINT}


def preserve_runtime(stage, version):
    metadata = json.loads((stage / 'PACKAGE-INFO.json').read_text(encoding='utf-8'))
    files = runtime_files(stage)
    checkpoint = {'version': version, 'commit': metadata['commit'],
                  'bytes': sum(path.stat().st_size for path in files.values()),
                  'files': {name: digest(path) for name, path in files.items()}}
    (stage / CHECKPOINT).write_text(json.dumps(checkpoint, indent=2) + '\n', encoding='utf-8')
    print(f'Runtime checkpoint: {len(files)} files, {checkpoint["bytes"]:,} bytes', flush=True)


def validate_runtime(stage, version):
    checkpoint = json.loads((stage / CHECKPOINT).read_text(encoding='utf-8'))
    if checkpoint['version'] != version:
        raise ValueError('Runtime checkpoint version differs from the requested installer')
    files = runtime_files(stage)
    if files.keys() != checkpoint['files'].keys():
        raise ValueError('Runtime checkpoint has missing or unexpected files')
    for name, path in files.items():
        if digest(path) != checkpoint['files'][name]:
            raise ValueError(f'Runtime checkpoint checksum mismatch: {name}')
    metadata = json.loads((stage / 'PACKAGE-INFO.json').read_text(encoding='utf-8'))
    if metadata['version'] != version or metadata['commit'] != checkpoint['commit']:
        raise ValueError('Runtime checkpoint provenance mismatch')
    return checkpoint


def prepare_runtime(stage, prefix, version, root=ROOT):
    if list((prefix / 'bin').glob('libgcc*.dll')) or list((prefix / 'lib').glob('*.dll.a')):
        raise ValueError('MinGW libraries cannot be packaged with MSVC')

    def copy_dependency(source, destination):
        if not Path(destination).exists():
            shutil.copy2(source, destination)
        return str(destination)

    for dll in (prefix / 'bin').glob('*.dll'):
        if dll.name.lower() not in {'vulkan-1.dll', 'spirv-tools-shared.dll'}:
            copy_dependency(dll, stage / 'bin' / dll.name)
    python_root = prefix / 'tools/python3'
    if not (python_root / 'python.exe').is_file():
        raise ValueError('Missing vcpkg Python runtime')
    shutil.copytree(python_root, stage / 'bin', dirs_exist_ok=True, copy_function=copy_dependency,
                    ignore=shutil.ignore_patterns('__pycache__', '*.pyc', '*.pdb'))
    for directory in ('etc', 'share/locale', 'share/ngspice'):
        if (prefix / directory).is_dir():
            shutil.copytree(prefix / directory, stage / directory, dirs_exist_ok=True)
    shutil.copytree(prefix / 'lib/ngspice', stage / 'lib/ngspice', dirs_exist_ok=True)
    licenses = stage / 'share/licenses/dependencies'
    licenses.mkdir(parents=True, exist_ok=True)
    for copyright_file in (prefix / 'share').glob('*/copyright'):
        target = licenses / copyright_file.parent.name
        target.mkdir(exist_ok=True)
        shutil.copy2(copyright_file, target / 'copyright')
    redist = Path(os.environ['VCToolsRedistDir']) / 'x64/Microsoft.VC143.CRT'
    if not redist.is_dir():
        raise ValueError('Missing MSVC v143 redistributable DLLs')
    for dll in redist.glob('*.dll'):
        copy_dependency(dll, stage / 'bin' / dll.name)
    for license in ('LICENSE', 'LICENSE.README', 'LICENSE.GPLv3', 'LICENSE.GPLv2', 'AUTHORS.txt'):
        if (root / license).is_file():
            shutil.copy2(root / license, stage / license)
    for executable in ('CircuitAliveEDA.exe', 'kicad-cli.exe', 'circuitalive-sim-worker.exe', 'python.exe'):
        if not (stage / 'bin' / executable).is_file():
            raise ValueError(f'Missing installed runtime executable: {executable}')
    models = stage / 'bin/ngspice'
    models.mkdir(exist_ok=True)
    for file in (prefix / 'lib/ngspice').glob('*.cm'):
        shutil.copy2(file, models / file.name)
    (stage / 'PACKAGE-INFO.json').write_text(json.dumps({
        'product': 'CircuitAliveEDA', 'version': version,
        'commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], text=True, cwd=root).strip(),
        'platform': 'windows-x86_64', 'toolchain': 'MSVC-v143', 'dependencies': 'vcpkg.json',
    }, indent=2) + '\n', encoding='utf-8')
    shutil.copy2(prefix.parent / 'vcpkg/status', stage / 'DEPENDENCIES.txt')
    preserve_runtime(stage, version)


def inno_quote(value):
    return str(value).replace('{', '{{').replace('"', '""')


def installer_script(stage, libraries, version, dist, root=ROOT):
    extra = []
    for folder in LIBRARIES:
        source = libraries / 'share/kicad' / folder
        if not source.is_dir():
            raise ValueError(f'Missing component library: {source}')
        extra.append(f'Source: "{inno_quote(source)}\\*"; DestDir: "{{app}}\\share\\kicad\\{folder}"; '
                     'Excludes: ".git,.git\\*,.history,*-backups"; Flags: ignoreversion recursesubdirs createallsubdirs')
    for manifest in sorted(libraries.glob('*-commit.txt')):
        extra.append(f'Source: "{inno_quote(manifest)}"; DestDir: "{{app}}"; Flags: ignoreversion')
    return f'''[Setup]
AppId=CircuitAliveEDA
AppName=CircuitAliveEDA
AppVersion={version}
VersionInfoVersion={version.split('-')[0]}
SetupArchitecture=x64
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
DefaultDirName={{localappdata}}\\Programs\\CircuitAliveEDA
DefaultGroupName=CircuitAliveEDA
PrivilegesRequired=lowest
DisableProgramGroupPage=yes
LicenseFile={inno_quote(root / 'LICENSE.GPLv3')}
OutputDir={inno_quote(dist)}
OutputBaseFilename=CircuitAliveEDA-{version}-windows-x86_64-setup
Compression=lzma2/max
SolidCompression=yes
CompressionThreads=2
LZMANumBlockThreads=1
DiskSpanning=no
UninstallDisplayIcon={{app}}\\bin\\CircuitAliveEDA.exe
SetupLogging=yes

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "brazilianportuguese"; MessagesFile: "compiler:Languages\\BrazilianPortuguese.isl"

[Files]
Source: "{inno_quote(stage)}\\*"; DestDir: "{{app}}"; Excludes: ".git,.git\\*,.history,*-backups,{CHECKPOINT}"; Flags: ignoreversion recursesubdirs createallsubdirs
{chr(10).join(extra)}

[Icons]
Name: "{{group}}\\CircuitAliveEDA"; Filename: "{{app}}\\bin\\CircuitAliveEDA.exe"
Name: "{{userdesktop}}\\CircuitAliveEDA"; Filename: "{{app}}\\bin\\CircuitAliveEDA.exe"
'''


def verify_installer(installer, root=ROOT):
    smoke = root / 'installer smoke'
    with tempfile.TemporaryDirectory(prefix='ca-installed-profile-') as temporary:
        profile = Path(temporary)
        env = dict(os.environ, APPDATA=str(profile / 'Roaming'), LOCALAPPDATA=str(profile / 'Local'),
                   USERPROFILE=str(profile), PYTHONHOME=str(smoke / 'bin'),
                   PYTHONPATH=str(smoke / 'bin/Lib/site-packages'),
                   PATH=os.pathsep.join((str(smoke / 'bin'), str(Path(os.environ['SystemRoot']) / 'System32'),
                                         os.environ['SystemRoot'])))
        for path in (profile / 'Roaming', profile / 'Local'):
            path.mkdir()
        subprocess.run([str(installer), '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART',
                        '/SP-', f'/DIR={smoke}', f'/LOG={root / "dist/installer-smoke.log"}'],
                       check=True, timeout=1800)
        subprocess.run([str(smoke / 'bin/kicad-cli.exe'), 'version'], check=True, timeout=60,
                       env=env, cwd=profile)
        python = str(smoke / 'bin/python.exe')
        subprocess.run([python, '-c', 'import wx, pcbnew; print("Installed Python/wx:", wx.version())'],
                       check=True, timeout=60, env=env, cwd=profile)
        subprocess.run([python, str(root / 'tools/circuitalive/release/prepare-runtime.py'),
                        str(smoke / 'bin'), str(smoke / 'bin/ngspice')], check=True, env=env, cwd=profile)
        subprocess.run([python, str(root / 'tools/circuitalive/release/smoke-worker.py'),
                        str(smoke / 'bin/circuitalive-sim-worker.exe'), '--scripts',
                        str(smoke / 'bin/ngspice/scripts')], check=True, timeout=150, env=env, cwd=profile)


def package_runtime(stage, version, compiler, root=ROOT):
    checkpoint = validate_runtime(stage, version)
    dist = root / 'dist'
    dist.mkdir(exist_ok=True)
    libraries = root / '.circuitalive/libraries'
    script = dist / 'installer.iss'
    script.write_text(installer_script(stage, libraries, version, dist, root), encoding='utf-8-sig')
    report = {'compiler': 'Inno Setup x64', 'source_commit': checkpoint['commit'],
              'runtime_bytes': checkpoint['bytes'], 'libraries': {}}
    for folder in LIBRARIES:
        sizes = [p.stat().st_size for p in (libraries / 'share/kicad' / folder).rglob('*')
                 if p.is_file() and '.git' not in p.parts]
        report['libraries'][folder] = {'files': len(sizes), 'bytes': sum(sizes)}
    (dist / 'windows-package-report.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(report, indent=2), flush=True)
    installer = dist / f'CircuitAliveEDA-{version}-windows-x86_64-setup.exe'
    installer.with_suffix('.exe.sha256').unlink(missing_ok=True)
    subprocess.run([compiler, '-Qp', str(script)], check=True)
    verify_installer(installer, root)
    # A checksum denotes a verified installer; failed smoke tests cannot be published.
    installer.with_suffix('.exe.sha256').write_text(f'{digest(installer)}  {installer.name}\n', encoding='utf-8')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('stage', type=Path)
    parser.add_argument('prefix', type=Path, nargs='?')
    parser.add_argument('--version', required=True)
    parser.add_argument('--prepare-only', action='store_true')
    parser.add_argument('--verify-only', action='store_true')
    parser.add_argument('--compiler', default=os.environ.get('INNO_ISCC'))
    args = parser.parse_args()
    if not re.fullmatch(r'[0-9]+\.[0-9]+\.[0-9]+(?:-[a-zA-Z0-9.]+)?', args.version):
        parser.error('Invalid release version')
    stage = args.stage.resolve()
    if args.verify_only:
        validate_runtime(stage, args.version)
        return
    if args.prefix:
        prepare_runtime(stage, args.prefix.resolve(), args.version)
    if args.prepare_only:
        if not args.prefix:
            parser.error('--prepare-only requires the dependency prefix')
        return
    if not args.compiler:
        parser.error('Set INNO_ISCC to the native x64 Inno Setup ISCC.exe')
    package_runtime(stage, args.version, args.compiler)


if __name__ == '__main__':
    main()
