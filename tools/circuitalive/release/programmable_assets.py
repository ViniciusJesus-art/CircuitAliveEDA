"""Validate the programmable models shipped inside an installed application."""
import json
from pathlib import Path


def validate_programmable_assets(stage):
    stage = Path(stage).resolve()
    models = stage / 'share/circuitalive/models'
    required = ('mcu_arduino_uno_r3', 'mcu_atmega48p_p', 'mcu_atmega88p_p',
                'mcu_atmega168p_p', 'mcu_atmega328p_p', 'mcu_pic16f84a_p')
    for name in required:
        if not (models / (name + '.casim')).is_file():
            raise FileNotFoundError(f'Missing installed programmable model: {name}')
    count = 0
    for file in models.glob('*.casim'):
        model = json.loads(file.read_text(encoding='utf-8'))
        if 'runtime' not in model:
            continue
        count += 1
        runtime = model['runtime']
        engine = runtime.get('engine')
        if engine not in ('javascript', 'native'):
            raise ValueError(f'Unknown runtime engine for {file.name}: {engine}')
        # Native peripherals live in the worker and do not ship script modules.
        # JavaScript models still require a nonempty list of bundled modules.
        modules = runtime.get('modules', [])
        if (not isinstance(modules, list)
                or (engine == 'javascript' and not modules)
                or (engine == 'native' and modules)):
            raise ValueError(f'Invalid runtime modules for {file.name}: {engine}')
        for module in modules:
            if not isinstance(module, str) or not module:
                raise ValueError(f'Invalid runtime module path for {file.name}: {module}')
            path = (file.parent / module).resolve()
            if not path.is_relative_to(stage) or not path.is_file() or not path.stat().st_size:
                raise FileNotFoundError(f'Missing bundled runtime for {file.name}: {module}')
    for name in ('quickjs-ng', 'avr8js'):
        if not (stage / 'share/circuitalive/licenses' / name / 'LICENSE').is_file():
            raise FileNotFoundError(f'Missing bundled interpreter license: {name}')
    return count
