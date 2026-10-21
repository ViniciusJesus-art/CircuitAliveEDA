"""Offline publication checks: incomplete or corrupt packages must never reach GitHub."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools/circuitalive/release'))
SPEC = importlib.util.spec_from_file_location('publish_release', ROOT / 'tools/circuitalive/release/publish-release.py')
release = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(release)
DEB_SPEC = importlib.util.spec_from_file_location('package_deb', ROOT / 'tools/circuitalive/release/package-deb.py')
deb = importlib.util.module_from_spec(DEB_SPEC)
DEB_SPEC.loader.exec_module(deb)


class ReleaseArtifactsTests(unittest.TestCase):
    version = '10.0.7-alpha.2'
    commit = 'a' * 40

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.packages = []
        for suffix, header in (
            ('fedora44-x86_64.rpm', b'\xed\xab\xee\xdb'),
            ('linux-x86_64.AppImage', b'\x7fELF' + b'\0' * 4 + b'AI\x02'),
            ('ubuntu24.04-amd64.deb', b'!<arch>\n'),
            ('windows-x86_64-setup.exe', b'MZ'),
        ):
            package = self.directory / f'CircuitAliveEDA-{self.version}-{suffix}'
            package.write_bytes(header + b'package fixture')
            self.write_checksum(package)
            self.packages.append(package)

    def write_checksum(self, package):
        digest = hashlib.sha256(package.read_bytes()).hexdigest()
        package.with_suffix(package.suffix + '.sha256').write_text(f'{digest}  {package.name}\n')

    def test_all_formats_and_checksums_are_recorded_with_source_commit(self):
        assets, notes = release.verify_packages(self.directory, self.version, self.commit)
        manifest = json.loads((self.directory / 'release-manifest.json').read_text())
        self.assertEqual(len(assets), 9)
        self.assertEqual(manifest['commit'], self.commit)
        self.assertEqual(set(manifest['sha256']), {package.name for package in self.packages})
        self.assertIn('glibc 2.43', notes.read_text())
        self.assertIn('Ubuntu 24.04', notes.read_text())

    def test_individual_build_requires_only_the_selected_package(self):
        for format, suffix in release.PACKAGE_SUFFIXES.items():
            with self.subTest(format=format), tempfile.TemporaryDirectory() as temporary:
                directory = Path(temporary)
                name = f'CircuitAliveEDA-{self.version}-{suffix}'
                for source in (self.directory / name, self.directory / (name + '.sha256')):
                    (directory / source.name).write_bytes(source.read_bytes())
                assets, notes = release.verify_packages(directory, self.version, self.commit, format)
                self.assertEqual(len(assets), 3)
                self.assertEqual(json.loads((directory / 'release-manifest.json').read_text())['formats'], [format])
                if format == 'exe':
                    self.assertNotIn('Fedora 44', notes.read_text())

    def test_linux_selection_does_not_require_windows(self):
        self.packages[-1].unlink()
        assets, _ = release.verify_packages(self.directory, self.version, self.commit, 'linux')
        self.assertEqual(len(assets), 7)

    def test_all_selection_requires_deb_before_any_publication(self):
        self.packages[2].unlink()
        with patch.object(release.subprocess, 'run') as remote:
            with self.assertRaises(FileNotFoundError):
                release.publish(self.directory, self.version, self.commit, 'owner/repo')
            remote.assert_not_called()

    def test_empty_unknown_and_duplicate_selections_are_rejected(self):
        for selection in ('', 'rpm,unknown', 'rpm,rpm', [], ['exe', 'deb', 'exe']):
            with self.subTest(selection=selection), self.assertRaises(ValueError):
                release.verify_packages(self.directory, self.version, self.commit, selection)

    def test_single_format_publication_uploads_only_selected_assets(self):
        with patch.object(release.subprocess, 'run') as remote:
            release.publish(self.directory, self.version, self.commit, 'owner/repo', 'deb')
        command = remote.call_args_list[0].args[0]
        self.assertTrue(any(arg.endswith('.deb') for arg in command))
        self.assertFalse(any(arg.endswith(('.rpm', '.AppImage', '.exe')) for arg in command))

    def test_missing_installer_prevents_every_remote_action(self):
        self.packages[-1].unlink()
        with patch.object(release.subprocess, 'run') as remote:
            with self.assertRaises(FileNotFoundError):
                release.publish(self.directory, self.version, self.commit, 'owner/repo')
            remote.assert_not_called()
        self.assertFalse((self.directory / 'release-manifest.json').exists())

    def test_corrupt_package_is_rejected(self):
        with self.packages[0].open('ab') as stream:
            stream.write(b'corruption')
        with self.assertRaisesRegex(ValueError, 'Checksum mismatch'):
            release.verify_packages(self.directory, self.version, self.commit)

    def test_checksum_must_identify_the_correct_package(self):
        sidecar = self.packages[0].with_suffix('.rpm.sha256')
        sidecar.write_text(sidecar.read_text().replace(self.packages[0].name, 'another-version.rpm'))
        with self.assertRaisesRegex(ValueError, 'Checksum mismatch'):
            release.verify_packages(self.directory, self.version, self.commit)

    def test_html_download_cannot_be_published_as_an_appimage(self):
        self.packages[1].write_bytes(b'<html>Download failed</html>')
        self.write_checksum(self.packages[1])
        with self.assertRaisesRegex(ValueError, 'Invalid package format'):
            release.verify_packages(self.directory, self.version, self.commit)

    def test_version_and_full_source_sha_are_required(self):
        for version, commit in (('v' + self.version, self.commit), ('../bad', self.commit),
                                (self.version, 'main'), (self.version, 'a' * 7)):
            with self.subTest(version=version, commit=commit), self.assertRaises(ValueError):
                release.verify_packages(self.directory, version, commit)

    def test_failed_upload_never_publishes_the_draft(self):
        with patch.object(release.subprocess, 'run', side_effect=subprocess.CalledProcessError(1, 'gh')) as remote:
            with self.assertRaises(subprocess.CalledProcessError):
                release.publish(self.directory, self.version, self.commit, 'owner/repo')
            self.assertEqual(remote.call_count, 1)
            self.assertIn('--draft', remote.call_args.args[0])

    def test_publication_follows_successful_draft_upload(self):
        with patch.object(release.subprocess, 'run') as remote:
            release.publish(self.directory, self.version, self.commit, 'owner/repo')
        create, publish = (call.args[0] for call in remote.call_args_list)
        self.assertEqual(create[:4], ['gh', 'release', 'create', 'v' + self.version])
        self.assertIn('--draft', create)
        self.assertEqual(create[create.index('--target') + 1], self.commit)
        self.assertEqual(publish[:4], ['gh', 'release', 'edit', 'v' + self.version])
        self.assertIn('--draft=false', publish)


class DebianLauncherTests(unittest.TestCase):
    def test_relocated_launcher_routes_cli_and_configures_private_runtime(self):
        with tempfile.TemporaryDirectory(prefix='ca deb path with spaces ') as temporary:
            app = Path(temporary) / 'installation'
            (app / 'bin/ngspice').mkdir(parents=True)
            (app / 'bin/ngspice/digital.cm').write_text('test module')
            cli = app / 'bin/kicad-cli'
            cli.write_text('#!/usr/bin/env bash\nset -eu\n[[ "$1" == version ]]\n'
                           'printf "%s\\n" "$APPDIR" "$PYTHONPATH" "$SPICE_SCRIPTS"\n')
            cli.chmod(0o755)
            launcher = deb.prepare_launcher(app)
            env = dict(os.environ, XDG_CACHE_HOME=temporary + '/cache')
            result = subprocess.check_output([str(launcher), 'cli', 'version'], env=env, text=True).splitlines()
            self.assertEqual(result[0], str(app))
            self.assertTrue(result[1].startswith(str(app / 'lib/python3/dist-packages')))
            init = Path(result[2]) / 'spinit'
            self.assertIn(f"codemodel '{app}/bin/ngspice/digital.cm'", init.read_text())

    def test_system_entry_symlink_finds_its_actual_installation(self):
        with tempfile.TemporaryDirectory(prefix='ca deb relocation ') as temporary:
            app = Path(temporary) / 'opt/circuitaliveeda'
            (app / 'bin/ngspice').mkdir(parents=True)
            (app / 'bin/ngspice/digital.cm').touch()
            cli = app / 'bin/kicad-cli'
            cli.write_text('#!/usr/bin/env bash\nprintf "%s" "$APPDIR"\n')
            cli.chmod(0o755)
            launcher = deb.prepare_launcher(app)
            entry = Path(temporary) / 'circuitalive-eda'
            entry.symlink_to(launcher)
            env = dict(os.environ, XDG_CACHE_HOME=temporary + '/cache')
            result = subprocess.check_output([str(entry), 'cli', 'version'], env=env, text=True)
            self.assertEqual(result, str(app))


if __name__ == '__main__':
    unittest.main()
