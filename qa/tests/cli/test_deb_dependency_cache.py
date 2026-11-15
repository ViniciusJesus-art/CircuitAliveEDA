"""A native dependency cache must not bypass ABI or file-integrity checks."""
import importlib.util
from pathlib import Path
import json
import shutil
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[3]
SPEC = importlib.util.spec_from_file_location(
    'deb_dependency_cache', ROOT / 'tools/circuitalive/release/deb-dependency-cache.py')
cache = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(cache)
sys.path.insert(0, str(ROOT / 'tools/circuitalive/release'))
PACKAGE_SPEC = importlib.util.spec_from_file_location(
    'package_deb', ROOT / 'tools/circuitalive/release/package-deb.py')
package = importlib.util.module_from_spec(PACKAGE_SPEC)
PACKAGE_SPEC.loader.exec_module(package)


class NativeDependencyCacheTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.directory = Path(temporary.name)
        for name in cache.REQUIRED:
            path = self.directory / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b'installed dependency fixture')

    def test_only_a_completed_matching_installation_can_be_reused(self):
        self.assertFalse(cache.verify(self.directory, 'compiler-profile-a'))
        cache.record(self.directory, 'compiler-profile-a')
        self.assertTrue(cache.verify(self.directory, 'compiler-profile-a'))
        self.assertFalse(cache.verify(self.directory, 'compiler-profile-b'))

    def test_missing_corrupt_and_added_files_invalidate_the_installation(self):
        cache.record(self.directory, 'profile')
        header = self.directory / cache.REQUIRED[2]
        original = header.read_bytes()
        header.write_bytes(b'changed development header')
        self.assertFalse(cache.verify(self.directory, 'profile'))
        header.write_bytes(original)
        header.unlink()
        self.assertFalse(cache.verify(self.directory, 'profile'))
        header.write_bytes(original)
        (self.directory / 'extra.so').write_bytes(b'untracked stale library')
        self.assertFalse(cache.verify(self.directory, 'profile'))

    def test_partial_installations_cannot_receive_a_completion_manifest(self):
        (self.directory / cache.REQUIRED[0]).unlink()
        with self.assertRaises(ValueError):
            cache.record(self.directory, 'profile')
        self.assertFalse((self.directory / cache.MANIFEST).exists())

    def test_restored_prefix_supplies_packaging_licenses_without_source_trees(self):
        cache.record(self.directory, 'profile')
        with tempfile.TemporaryDirectory() as temporary:
            restored = Path(temporary) / 'restored-prefix'
            shutil.copytree(self.directory, restored)
            shutil.rmtree(self.directory)
            self.assertTrue(cache.verify(restored, 'profile'))
            app = Path(temporary) / 'stage/opt/circuitaliveeda'
            package.copy_native_dependency_licenses(restored, app)
            for name in cache.LICENSES:
                self.assertEqual((app / name).read_bytes(), (restored / name).read_bytes())

    def test_old_cache_without_each_license_is_rejected_before_application_build(self):
        for name in cache.LICENSES:
            with self.subTest(license=name):
                path = self.directory / name
                original = path.read_bytes()
                path.unlink()
                # Reproduce the old cache: its manifest tracks all installed
                # binaries/headers correctly, but contains no source licenses.
                (self.directory / cache.MANIFEST).write_text(json.dumps(
                    {'profile': 'profile', 'files': cache.inventory(self.directory)}))
                self.assertFalse(cache.verify(self.directory, 'profile'))
                with self.assertRaises(ValueError):
                    cache.record(self.directory, 'profile')
                app = self.directory / 'not-created'
                with self.assertRaisesRegex(FileNotFoundError, 'Missing cached native dependency license'):
                    package.copy_native_dependency_licenses(self.directory, app)
                self.assertFalse(app.exists())
                path.write_bytes(original)

    def test_malformed_completion_manifests_are_cache_misses(self):
        for contents in ('{broken', 'null', '[]', '{}'):
            (self.directory / cache.MANIFEST).write_text(contents)
            self.assertFalse(cache.verify(self.directory, 'profile'))

    def test_broken_or_external_symlinks_are_rejected(self):
        link = self.directory / 'lib/dependency-link.so'
        try:
            link.symlink_to('libnng.so')
        except OSError:
            self.skipTest('Symlinks unavailable')
        cache.record(self.directory, 'profile')
        self.assertTrue(cache.verify(self.directory, 'profile'))
        link.unlink()
        link.symlink_to('missing.so')
        self.assertFalse(cache.verify(self.directory, 'profile'))
        link.unlink()
        link.symlink_to(Path(__file__).resolve())
        self.assertFalse(cache.verify(self.directory, 'profile'))

    def test_compiler_packages_and_build_recipe_all_invalidate_the_profile(self):
        def tool_output(command, **kwargs):
            return (' '.join(command) + '=version-a').encode()
        original = cache.profile(run=tool_output)
        self.assertEqual(original, cache.profile(run=tool_output))
        for tool in ('gcc', 'g++', 'cmake', 'dpkg-query'):
            def updated(command, **kwargs):
                return tool_output(command) + (b'-updated' if command[0] == tool else b'')
            self.assertNotEqual(original, cache.profile(run=updated), tool)
        recipe_root = self.directory / 'checkout'
        for name in ('linux-toolchain.json', 'build-deb.sh', 'deb-dependency-cache.py'):
            dest = recipe_root / 'tools/circuitalive/release' / name
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes((ROOT / 'tools/circuitalive/release' / name).read_bytes())
        copied = cache.profile(recipe_root, run=tool_output)
        self.assertNotEqual(original, copied, 'The installed prefix changed')
        recipe = recipe_root / 'tools/circuitalive/release/build-deb.sh'
        recipe.write_text(recipe.read_text() + '\n# changed configure flags\n')
        self.assertNotEqual(copied, cache.profile(recipe_root, run=tool_output))
        with patch.dict('os.environ', {'CFLAGS': '-O0 -DCHANGED_ABI=1'}):
            self.assertNotEqual(original, cache.profile(run=tool_output))


if __name__ == '__main__':
    unittest.main()
