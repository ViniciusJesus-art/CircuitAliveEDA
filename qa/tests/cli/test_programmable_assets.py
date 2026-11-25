"""Validate the complete installed model catalog before release compilation."""
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / 'tools/circuitalive/release'))
from programmable_assets import validate_programmable_assets


class ProgrammableAssetsTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='ca runtime assets with spaces ')
        self.addCleanup(temporary.cleanup)
        self.stage = Path(temporary.name) / 'installation'
        self.assets = self.stage / 'share/circuitalive'
        for folder in ('models', 'runtime'):
            shutil.copytree(ROOT / 'simulation' / folder, self.assets / folder)
        for name in ('quickjs-ng', 'avr8js'):
            target = self.assets / 'licenses' / name
            target.mkdir(parents=True)
            shutil.copy2(ROOT / 'thirdparty' / name / 'LICENSE', target)

    def model(self, name):
        return self.assets / 'models' / (name + '.casim')

    def test_complete_shipped_catalog_accepts_native_peripherals_without_modules(self):
        runtimes = []
        for file in self.assets.glob('models/*.casim'):
            model = json.loads(file.read_text(encoding='utf-8'))
            if 'runtime' in model:
                runtimes.append(model['runtime'])
        self.assertTrue(any(r['engine'] == 'javascript' for r in runtimes))
        self.assertTrue(any(r['engine'] == 'native' and 'modules' not in r for r in runtimes))
        self.assertEqual(validate_programmable_assets(self.stage), len(runtimes))

    def test_native_empty_module_list_is_accepted(self):
        file = self.model('ir_receiver_tsop')
        model = json.loads(file.read_text(encoding='utf-8'))
        model['runtime']['modules'] = []
        file.write_text(json.dumps(model), encoding='utf-8')
        validate_programmable_assets(self.stage)

    def test_javascript_modules_remain_required_and_typed(self):
        file = self.model('mcu_atmega328p_p')
        model = json.loads(file.read_text(encoding='utf-8'))
        for modules in (None, [], '../runtime/avr8js.js', [None], ['']):
            with self.subTest(modules=modules):
                model['runtime']['modules'] = modules
                file.write_text(json.dumps(model), encoding='utf-8')
                with self.assertRaisesRegex(ValueError, 'module.*mcu_atmega328p_p'):
                    validate_programmable_assets(self.stage)
        del model['runtime']['modules']
        file.write_text(json.dumps(model), encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'modules.*mcu_atmega328p_p'):
            validate_programmable_assets(self.stage)

    def test_native_runtime_cannot_declare_script_modules(self):
        file = self.model('ir_receiver_tsop')
        model = json.loads(file.read_text(encoding='utf-8'))
        model['runtime']['modules'] = ['../runtime/avr8js.js']
        file.write_text(json.dumps(model), encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'modules.*ir_receiver_tsop'):
            validate_programmable_assets(self.stage)

    def test_unknown_engine_is_rejected(self):
        file = self.model('ir_receiver_tsop')
        model = json.loads(file.read_text(encoding='utf-8'))
        model['runtime']['engine'] = 'unsupported'
        file.write_text(json.dumps(model), encoding='utf-8')
        with self.assertRaisesRegex(ValueError, 'Unknown runtime engine.*ir_receiver_tsop'):
            validate_programmable_assets(self.stage)

    def test_missing_or_empty_bundled_script_is_rejected(self):
        for name in ('avr8js.js', 'pic16.js', 'mcu-serial.js', 'simulation-devices.js'):
            file = self.assets / 'runtime' / name
            content = file.read_bytes()
            for missing in (True, False):
                with self.subTest(name=name, missing=missing):
                    if missing:
                        file.unlink()
                    else:
                        file.write_bytes(b'')
                    with self.assertRaisesRegex(FileNotFoundError, 'Missing bundled runtime'):
                        validate_programmable_assets(self.stage)
                    file.write_bytes(content)

    def test_module_outside_installation_is_rejected_even_when_it_exists(self):
        outside = self.stage.parent / 'outside.js'
        outside.write_text('outside bundle', encoding='utf-8')
        file = self.model('mcu_atmega328p_p')
        model = json.loads(file.read_text(encoding='utf-8'))
        model['runtime']['modules'] = [str(outside)]
        file.write_text(json.dumps(model), encoding='utf-8')
        with self.assertRaisesRegex(FileNotFoundError, 'Missing bundled runtime'):
            validate_programmable_assets(self.stage)

    def test_required_model_and_interpreter_licenses_remain_required(self):
        for file in (self.model('mcu_pic16f84a_p'),
                     self.assets / 'licenses/quickjs-ng/LICENSE',
                     self.assets / 'licenses/avr8js/LICENSE'):
            with self.subTest(file=file):
                content = file.read_bytes()
                file.unlink()
                with self.assertRaises(FileNotFoundError):
                    validate_programmable_assets(self.stage)
                file.write_bytes(content)


if __name__ == '__main__':
    unittest.main()
