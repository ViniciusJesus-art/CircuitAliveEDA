#!/usr/bin/env python3
"""Build this platform and add its verified packages to a shared GitHub release."""
import argparse
import importlib.util
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
SPEC = importlib.util.spec_from_file_location('publish_release', Path(__file__).with_name('publish-release.py'))
packages = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(packages)
MARKER = '<!-- circuitalive-release '


def run(*args, **kwargs):
    return subprocess.run(list(args), cwd=ROOT, check=True, **kwargs)


def output(*args):
    return run(*args, capture_output=True, text=True).stdout.strip()


def api(repository, path):
    endpoint = f'repos/{repository}' + ('/' + path if path else '')
    return json.loads(output('gh', 'api', endpoint))


def remote_release(repository, tag):
    # Listing by tag also finds drafts. Unlike a failed view, this never treats
    # authentication/network errors as permission to create a new release.
    raw = output('gh', 'api', '--paginate', '--slurp',
                 f'repos/{repository}/releases?per_page=100')
    matches = [item for page in json.loads(raw) for item in page if item['tag_name'] == tag]
    return matches[0] if matches else None


def check_identity(existing, version):
    # The release name/tag is the identity. Commits and legacy body markers
    # do not constrain uploads from another machine or documentation revision.
    tag = packages.release_tag(packages.release_slug(version))
    if existing['tag_name'] != tag:
        raise ValueError('Release tag does not match the requested version.')
    if existing.get('immutable'):
        raise ValueError('This GitHub release is immutable; packages cannot be added. Use a new version.')


def upload(directory, version, commit, repository, formats):
    tag = packages.release_tag(packages.release_slug(version))
    # A fresh directory prevents stale aliases/checksum files from older builds.
    # Hardlinks avoid copying multi-GB packages and work on native NTFS too.
    with tempfile.TemporaryDirectory(prefix='.release-upload-', dir=directory) as temporary:
        staging = Path(temporary)
        for kind in packages.selected_formats(formats):
            name = f'CircuitAliveEDA-{version}-{packages.PACKAGE_SUFFIXES[kind]}'
            for filename in (name, name + '.sha256'):
                os.link(Path(directory) / filename, staging / filename)
        assets, _ = packages.verify_packages(staging, version, commit, formats)
        selected = [asset for asset in assets if asset.name != 'SHA256SUMS.txt']
        manifest = json.loads((staging / 'release-manifest.json').read_text())
        sidecars = []
        for asset in selected:
            checksum = staging / (asset.name + '.sha256')
            checksum.write_text(f"{manifest['sha256'][asset.name]}  {asset.name}\n")
            sidecars.append(checksum)
        existing = remote_release(repository, tag)
        if existing:
            check_identity(existing, version)
        else:
            notes = staging / 'notes.md'
            notes.write_text(
                f'CircuitAliveEDA {version}\n\n'
                'Installers are added independently as each platform finishes its build and tests. '
                'Builds are grouped by release version and may use different source commits. '
                'Each installer has its own SHA-256 checksum file.\n\n'
                + MARKER + json.dumps({'version': version}) + ' -->\n')
            branch = api(repository, '')['default_branch']
            run('gh', 'release', 'create', tag, '--repo', repository, '--target', branch,
                '--title', 'CircuitAliveEDA ' + version, '--draft', '--notes-file', str(notes))
        # Only this platform's assets are replaced. No shared checksum/index can
        # overwrite the checksums or packages uploaded from another machine.
        run('gh', 'release', 'upload', tag, '--repo', repository, '--clobber',
            *(str(asset) for asset in [*selected, *sidecars]))
        if not existing or existing['draft']:
            run('gh', 'release', 'edit', tag, '--repo', repository, '--draft=false')
    print(f'https://github.com/{repository}/releases/tag/{tag}')


def build(version, commit, jobs, engine=None):
    if platform.system() == 'Windows':
        for phase in ('prepare', 'build', 'libraries', 'package'):
            run('pwsh', '-NoProfile', '-File', str(ROOT / 'tools/circuitalive/dev-windows.ps1'),
                '-Phase', phase, '-Version', version, '-Jobs', str(jobs))
    elif platform.system() == 'Linux':
        engine = engine or ('podman' if shutil.which('podman') else 'docker')
        env = dict(os.environ, RELEASE_VERSION=version, RELEASE_COMMIT=commit,
                   RELEASE_FORMATS='rpm,appimage', CIRCUITALIVE_JOBS=str(jobs))
        run(engine, 'run', '--rm', '--user', '0',
            '-e', 'RELEASE_VERSION', '-e', 'RELEASE_COMMIT', '-e', 'RELEASE_FORMATS',
            '-e', 'CIRCUITALIVE_JOBS', '-v', f'{ROOT}:/work:Z', '-w', '/work',
            'fedora:44', 'bash', 'tools/circuitalive/release/build-linux.sh', env=env)
    else:
        raise ValueError('Run on Windows x64 or Linux x86_64.')


def receipt(directory, version, commit, formats, save=False):
    path = directory / ('build-source-windows.json' if formats == 'exe' else 'build-source-fedora.json')
    hashes = {}
    for kind in packages.selected_formats(formats):
        name = f'CircuitAliveEDA-{version}-{packages.PACKAGE_SUFFIXES[kind]}'
        with (directory / name).open('rb') as stream:
            hashes[name] = hashlib.file_digest(stream, 'sha256').hexdigest()
    identity = {'version': version, 'commit': commit, 'sha256': hashes}
    if save:
        path.write_text(json.dumps(identity, indent=2) + '\n')
        return commit
    saved = json.loads(path.read_text()) if path.exists() else {}
    if saved.get('version') != version or saved.get('sha256') != hashes:
        raise ValueError('Packages do not match this version/build receipt. Run again without --upload-only.')
    return saved['commit']  # Build provenance only; a newer checkout can resend it.


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('version', help='Same version on both machines, e.g. 10.0.7-alpha.4.2')
    parser.add_argument('--repository', default='ViniciusJesus-art/CircuitAliveEDA')
    parser.add_argument('--upload-only', action='store_true', help='Retry sending existing dist packages without building')
    parser.add_argument('--jobs', type=int, choices=(1, 2), default=2)
    parser.add_argument('--engine', choices=('docker', 'podman'))
    args = parser.parse_args()
    packages.parse_version(args.version)
    if platform.system() not in ('Windows', 'Linux') or platform.machine().lower() not in ('amd64', 'x86_64'):
        parser.error('Run on Windows x64 or Linux x86_64.')
    if not re.fullmatch(r'[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+', args.repository):
        parser.error('Repository must be owner/name.')
    if output('git', 'status', '--porcelain', '--untracked-files=normal'):
        parser.error('Commit your changes before building/publishing a release.')
    commit = output('git', 'rev-parse', 'HEAD')
    run('gh', 'auth', 'status')
    tag = packages.release_tag(packages.release_slug(args.version))
    existing = remote_release(args.repository, tag)
    if existing:
        check_identity(existing, args.version)
    directory = ROOT / 'dist'
    directory.mkdir(exist_ok=True)
    formats = 'exe' if platform.system() == 'Windows' else 'rpm,appimage'
    if not args.upload_only:
        build(args.version, commit, args.jobs, args.engine)
    # Do not label binaries with a revision that changed during a long build.
    if output('git', 'rev-parse', 'HEAD') != commit or output('git', 'status', '--porcelain', '--untracked-files=normal'):
        parser.error('Checkout changed during the build; nothing was uploaded.')
    build_commit = receipt(directory, args.version, commit, formats, save=not args.upload_only)
    upload(directory, args.version, build_commit, args.repository, formats)


if __name__ == '__main__':
    try:
        main()
    except (ValueError, OSError, subprocess.CalledProcessError) as error:
        raise SystemExit(str(error)) from error
