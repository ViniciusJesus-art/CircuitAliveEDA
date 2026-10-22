"""Installed libraries must be usable without any manual path configuration."""
import importlib.util
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools/circuitalive/release'))
from component_libraries import validate_component_libraries, validate_installed_assets, verify_first_start


def create_library_fixture(data):
    for folder in ('symbols', 'footprints/Resistor_SMD.pretty', '3dmodels/Resistor_SMD.3dshapes',
                   'template/example'):
        (data / folder).mkdir(parents=True, exist_ok=True)
    (data / 'symbols/Device.kicad_sym').write_text('(kicad_symbol_lib (version 20250114))')
    (data / 'footprints/Resistor_SMD.pretty/R.kicad_mod').write_text('(footprint "R")')
    (data / '3dmodels/Resistor_SMD.3dshapes/R.step').write_text('ISO-10303-21;')
    (data / 'template/example/example.kicad_pro').write_text('{}')
    for filename, kind, variable, library in (
            ('sym-lib-table', 'sym', 'SYMBOL', 'Device.kicad_sym'),
            ('fp-lib-table', 'fp', 'FOOTPRINT', 'Resistor_SMD.pretty')):
        (data / 'template' / filename).write_text(
            f'({kind}_lib_table (version 7) (lib (name "Fixture") (type "KiCad") '
            f'(uri "${{KICAD10_{variable}_DIR}}/{library}") (options "") (descr "")))')


class ComponentLibrariesTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='ca libraries with spaces ')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.data = self.root / 'share/kicad'
        create_library_fixture(self.data)

    def test_installed_layout_is_complete(self):
        validate_component_libraries(self.data)

    def test_raw_checkout_tables_cannot_replace_installed_tables(self):
        table = self.data / 'template/sym-lib-table'
        table.rename(self.data / 'symbols/sym-lib-table')
        with self.assertRaisesRegex(ValueError, 'Missing installed library table'):
            validate_component_libraries(self.data)

    def test_missing_symbols_footprints_3d_and_templates_are_rejected(self):
        for name in ('symbols/Device.kicad_sym', 'footprints/Resistor_SMD.pretty/R.kicad_mod',
                     '3dmodels/Resistor_SMD.3dshapes/R.step', 'template/example/example.kicad_pro'):
            with self.subTest(name=name):
                path = self.data / name
                original = path.read_bytes()
                path.unlink()
                with self.assertRaises(ValueError):
                    validate_component_libraries(self.data)
                path.write_bytes(original)

    def test_symbols_missing_from_table_are_rejected(self):
        shutil.copy2(self.data / 'symbols/Device.kicad_sym', self.data / 'symbols/Unregistered.kicad_sym')
        with self.assertRaisesRegex(ValueError, 'disagree'):
            validate_component_libraries(self.data)

    def test_simulation_models_and_blocks_are_required(self):
        for folder in ('models', 'blocks'):
            with self.assertRaisesRegex(ValueError, 'Missing bundled simulation ' + folder):
                validate_installed_assets(self.root)
            (self.root / 'share/circuitalive' / folder).mkdir(parents=True)
            (self.root / 'share/circuitalive' / folder / 'fixture.casim').write_text('{}')
        validate_installed_assets(self.root)

    def test_first_start_requires_relocatable_global_tables(self):
        config = self.root / 'profile/10.0'
        config.mkdir(parents=True)
        with self.assertRaisesRegex(ValueError, 'did not register'):
            verify_first_start(config.parent)
        for filename in ('sym-lib-table', 'fp-lib-table'):
            (config / filename).write_text('${KICAD10_TEMPLATE_DIR}/' + filename)
        verify_first_start(config.parent)
        (config / 'fp-lib-table').write_text('/build-machine/template/fp-lib-table')
        with self.assertRaisesRegex(ValueError, 'did not register'):
            verify_first_start(config.parent)

    @unittest.skipUnless(shutil.which('cmake') and shutil.which('ninja'), 'CMake/Ninja integration')
    def test_source_preparation_runs_build_and_upstream_install_rules(self):
        spec = importlib.util.spec_from_file_location('prepare_components',
                ROOT / 'tools/circuitalive/release/prepare-source-libraries.py')
        prepare = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(prepare)
        source = self.root / 'component source'
        source.mkdir()
        # Table and packed library only exist after the build. Copying this source tree fails.
        (source / 'CMakeLists.txt').write_text('''cmake_minimum_required(VERSION 3.16)
project(component_fixture NONE)
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/Device.kicad_sym"
 COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_CURRENT_SOURCE_DIR}/unpacked.symbol"
 "${CMAKE_CURRENT_BINARY_DIR}/Device.kicad_sym")
add_custom_target(pack_symbols ALL DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/Device.kicad_sym")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/Device.kicad_sym" DESTINATION share/kicad/symbols)
install(FILES sym-lib-table DESTINATION share/kicad/template)
''')
        (source / 'unpacked.symbol').write_text('(kicad_symbol_lib)')
        shutil.copy2(self.data / 'template/sym-lib-table', source / 'sym-lib-table')
        prefix = self.root / 'installed components'
        prepare.install_repository(source, self.root / 'build with spaces', prefix)
        self.assertEqual((prefix / 'share/kicad/symbols/Device.kicad_sym').read_text(), '(kicad_symbol_lib)')
        self.assertTrue((prefix / 'share/kicad/template/sym-lib-table').is_file())


if __name__ == '__main__':
    unittest.main()
