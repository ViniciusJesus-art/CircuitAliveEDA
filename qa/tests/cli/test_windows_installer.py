"""Windows packaging regressions: preserved binaries, clean runtime and large installers."""
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import shutil
import sys
import tempfile
import unittest
from unittest.mock import patch
from test_component_libraries import create_library_fixture

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools/circuitalive/release'))
SPEC = importlib.util.spec_from_file_location('windows_package', ROOT / 'tools/circuitalive/release/package-windows.py')
package = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(package)


class WindowsInstallerTests(unittest.TestCase):
    version = '10.0.7-alpha.1'

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='ca installer paths with spaces ')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.stage = self.root / 'stage/windows-msvc'
        (self.stage / 'bin').mkdir(parents=True)
        (self.stage / 'bin/CircuitAliveEDA.exe').write_bytes(b'MZ desktop fixture')
        assets = self.stage / 'share/circuitalive'
        (assets / 'models').mkdir(parents=True)
        for name in ('mcu_arduino_uno_r3', 'mcu_atmega48p_p', 'mcu_atmega88p_p',
                     'mcu_atmega168p_p', 'mcu_atmega328p_p', 'mcu_pic16f84a_p'):
            shutil.copy2(ROOT / 'simulation/models' / (name + '.casim'), assets / 'models')
        shutil.copytree(ROOT / 'simulation/runtime', assets / 'runtime')
        shutil.copytree(ROOT / 'simulation/blocks', assets / 'blocks')
        for name in ('quickjs-ng', 'avr8js'):
            target = assets / 'licenses' / name
            target.mkdir(parents=True)
            shutil.copy2(ROOT / 'thirdparty' / name / 'LICENSE', target)
        (self.stage / 'PACKAGE-INFO.json').write_text(json.dumps({
            'version': self.version, 'commit': 'a' * 40}), encoding='utf-8')
        self.libraries = self.root / '.circuitalive/libraries'
        for folder in package.LIBRARIES:
            path = self.libraries / 'share/kicad' / folder
            path.mkdir(parents=True)
            (path / 'data.txt').write_text('library data', encoding='utf-8')
        create_library_fixture(self.libraries / 'share/kicad')
        (self.root / 'LICENSE.GPLv3').write_text('fixture license', encoding='utf-8')
        self.dist = self.root / 'dist'
        self.dist.mkdir()
        package.preserve_runtime(self.stage, self.version)

    def test_restored_runtime_retains_version_source_and_every_binary(self):
        checkpoint = package.validate_runtime(self.stage, self.version)
        self.assertEqual(checkpoint['commit'], 'a' * 40)
        self.assertIn('bin/CircuitAliveEDA.exe', checkpoint['files'])

    def test_corrupted_missing_and_extra_binaries_are_rejected_before_packaging(self):
        exe = self.stage / 'bin/CircuitAliveEDA.exe'
        original = exe.read_bytes()
        exe.write_bytes(b'corrupt')
        with self.assertRaisesRegex(ValueError, 'checksum mismatch'):
            package.validate_runtime(self.stage, self.version)
        exe.unlink()
        with self.assertRaisesRegex(ValueError, 'missing or unexpected'):
            package.validate_runtime(self.stage, self.version)
        exe.write_bytes(original)
        (self.stage / 'bin/unexpected.dll').touch()
        with self.assertRaisesRegex(ValueError, 'missing or unexpected'):
            package.validate_runtime(self.stage, self.version)

    def test_different_version_cannot_relabel_an_existing_runtime(self):
        with self.assertRaisesRegex(ValueError, 'version differs'):
            package.validate_runtime(self.stage, '10.0.7-alpha.2')

    def test_installer_requires_bundled_firmware_engines(self):
        for name in ('avr8js.js', 'pic16.js'):
            with self.subTest(module=name):
                path = self.stage / 'share/circuitalive/runtime' / name
                original = path.read_bytes()
                path.unlink()
                with self.assertRaisesRegex(FileNotFoundError, 'Missing bundled runtime'):
                    package.validate_runtime(self.stage, self.version)
                path.write_bytes(original)

    def test_missing_component_library_fails_before_running_the_compiler(self):
        shutil.rmtree(self.libraries / 'share/kicad/3dmodels')
        with patch.object(package.subprocess, 'run') as run:
            with self.assertRaisesRegex(ValueError, 'Missing component library'):
                package.package_runtime(self.stage, self.version, 'ISCC.exe', self.root)
            run.assert_not_called()

    def test_setup_icon_uses_repository_icon_path(self):
        script = package.installer_script(self.stage, self.libraries, self.version, self.dist, self.root)
        icon = package.inno_quote(self.root / 'resources/bitmaps_png/icons/icon_kicad.ico')
        self.assertIn(f'SetupIconFile="{icon}"', script)

    def test_inno_setup_paths_are_quoted_when_root_or_output_have_spaces(self):
        root = self.root / 'ci runner path with spaces'
        dist = self.root / 'dist path with spaces'
        script = package.installer_script(self.stage, self.libraries, self.version, dist, root)
        self.assertIn(f'LicenseFile="{package.inno_quote(root / "LICENSE.GPLv3")}"', script)
        self.assertIn(f'OutputDir="{package.inno_quote(dist)}"', script)

    def test_failed_compilation_and_failed_installation_do_not_get_a_checksum(self):
        installer = self.dist / f'CircuitAliveEDA-{self.version}-windows-x86_64-setup.exe'
        installer.write_bytes(b'MZ partial installer')
        for compile_error in (True, False):
            with self.subTest(compile_error=compile_error):
                installer.with_suffix('.exe.sha256').write_text('stale checksum')
                with patch.object(package.subprocess, 'run', side_effect=(
                        subprocess.CalledProcessError(1, 'ISCC.exe') if compile_error else None)), \
                        patch.object(package, 'verify_installer', side_effect=RuntimeError('smoke failed')):
                    with self.assertRaises((subprocess.CalledProcessError, RuntimeError)):
                        package.package_runtime(self.stage, self.version, 'ISCC.exe', self.root)
                self.assertFalse(installer.with_suffix('.exe.sha256').exists())
                self.assertTrue((self.dist / 'installer.iss').is_file())
                self.assertTrue((self.dist / 'windows-package-report.json').is_file())

    def test_successful_installation_is_required_before_checksum_is_written(self):
        installer = self.dist / f'CircuitAliveEDA-{self.version}-windows-x86_64-setup.exe'
        installer.write_bytes(b'MZ verified installer')
        with patch.object(package.subprocess, 'run'), patch.object(package, 'verify_installer') as verify:
            package.package_runtime(self.stage, self.version, 'ISCC.exe', self.root)
        verify.assert_called_once_with(installer, self.root)
        self.assertEqual(installer.with_suffix('.exe.sha256').read_text().split()[0], package.digest(installer))

    def test_runtime_checks_use_installed_python_and_no_build_path(self):
        with patch.dict(os.environ, {'SystemRoot': 'C:/Windows', 'PATH': 'D:/vcpkg/bin'}), \
                patch.object(package.subprocess, 'run') as run, \
                patch.object(package, 'validate_installed_assets'), \
                patch.object(package, 'validate_programmable_assets'), \
                patch.object(package, 'verify_first_start') as first_start:
            package.verify_installer(self.dist / 'setup.exe', self.root)
        first_start.assert_called_once()
        install = run.call_args_list[0].args[0]
        self.assertIn('/VERYSILENT', install)
        self.assertIn(f'/DIR={self.root / "installer smoke"}', install)
        for call in run.call_args_list[1:]:
            self.assertNotIn('vcpkg', call.kwargs['env']['PATH'])
            self.assertEqual(call.kwargs['env']['PYTHONHOME'], str(self.root / 'installer smoke/bin'))
        self.assertEqual(run.call_args_list[3].args[0][0], str(self.root / 'installer smoke/bin/python.exe'))

    @unittest.skipUnless(os.name == 'nt' and os.environ.get('INNO_ISCC'), 'Native Inno Setup integration')
    def test_real_installer_extracts_libraries_and_excludes_git_metadata(self):
        for folder in package.LIBRARIES:
            git = self.libraries / 'share/kicad' / folder / '.git'
            git.mkdir()
            (git / 'private-metadata').write_text('must not ship')
        destination = self.root / 'fresh installation with spaces'
        script = self.dist / 'fixture.iss'
        text = package.installer_script(self.stage, self.libraries, self.version, self.dist, self.root)
        text = text.replace('AppId=CircuitAliveEDA\n', 'AppId=CircuitAliveEDA.PackagingFixture\n')
        text = text.split('[Icons]')[0]
        script.write_text(text, encoding='utf-8-sig')
        subprocess.run([os.environ['INNO_ISCC'], '-Qp', str(script)], check=True, timeout=120)
        installer = self.dist / f'CircuitAliveEDA-{self.version}-windows-x86_64-setup.exe'
        subprocess.run([str(installer), '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/SP-',
                        f'/DIR={destination}', f'/LOG={self.dist / "fixture-install.log"}'], check=True, timeout=120)
        try:
            self.assertEqual((destination / 'bin/CircuitAliveEDA.exe').read_bytes(), b'MZ desktop fixture')
            for folder in package.LIBRARIES:
                installed = destination / 'share/kicad' / folder
                self.assertEqual((installed / 'data.txt').read_text(), 'library data')
                self.assertFalse((installed / '.git').exists())
            self.assertFalse((destination / package.CHECKPOINT).exists())
            self.assertEqual(package.validate_programmable_assets(destination), 6)
        finally:
            subprocess.run([str(destination / 'unins000.exe'), '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART'],
                           check=True, timeout=120)


if __name__ == '__main__':
    unittest.main()
