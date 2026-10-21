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
        for module in model['runtime']['modules']:
            path = (file.parent / module).resolve()
            if not path.is_relative_to(stage) or not path.is_file() or not path.stat().st_size:
                raise FileNotFoundError(f'Missing bundled runtime for {file.name}: {module}')
    for name in ('quickjs-ng', 'avr8js'):
        if not (stage / 'share/circuitalive/licenses' / name / 'LICENSE').is_file():
            raise FileNotFoundError(f'Missing bundled interpreter license: {name}')
    return count
