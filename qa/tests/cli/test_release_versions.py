"""Verify the independent KiCad/CircuitAlive versions across package formats."""
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools/circuitalive/release'))
from release_version import parse_version


VALID = ('10.0.7-alpha.1', '10.0.7-alpha.4.1', '10.0.7-alpha.4.1.2.3',
         '10.0.7-4.1', '10.0.7-1.2.3', '10.0.7-0', '10.0.7-65535.0.0.65535')
INVALID = ('10.0.7', 'v10.0.7-alpha.4.1', '10.0.7-alpha.', '10.0.7-1..2',
           '10.0.7-1.2.3.4.5', '10.0.7-beta.1', '../10.0.7-alpha.1',
           '10.0.7-alpha.4.1\n', '10.0.7-65536', '10.0.7-alpha.1.65536')


class ReleaseVersionTests(unittest.TestCase):
    def test_prerelease_and_final_versions_preserve_both_parts(self):
        alpha = parse_version('10.0.7-alpha.4.1')
        final = parse_version('10.0.7-4.1')
        self.assertEqual((alpha.base, alpha.circuitalive, alpha.alpha), ('10.0.7', '4.1', True))
        self.assertEqual((final.base, final.circuitalive, final.alpha), ('10.0.7', '4.1', False))
        self.assertEqual(alpha.windows, '4.1.0.0')
        self.assertEqual(final.windows, '4.1.0.0')
        self.assertEqual(alpha.debian, '10.0.7~alpha.4.1-1')
        self.assertEqual(final.debian, '10.0.7+4.1-1')
        self.assertEqual(alpha.rpm_release, '0.alpha.4.1.fc44')
        self.assertEqual(final.rpm_release, '1.4.1.fc44')

    def test_invalid_versions_fail_before_packaging(self):
        for version in INVALID:
            with self.subTest(version=version), self.assertRaises(ValueError):
                parse_version(version)

    @unittest.skipUnless(shutil.which('cmake'), 'CMake is required')
    def test_cmake_headers_preserve_the_full_version_and_numeric_resource(self):
        with tempfile.TemporaryDirectory() as directory:
            header = Path(directory) / 'version.h'
            for version in VALID + INVALID:
                with self.subTest(version=version):
                    result = subprocess.run([
                        'cmake', f'-DKICAD_CMAKE_MODULE_PATH={ROOT / "cmake"}',
                        f'-DSRC_PATH={ROOT}', f'-DCIRCUITALIVE_RELEASE_VERSION={version}',
                        '-DKICAD_VERSION_EXTRA=CircuitAliveEDA', f'-DOUTPUT_FILE={header}',
                        f'-DTEXT_OUTPUT_FILE={directory}/version.txt',
                        '-P', str(ROOT / 'cmake/BuildSteps/WriteVersionHeader.cmake')],
                        capture_output=True, text=True)
                    if version in INVALID:
                        self.assertNotEqual(result.returncode, 0, result.stdout)
                        continue
                    self.assertEqual(result.returncode, 0, result.stderr)
                    definitions = dict(re.findall(r'^#define\s+(\w+)\s+(.+)$', header.read_text(), re.MULTILINE))
                    self.assertEqual(definitions['KICAD_VERSION'], f'"{version}"')
                    self.assertEqual(definitions['KICAD_VERSION_FULL'], f'"{version}-CircuitAliveEDA"')
                    numeric = parse_version(version).windows.replace('.', ', ')
                    self.assertEqual(definitions['KICAD_WIN32_RC_FILEVER'], numeric)
                    self.assertEqual(definitions['KICAD_WIN32_RC_PRODVER'], numeric)
                    self.assertIn(version, definitions['KICAD_WIN32_RC_PRODVER_STR'])

    @unittest.skipUnless(shutil.which('bash'), 'Bash is required')
    def test_linux_build_guards_accept_the_same_version_structure(self):
        for script in ('build-linux.sh', 'build-deb.sh'):
            text = (ROOT / 'tools/circuitalive/release' / script).read_text()
            line = next(line for line in text.splitlines() if '$RELEASE_VERSION =~' in line)
            pattern = line.split('$RELEASE_VERSION =~ ', 1)[1].split(' ||', 1)[0].removesuffix(' ]]; then')
            for version in VALID + INVALID:
                if '65536' in version:
                    continue  # The build's CMake validation checks numeric resource bounds.
                with self.subTest(script=script, version=version):
                    result = subprocess.run(['bash', '-c', '[[ $1 =~ $2 ]]', '--', version, pattern])
                    self.assertEqual(result.returncode == 0, version in VALID)

    @unittest.skipUnless(shutil.which('rpm'), 'RPM is required')
    def test_rpm_orders_updates_and_final_releases_after_alpha(self):
        versions = ['10.0.7-alpha.4', '10.0.7-alpha.4.1', '10.0.7-alpha.4.2',
                    '10.0.7-1', '10.0.7-1.1', '10.0.7-4.1']
        for before, after in zip(versions, versions[1:]):
            with self.subTest(before=before, after=after):
                a, b = parse_version(before).rpm_release, parse_version(after).rpm_release
                expression = '%{lua:print(rpm.vercmp("' + a + '","' + b + '"))}'
                self.assertLess(int(subprocess.check_output(['rpm', '--eval', expression], text=True)), 0)

    @unittest.skipUnless(shutil.which('dpkg'), 'dpkg is required')
    def test_debian_orders_updates_and_final_releases_after_alpha(self):
        versions = ['10.0.7-alpha.4', '10.0.7-alpha.4.1', '10.0.7-alpha.4.2',
                    '10.0.7-1', '10.0.7-1.1', '10.0.7-4.1']
        for before, after in zip(versions, versions[1:]):
            with self.subTest(before=before, after=after):
                subprocess.run(['dpkg', '--compare-versions', parse_version(before).debian,
                                'lt', parse_version(after).debian], check=True)


if __name__ == '__main__':
    unittest.main()
