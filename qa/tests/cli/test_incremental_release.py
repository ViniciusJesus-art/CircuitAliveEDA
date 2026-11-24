"""Offline tests for sending packages independently from each build machine."""
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import zipfile
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools/circuitalive/release'))
SPEC = importlib.util.spec_from_file_location('incremental_release', ROOT / 'tools/circuitalive/release/release.py')
release = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(release)


class IncrementalReleaseTests(unittest.TestCase):
    version = '10.0.7-alpha.4.2'
    commit = 'a' * 40

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)
        self.exe = self.directory / f'CircuitAliveEDA-{self.version}-windows-x86_64-setup.exe'
        self.exe.write_bytes(b'MZ fixture')
        self.exe.with_suffix('.exe.sha256').write_text(
            hashlib.sha256(self.exe.read_bytes()).hexdigest() + '  ' + self.exe.name + '\n')
        self.identity = {'version': self.version, 'commit': self.commit}
        self.existing = {'tag_name': 'v' + self.version, 'draft': False,
                         'body': release.MARKER + json.dumps(self.identity) + ' -->'}

    def upload(self):
        release.upload(self.directory, self.version, self.commit, 'owner/repo', 'exe')

    def test_missing_github_cli_explains_installation_and_login(self):
        for system, install in [('Linux', 'sudo dnf install gh'),
                                ('Windows', 'winget install --id GitHub.cli --exact')]:
            with self.subTest(system=system), \
                 patch.object(release.platform, 'system', return_value=system), \
                 patch.object(release.subprocess, 'run', side_effect=FileNotFoundError('gh')):
                with self.assertRaises(ValueError) as error:
                    release.run('gh', 'auth', 'status')
                self.assertIn(install, str(error.exception))
                self.assertIn('gh auth login', str(error.exception))

    def test_first_machine_creates_draft_uploads_and_publishes(self):
        with patch.object(release, 'remote_release', return_value=None), \
             patch.object(release, 'api', return_value={'default_branch': 'main'}), patch.object(release, 'run') as run:
            self.upload()
        commands = [call.args for call in run.call_args_list]
        self.assertEqual([cmd[2] for cmd in commands], ['create', 'upload', 'edit'])
        self.assertIn('--draft', commands[0])
        self.assertIn('--target', commands[0])
        self.assertIn('--draft=false', commands[-1])
        upload = commands[1]
        self.assertTrue(any(str(item).endswith('-auxiliares.zip') for item in upload))
        self.assertFalse(any(str(item).endswith('.sha256') for item in upload))
        self.assertFalse(any(str(item).endswith(('SHA256SUMS.txt', '.rpm')) for item in upload))

    def test_second_machine_only_adds_its_packages(self):
        with patch.object(release, 'remote_release', return_value=self.existing), \
             patch.object(release, 'api', return_value={'object': {'type': 'commit', 'sha': self.commit}}), \
             patch.object(release, 'run') as run:
            self.upload()
        self.assertEqual(run.call_count, 1)
        self.assertEqual(run.call_args.args[:3], ('gh', 'release', 'upload'))
        self.assertNotIn('--notes-file', run.call_args.args)

    def test_other_commit_and_legacy_or_missing_body_do_not_block_same_release(self):
        for body in (release.MARKER + json.dumps(dict(self.identity, commit='b' * 40)) + ' -->',
                     release.MARKER + json.dumps({'version': 'old'}) + ' -->', '', 'User release notes'):
            with self.subTest(body=body), \
                 patch.object(release, 'remote_release', return_value=dict(self.existing, body=body)), \
                 patch.object(release, 'api') as api, \
                 patch.object(release, 'run') as run:
                self.upload()
                self.assertEqual(run.call_args.args[:3], ('gh', 'release', 'upload'))
                api.assert_not_called()  # No commit/tag resolution is required.

    def test_wrong_release_tag_and_immutable_release_are_rejected(self):
        for changes in ({'tag_name': 'v10.0.7-1'}, {'immutable': True}):
            with self.subTest(changes=changes), \
                 patch.object(release, 'remote_release', return_value=dict(self.existing, **changes)), \
                 patch.object(release, 'run') as run:
                with self.assertRaises(ValueError):
                    self.upload()
                run.assert_not_called()

    def test_failed_upload_leaves_new_release_as_draft(self):
        def fail_upload(*args, **kwargs):
            if args[2] == 'upload':
                raise subprocess.CalledProcessError(1, args)
        with patch.object(release, 'remote_release', return_value=None), \
             patch.object(release, 'api', return_value={'default_branch': 'main'}), \
             patch.object(release, 'run', side_effect=fail_upload) as run:
            with self.assertRaises(subprocess.CalledProcessError):
                self.upload()
        self.assertFalse(any(call.args[2] == 'edit' for call in run.call_args_list))

    def test_network_failure_does_not_create_release(self):
        with patch.object(release, 'remote_release', side_effect=subprocess.CalledProcessError(1, 'gh')), \
             patch.object(release, 'run') as run:
            with self.assertRaises(subprocess.CalledProcessError):
                self.upload()
            run.assert_not_called()

    def test_corrupt_package_never_contacts_github(self):
        self.exe.write_bytes(b'MZ corrupted')
        with patch.object(release, 'remote_release') as remote:
            with self.assertRaisesRegex(ValueError, 'Checksum mismatch'):
                self.upload()
            remote.assert_not_called()

    def test_retry_requires_matching_build_receipt_and_package_hash(self):
        release.receipt(self.directory, self.version, self.commit, 'exe', save=True)
        release.receipt(self.directory, self.version, self.commit, 'exe')
        self.assertEqual(release.receipt(self.directory, self.version, 'b' * 40, 'exe'), self.commit)
        self.exe.write_bytes(b'MZ changed')
        with self.assertRaisesRegex(ValueError, 'do not match'):
            release.receipt(self.directory, self.version, self.commit, 'exe')

    def test_windows_build_runs_all_packaging_phases(self):
        with patch.object(release.platform, 'system', return_value='Windows'), \
             patch.object(release, 'run') as run:
            release.build(self.version, self.commit, 2)
        self.assertEqual([call.args[call.args.index('-Phase') + 1] for call in run.call_args_list],
                         ['prepare', 'build', 'libraries', 'package'])
        self.assertTrue(all(self.version in call.args for call in run.call_args_list))

    def test_fedora_build_passes_same_commit_version_and_formats(self):
        with patch.object(release.platform, 'system', return_value='Linux'), \
             patch.object(release, 'run') as run:
            release.build(self.version, self.commit, 2, 'podman')
        self.assertEqual(run.call_args.args[0], 'podman')
        self.assertIn('fedora:44', run.call_args.args)
        env = run.call_args.kwargs['env']
        self.assertEqual((env['RELEASE_VERSION'], env['RELEASE_COMMIT'], env['RELEASE_FORMATS']),
                         (self.version, self.commit, 'rpm,appimage'))

    def test_draft_from_failed_upload_can_be_resumed(self):
        with patch.object(release, 'remote_release', return_value=dict(self.existing, draft=True)), \
             patch.object(release, 'api', return_value={'default_branch': 'main'}), \
             patch.object(release, 'run') as run:
            self.upload()
        self.assertEqual([call.args[2] for call in run.call_args_list], ['upload', 'edit'])

    def test_creation_uses_default_branch_without_requiring_local_commit_on_github(self):
        with patch.object(release, 'remote_release', return_value=None), \
             patch.object(release, 'api', return_value={'default_branch': 'main'}) as api, \
             patch.object(release, 'run') as run:
            self.upload()
        create = run.call_args_list[0].args
        self.assertEqual(create[create.index('--target') + 1], 'main')
        api.assert_called_once_with('owner/repo', '')

    def test_cli_accepts_uncommitted_files_and_does_not_require_a_clean_checkout(self):
        def git_output(*args):
            if args == ('git', 'status', '--porcelain', '--untracked-files=normal'):
                return ' M source.cpp\n?? new-model.casim'
            self.assertEqual(args, ('git', 'rev-parse', 'HEAD'))
            return self.commit
        with patch.object(sys, 'argv', ['release.py', self.version]), \
             patch.object(release, 'ROOT', self.directory), \
             patch.object(release.platform, 'system', return_value='Linux'), \
             patch.object(release.platform, 'machine', return_value='x86_64'), \
             patch.object(release, 'output', side_effect=git_output), \
             patch.object(release, 'run'), \
             patch.object(release, 'remote_release', return_value=None), \
             patch.object(release, 'build') as build, \
             patch.object(release, 'receipt', return_value=self.commit), \
             patch.object(release, 'upload') as upload:
            release.main()
        build.assert_called_once_with(self.version, self.commit, 2, None)
        upload.assert_called_once_with(self.directory / 'dist', self.version, self.commit,
                                       'ViniciusJesus-art/CircuitAliveEDA', 'rpm,appimage')

    def test_cli_build_upload_and_retry_use_current_platform_and_revision(self):
        for system, formats in [('Linux', 'rpm,appimage'), ('Windows', 'exe')]:
            for retry in (False, True):
                with self.subTest(system=system, retry=retry), \
                     patch.object(sys, 'argv', ['release.py', self.version] + (['--upload-only'] if retry else [])), \
                     patch.object(release, 'ROOT', self.directory), \
                     patch.object(release.platform, 'system', return_value=system), \
                     patch.object(release.platform, 'machine', return_value='AMD64'), \
                     patch.object(release, 'output', side_effect=[self.commit, self.commit]), \
                     patch.object(release, 'run'), patch.object(release, 'api'), \
                     patch.object(release, 'remote_release', return_value=None), \
                     patch.object(release, 'build') as build, \
                     patch.object(release, 'receipt', return_value=self.commit) as receipt, \
                     patch.object(release, 'upload') as upload:
                    release.main()
                self.assertEqual(build.call_count, 0 if retry else 1)
                receipt.assert_called_once_with(self.directory / 'dist', self.version, self.commit,
                                                formats, save=not retry)
                upload.assert_called_once_with(self.directory / 'dist', self.version, self.commit,
                                               'ViniciusJesus-art/CircuitAliveEDA', formats)

    def test_source_changed_during_build_is_not_uploaded(self):
        with patch.object(sys, 'argv', ['release.py', self.version]), \
             patch.object(release, 'ROOT', self.directory), \
             patch.object(release.platform, 'system', return_value='Linux'), \
             patch.object(release.platform, 'machine', return_value='x86_64'), \
             patch.object(release, 'output', side_effect=[self.commit, 'b' * 40]), \
             patch.object(release, 'run'), patch.object(release, 'api'), \
             patch.object(release, 'remote_release', return_value=None), \
             patch.object(release, 'build'), patch.object(release, 'upload') as upload:
            with self.assertRaises(SystemExit):
                release.main()
            upload.assert_not_called()

    def test_fedora_and_windows_from_different_commits_join_the_same_release(self):
        for kind, header in [('rpm', b'\xed\xab\xee\xdb'),
                             ('appimage', b'\x7fELF' + b'\0' * 4 + b'AI\x02')]:
            package = self.directory / f'CircuitAliveEDA-{self.version}-{release.packages.PACKAGE_SUFFIXES[kind]}'
            package.write_bytes(header + b' fixture')
            package.with_suffix(package.suffix + '.sha256').write_text(
                hashlib.sha256(package.read_bytes()).hexdigest() + '  ' + package.name + '\n')
        with patch.object(release, 'remote_release', side_effect=[None, self.existing]), \
             patch.object(release, 'api', return_value={'default_branch': 'main'}), \
             patch.object(release, 'run') as run:
            release.upload(self.directory, self.version, self.commit, 'owner/repo', 'rpm,appimage')
            release.upload(self.directory, self.version, 'b' * 40, 'owner/repo', 'exe')
        commands = [call.args for call in run.call_args_list]
        self.assertEqual([command[2] for command in commands], ['create', 'upload', 'edit', 'upload'])
        self.assertEqual({command[3] for command in commands}, {'v' + self.version})
        fedora, windows = commands[1], commands[-1]
        self.assertTrue(any(str(item).endswith('.rpm') for item in fedora))
        self.assertTrue(any(str(item).endswith('.AppImage') for item in fedora))
        self.assertFalse(any(str(item).endswith('.exe') for item in fedora))
        self.assertTrue(any(str(item).endswith('.exe') for item in windows))
        self.assertFalse(any(str(item).endswith(('.rpm', '.AppImage')) for item in windows))

    def test_auxiliary_archive_preserves_other_platform_metadata_and_checksums(self):
        for previous_kind, current_kind in [('rpm', 'exe'), ('exe', 'rpm')]:
            with self.subTest(previous=previous_kind):
                previous_name = release.packages.package_filename(self.version, previous_kind)
                previous_records = {previous_kind: {'version': self.version, 'commit': 'b' * 40,
                    'asset': previous_name, 'sha256': '1' * 64}}
                previous_zip = release.packages.write_auxiliary(self.directory, self.version, previous_records)
                archived = previous_zip.read_bytes()
                if current_kind == 'rpm':
                    source = self.directory / release.packages.package_filename(self.version, 'rpm')
                    source.write_bytes(b'\xed\xab\xee\xdb rpm fixture')
                    source.with_suffix('.rpm.sha256').write_text(
                        hashlib.sha256(source.read_bytes()).hexdigest() + '  ' + source.name + '\n')
                captured = {}
                def github(*args, **kwargs):
                    if args[2] == 'download':
                        target = Path(args[args.index('--dir') + 1]) / previous_zip.name
                        target.write_bytes(archived)
                    elif args[2] == 'upload':
                        archive_path = Path(next(arg for arg in args if str(arg).endswith('-auxiliares.zip')))
                        captured.update(release.packages.read_auxiliary(archive_path, self.version))
                        with zipfile.ZipFile(archive_path) as archive:
                            captured['sums'] = archive.read('SHA256SUMS.txt').decode()
                existing = dict(self.existing, assets=[{'name': previous_zip.name}])
                with patch.object(release, 'remote_release', return_value=existing), \
                     patch.object(release, 'run', side_effect=github):
                    release.upload(self.directory, self.version, self.commit, 'owner/repo', current_kind)
                self.assertEqual(captured[previous_kind]['commit'], 'b' * 40)
                self.assertEqual(captured[current_kind]['commit'], self.commit)
                self.assertIn(previous_name, captured['sums'])
                self.assertIn(release.packages.package_filename(self.version, current_kind), captured['sums'])

    def test_corrupt_existing_auxiliary_archive_blocks_upload(self):
        archive_name = release.packages.auxiliary_filename(self.version)
        def github(*args, **kwargs):
            if args[2] == 'download':
                (Path(args[args.index('--dir') + 1]) / archive_name).write_bytes(b'not a zip')
        with patch.object(release, 'remote_release', return_value=dict(self.existing, assets=[{'name': archive_name}])), \
             patch.object(release, 'run', side_effect=github) as run:
            with self.assertRaisesRegex(ValueError, 'Invalid auxiliary archive'):
                self.upload()
        self.assertEqual([call.args[2] for call in run.call_args_list], ['download'])

    def test_relative_directory_uploads_each_installer_and_auxiliary_only_once(self):
        import os
        current = Path.cwd()
        try:
            os.chdir(self.directory.parent)
            with patch.object(release, 'remote_release', return_value=None), \
                 patch.object(release, 'api', return_value={'default_branch': 'main'}), \
                 patch.object(release, 'run') as run:
                release.upload(Path(self.directory.name), self.version, self.commit, 'owner/repo', 'exe')
            command = run.call_args_list[1].args
            archives = [arg for arg in command if str(arg).endswith('-auxiliares.zip')]
            self.assertEqual(len(archives), 1)
        finally:
            os.chdir(current)

    def test_ci_artifacts_can_publish_without_local_build_receipt(self):
        with patch.object(sys, 'argv', ['release.py', self.version, '--from-artifacts', '--formats', 'exe']), \
             patch.object(release, 'ROOT', self.directory), \
             patch.object(release.platform, 'system', return_value='Linux'), \
             patch.object(release.platform, 'machine', return_value='x86_64'), \
             patch.object(release, 'output', return_value=self.commit), \
             patch.object(release, 'run'), patch.object(release, 'remote_release', return_value=None), \
             patch.object(release, 'receipt') as receipt, \
             patch.object(release, 'build') as build, patch.object(release, 'upload') as upload:
            release.main()
        receipt.assert_not_called()
        build.assert_not_called()
        upload.assert_called_once_with(self.directory / 'dist', self.version, self.commit,
                                       'ViniciusJesus-art/CircuitAliveEDA', 'exe')

    def test_retry_after_stale_public_alias_does_not_require_cleanup(self):
        (self.directory / 'CircuitAliveEDA-windows-x86_64-setup.exe').write_bytes(b'old build')
        self.test_first_machine_creates_draft_uploads_and_publishes()


if __name__ == '__main__':
    unittest.main()
