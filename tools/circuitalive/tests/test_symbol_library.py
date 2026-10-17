"""Regression checks for physical pins used by the model-library audit."""
import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from symbol_library import library_symbols, symbol_pins

spec = importlib.util.spec_from_file_location('model_catalog', Path(__file__).resolve().parents[1] / 'model-catalog.py')
catalog = importlib.util.module_from_spec(spec)
spec.loader.exec_module(catalog)


class SymbolPins(unittest.TestCase):
    def test_unnamed_pins_alternate_drawings_and_inheritance(self):
        source = '''(kicad_symbol_lib
\t(symbol "Gate"
\t\t(symbol "Gate_1_1"
\t\t\t(pin input line (name "") (number "1"))
\t\t\t(pin output inverted (name "") (number "2")))
\t\t(symbol "Gate_1_2"
\t\t\t(pin output line (name "alternate") (number "2")))
\t\t(symbol "Gate_2_0"
\t\t\t(pin power_in line (name "GND") (number "3"))))
\t(symbol "Alias" (extends "Gate"))
)'''
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'Test.kicad_sym'
            path.write_text(source)
            symbols = library_symbols(path)
            self.assertEqual(set(symbols), {'Gate', 'Alias'})
            self.assertEqual(symbol_pins(symbols, 'Alias'), {'1': '', '2': '', '3': 'GND'})

    def test_cyclic_inheritance_is_rejected(self):
        with self.assertRaises(ValueError):
            symbol_pins({'A': '(extends "B")', 'B': '(extends "A")'}, 'A')

    def test_report_does_not_count_incompatible_or_ambiguous_models_as_coverage(self):
        import json
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'Test.kicad_sym').write_text('''(kicad_symbol_lib
\t(symbol "Part"
\t\t(symbol "Part_1_1"
\t\t\t(pin passive line (name "") (number "1")))))''')
            def model(name, pin):
                path = root / (name + '.casim')
                path.write_text(json.dumps({'schema_version': 1, 'backend': 'ngspice',
                    'id': name, 'pins': [{'default_pin': pin}],
                    'matches': {'library_ids': ['Test:Part']}}))
                return path
            bad = model('bad', '2')
            summary, _ = catalog.inventory(root, [bad])
            self.assertEqual(summary['statuses'], {'pin_mismatch': 1})
            self.assertEqual(summary['coverage_percent'], 0)
            good = model('good', '1')
            summary, _ = catalog.inventory(root, [good])
            self.assertEqual(summary['statuses'], {'associated': 1})
            summary, _ = catalog.inventory(root, [good, bad])
            self.assertEqual(summary['statuses'], {'ambiguous': 1})

    def test_named_alias_uses_electrical_pin_names_instead_of_canonical_pin_order(self):
        import json
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            body = '''(kicad_symbol_lib
\t(symbol "MMBT2222A"
\t\t(symbol "MMBT2222A_1_1"
\t\t\t(pin passive line (name "B") (number "1"))
\t\t\t(pin passive line (name "E") (number "2"))
\t\t\t(pin passive line (name "C") (number "3")))))'''
            (root / 'Test.kicad_sym').write_text(body)
            model = root / 'npn.casim'
            model.write_text(json.dumps({'schema_version': 1, 'backend': 'ngspice', 'id': 'npn',
                'pins': [{'id': 'c', 'default_pin': '1'}, {'id': 'b', 'default_pin': '2'}, {'id': 'e', 'default_pin': '3'}],
                'matches': {'part_names': ['MMBT2222A'], 'pin_names': {'c': ['C'], 'b': ['B'], 'e': ['E']}}}))
            summary, _ = catalog.inventory(root, [model])
            self.assertEqual(summary['statuses'], {'associated': 1})
            (root / 'Test.kicad_sym').write_text(body.replace('(name "E")', '(name "C")'))
            summary, _ = catalog.inventory(root, [model])
            self.assertEqual(summary['statuses'], {'pin_mismatch': 1})


if __name__ == '__main__':
    unittest.main()
