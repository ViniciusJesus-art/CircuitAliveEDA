#!/usr/bin/env python3
"""Turn the packaged Fedora application into an AppImage and test its entry point."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
from component_libraries import validate_installed_assets, verify_first_start


def create_appimage(appdir, output, version, tool, runtime):
    appdir, output, tool, runtime = (Path(p).resolve() for p in (appdir, output, tool, runtime))
    validate_installed_assets(appdir)
    for required in (appdir / 'CircuitAliveEDA',
                     appdir / 'share/icons/hicolor/128x128/apps/circuitaliveeda.png', tool, runtime):
        if not required.is_file():
            raise FileNotFoundError(required)
    # The existing launcher configures Python, libraries and ngspice after relocation.
    entry = appdir / 'AppRun'
    entry.write_text('#!/usr/bin/env bash\nset -Eeuo pipefail\n'
                     'appdir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd -P)"\n'
                     'exec "$appdir/CircuitAliveEDA" "$@"\n')
    entry.chmod(0o755)
    (appdir / 'circuitaliveeda.desktop').write_text(
        '[Desktop Entry]\nType=Application\nName=CircuitAliveEDA\nExec=AppRun %F\n'
        'Icon=circuitaliveeda\nCategories=Development;Electronics;\nTerminal=false\n')
    shutil.copy2(appdir / 'share/icons/hicolor/128x128/apps/circuitaliveeda.png',
                 appdir / 'circuitaliveeda.png')
    output.parent.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, ARCH='x86_64', VERSION=version, APPIMAGE_EXTRACT_AND_RUN='1')
    subprocess.run([str(tool), '--no-appstream', '--runtime-file', str(runtime),
                    '--mksquashfs-opt=-processors', '--mksquashfs-opt=2',
                    '--mksquashfs-opt=-mem', '--mksquashfs-opt=1G',
                    str(appdir), str(output)], check=True, env=env)
    output.chmod(0o755)
    # Extraction avoids requiring FUSE or a desktop session on the hosted runner.
    with tempfile.TemporaryDirectory(prefix='circuitalive-appimage-smoke-') as smoke:
        smoke_env = dict(env, XDG_CACHE_HOME=smoke + '/cache', XDG_DATA_HOME=smoke + '/data',
                         KICAD_CONFIG_HOME=smoke + '/config', KICAD_CACHE_HOME=smoke + '/cache',
                         KICAD_DOCUMENTS_HOME=smoke + '/documents', PATH='/usr/bin:/bin')
        for key in ('LD_LIBRARY_PATH', 'PYTHONHOME', 'PYTHONPATH', 'KICAD_RUN_FROM_BUILD_DIR'):
            smoke_env.pop(key, None)
        subprocess.run([str(output), 'cli', 'version'], check=True, env=smoke_env, timeout=180)
        verify_first_start(smoke + '/config')
    return output


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('appdir', 'output', 'tool', 'runtime'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--version', required=True)
    args = parser.parse_args()
    print(create_appimage(args.appdir, args.output, args.version, args.tool, args.runtime))
