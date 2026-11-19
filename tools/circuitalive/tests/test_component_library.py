"""Project assets must remain usable after the original project is unavailable."""
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from component_library import import_components, install_collection, parse, refresh_sources


class ComponentPromotion(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='ca components with spaces ')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.project = self.root / 'project'
        self.library = self.project / 'Parts.pretty'
        self.library.mkdir(parents=True)
        self.collection = self.root / 'resources/component-libraries'
        (self.library / 'case STEP.step').write_text('ISO-10303-21; fixture')
        (self.library / 'Part.kicad_mod').write_text(
            '(footprint "Part" (descr "Quoted \\"text\\" (test)") '
            '(model "case STEP.step" (offset (xyz 1 2 3))))')
        (self.project / 'Parts.kicad_sym').write_text(
            '(kicad_symbol_lib (version 20211014) (symbol "Part" '
            '(property "Footprint" "Part") (symbol "Part_1_1")) '
            '(symbol "Alias" (extends "Part")))')
        (self.project / 'sym-lib-table').write_text(
            '(sym_lib_table (lib (name "Parts") (type "KiCad") '
            '(uri "${KIPRJMOD}/Parts.kicad_sym")))')
        (self.project / 'fp-lib-table').write_text(
            '(fp_lib_table (lib (name "Parts") (type "KiCad") '
            '(uri "${KIPRJMOD}/Parts.pretty")))')

    def test_project_survives_relocation_and_preserves_existing_global_rows(self):
        import_components(self.project, self.collection)
        shutil.rmtree(self.project)
        profile = self.root / 'profile'
        profile.mkdir()
        personal = '(lib (name "Personal") (type "KiCad") (uri "/personal.kicad_sym"))'
        (profile / 'sym-lib-table').write_text('(sym_lib_table ' + personal + ')')
        install_collection(self.root / 'relocated install', self.collection, profile)
        symbol = parse((self.collection / 'symbols/Parts.kicad_sym').read_text())
        self.assertEqual(symbol.fields('symbol')[0].fields('property')[0].children[2].value, 'Parts:Part')
        self.assertEqual(symbol.fields('symbol')[1].field('extends'), 'Part')
        footprint = parse((self.root / 'relocated install/footprints/Parts.pretty/Part.kicad_mod').read_text())
        uri = footprint.field('model')
        self.assertEqual(uri, '${KICAD10_3DMODEL_DIR}/Parts.3dshapes/case STEP.step')
        self.assertTrue((self.root / 'relocated install/3dmodels/Parts.3dshapes/case STEP.step').is_file())
        self.assertIn(personal, (profile / 'sym-lib-table').read_text())
        self.assertNotIn('KIPRJMOD', (profile / 'sym-lib-table').read_text())
        before = (profile / 'sym-lib-table').read_bytes()
        install_collection(self.root / 'relocated install', self.collection, profile)
        self.assertEqual((profile / 'sym-lib-table').read_bytes(), before)

    def test_missing_model_cannot_partially_change_the_collection(self):
        import_components(self.project, self.collection)
        before = {p.relative_to(self.collection): p.read_bytes() for p in self.collection.rglob('*') if p.is_file()}
        (self.library / 'case STEP.step').unlink()
        with self.assertRaisesRegex(ValueError, 'Missing 3D model'):
            import_components(self.project, self.collection)
        self.assertEqual(before, {p.relative_to(self.collection): p.read_bytes() for p in self.collection.rglob('*') if p.is_file()})

    def test_explicit_model_rename_is_portable(self):
        (self.library / 'case STEP.step').rename(self.library / 'renamed.step')
        import_components(self.project, self.collection, model_map={'case STEP.step': 'renamed.step'})
        footprint = parse((self.collection / 'footprints/Parts.pretty/Part.kicad_mod').read_text())
        self.assertEqual(footprint.field('model'), '${KICAD10_3DMODEL_DIR}/Parts.3dshapes/renamed.step')

    def test_only_promoted_project_relative_global_rows_are_repaired(self):
        import_components(self.project, self.collection)
        (self.collection / 'aliases.json').write_text(json.dumps({'symbols': {'Old': 'Parts'}}))
        profile = self.root / 'profile'
        profile.mkdir()
        original = ('(sym_lib_table (lib (name "Old") (type "KiCad") '
                    '(uri "${KIPRJMOD}/moved.kicad_sym")) (lib (name "Other") '
                    '(type "KiCad") (uri "${KIPRJMOD}/Other.kicad_sym")))')
        table = profile / 'sym-lib-table'
        table.write_text(original)
        install_collection(self.root / 'installed', self.collection, profile)
        rows = {row.field('name'): row.field('uri') for row in parse(table.read_text()).fields('lib')}
        self.assertEqual(rows['Old'], '${KICAD10_SYMBOL_DIR}/Parts.kicad_sym')
        self.assertEqual(rows['Other'], '${KIPRJMOD}/Other.kicad_sym')
        self.assertEqual(table.with_name(table.name + '.before-component-promotion').read_text(), original)

    def test_watched_additions_are_incorporated_and_disconnected_sources_survive(self):
        registry = self.root / '.circuitalive/component-sources.json'
        registry.parent.mkdir()
        registry.write_text(json.dumps([{'source': str(self.project)}]))
        refresh_sources(self.root)
        (self.library / 'New.kicad_mod').write_text('(footprint "New")')
        refresh_sources(self.root)
        self.assertTrue((self.collection / 'footprints/Parts.pretty/New.kicad_mod').is_file())
        shutil.rmtree(self.project)
        refresh_sources(self.root)
        self.assertTrue((self.collection / 'footprints/Parts.pretty/New.kicad_mod').is_file())

    def test_personal_library_with_same_nickname_is_never_repointed(self):
        import_components(self.project, self.collection)
        profile = self.root / 'profile'
        profile.mkdir()
        table = profile / 'sym-lib-table'
        original = '(sym_lib_table (lib (name "Parts") (type "KiCad") (uri "/personal.kicad_sym")))'
        table.write_text(original)
        with self.assertRaisesRegex(ValueError, 'nickname conflict'):
            install_collection(self.root / 'installed', self.collection, profile)
        self.assertEqual(table.read_text(), original)

    def test_sync_never_discards_edits_made_in_the_installed_library(self):
        import_components(self.project, self.collection)
        data = self.root / 'installed'
        install_collection(data, self.collection)
        installed = data / 'symbols/Parts.kicad_sym'
        changed = installed.read_text().replace('"Alias"', '"New local symbol"')
        installed.write_text(changed)
        with self.assertRaisesRegex(ValueError, 'local changes'):
            install_collection(data, self.collection)
        self.assertEqual(installed.read_text(), changed)


if __name__ == '__main__':
    unittest.main()
