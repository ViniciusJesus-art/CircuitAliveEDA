#!/usr/bin/env python3
"""Audit .casim automatic associations against an installed symbol library.

Writes a per-symbol CSV backlog and a JSON summary. Associations come exclusively
from the .casim files; this report is not consulted by the simulator. Physical
pin compatibility is checked; an association is not electrical certification.
"""
import argparse
from collections import Counter
import csv
import json
from pathlib import Path

from symbol_library import library_symbols, symbol_pins

ROOT = Path(__file__).resolve().parents[2]
EXCLUDED_LIBRARIES = {'Auxiliary_Items', 'Graphic', 'Mechanical', 'Jumper',
                      'Simulation_SPICE', 'power'}
EXCLUDED_DEVICE_SYMBOLS = {'FrequencyCounter', 'Galvanometer', 'Ohmmeter',
                           'Oscilloscope', 'Thermal_Jumper'}


def electronic(library, name):
    if library.startswith('Connector') or library in EXCLUDED_LIBRARIES:
        return False
    return not (library == 'Device' and
                (name.startswith(('Ammeter', 'Voltmeter', 'NetTie', 'RFShield')) or
                 name in EXCLUDED_DEVICE_SYMBOLS))


def load_models(paths):
    models = []
    seen = set()
    for path in paths:
        model = json.loads(path.read_text(encoding='utf-8'))
        if model['schema_version'] != 1 or model['backend'] != 'ngspice':
            raise ValueError(f'{path}: unsupported model version/backend')
        if model['id'] in seen:
            continue  # Same first-directory priority as MODEL_REPOSITORY.
        seen.add(model['id'])
        physical = [p['default_pin'] for p in model['pins']]
        if not physical or len(set(physical)) != len(physical):
            raise ValueError(f'{path}: missing/repeated physical pins')
        models.append((path, model, set(physical)))
    return models


def inventory(symbol_directory, model_paths):
    models = load_models(model_paths)
    rows, available, by_library = [], set(), {}
    exact, names, parts = {}, {}, {}
    for entry in models:
        matches = entry[1].get('matches', {})
        for identity in matches.get('library_ids', []):
            exact.setdefault(identity, []).append(entry)
        for name in matches.get('symbol_names', []):
            names.setdefault(name, []).append(entry)
        for name in matches.get('part_names', []):
            parts.setdefault(name.strip().upper(), []).append(entry)
    for path in sorted(symbol_directory.glob('*.kicad_sym')):
        library, counts = path.stem, Counter()
        symbols = library_symbols(path)
        available.update(library + ':' + name for name in symbols)
        for name in sorted(symbols):
            if not electronic(library, name):
                continue
            identity = library + ':' + name
            aliases = parts.get(name.strip().upper(), [])
            candidates = exact.get(identity, []) + aliases
            selected = list({entry[1]['id']: entry for entry in candidates}.values()) if candidates else [
                entry for entry in names.get(name, []) if entry[2] == set(symbol_pins(symbols, name))]
            status, note = 'no_model', ''
            if len(selected) > 1:
                status = 'ambiguous'
            elif selected:
                pin_names = symbol_pins(symbols, name)
                physical = set(pin_names)
                model_pins = selected[0][2]
                if selected[0] in aliases:
                    ports = selected[0][1]['matches']['pin_names']
                    mapped = []
                    for port in selected[0][1]['pins']:
                        labels = {value.strip().upper() for value in ports[port['id']]}
                        matches = [number for number, label in pin_names.items() if label.strip().upper() in labels]
                        if len(matches) != 1:
                            mapped = []; break
                        mapped.append(matches[0])
                    model_pins = set(mapped) if len(mapped) == len(set(mapped)) else set()
                status = 'associated' if physical == model_pins else 'pin_mismatch'
                if status == 'pin_mismatch':
                    note = f'unmodeled={sorted(physical-model_pins)}; missing={sorted(model_pins-physical)}'
            counts[status] += 1
            rows.append({'library_id': identity, 'status': status,
                         'model_ids': ';'.join(e[1]['id'] for e in selected),
                         'model_files': ';'.join(str(e[0]) for e in selected),
                         'note': note})
        if counts:
            by_library[library] = dict(counts)
    counts = Counter(row['status'] for row in rows)
    unavailable = sorted(identity for identity in exact if identity not in available)
    summary = {'schema_version': 1, 'symbols_directory': str(symbol_directory),
               'model_files': len(models), 'electronic_symbol_entries': len(rows),
               'statuses': dict(counts),
               'coverage_percent': round(100 * counts['associated'] / max(1, len(rows)), 3),
               'by_library': by_library, 'unavailable_library_ids': unavailable,
               'notes': ['Counts library entries, including package and drawing variants.',
                         'Associated means unambiguous file association and compatible physical pins, not electrical certification.',
                         'Part-name aliases use the symbol identity and validate every electrical pin name. Value overrides and Sim.Name aliases are not counted.',
                         'User profiles, explicit per-instance assignments and legacy Sim.* device hints are not counted.',
                         'Connectors, ideal simulation sources, power flags, graphic/mechanical items, jumpers and generic measuring instruments are excluded.']}
    return summary, rows


def write_report(prefix, summary, rows):
    prefix.parent.mkdir(parents=True, exist_ok=True)
    prefix.with_suffix('.json').write_text(json.dumps(summary, indent=2, ensure_ascii=False) + '\n',
                                          encoding='utf-8')
    with prefix.with_suffix('.csv').open('w', encoding='utf-8', newline='') as output:
        writer = csv.DictWriter(output, fieldnames=['library_id', 'status', 'model_ids', 'model_files', 'note'])
        writer.writeheader()
        writer.writerows(rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--symbols', type=Path, default=Path('/usr/share/kicad/symbols'))
    parser.add_argument('--models', type=Path, nargs='+', default=[ROOT / 'simulation/models'],
                        help='Model directories, in precedence order')
    parser.add_argument('--output', type=Path, default=ROOT / '.circuitalive/reports/model-catalog')
    parser.add_argument('--fail-on-errors', action='store_true', help='Fail on ambiguous/pin-incompatible associations')
    args = parser.parse_args()
    if not args.symbols.is_dir():
        parser.error('Symbol directory does not exist; provide --symbols.')
    paths = [file for directory in args.models for file in sorted(directory.glob('*.casim'))]
    if not paths:
        parser.error('No .casim models found.')
    summary, rows = inventory(args.symbols, paths)
    write_report(args.output, summary, rows)
    print(f'{summary["model_files"]} models; {summary["electronic_symbol_entries"]} electronic symbol entries.')
    print('; '.join(f'{status}={count}' for status, count in sorted(summary['statuses'].items())))
    print(f'Reports: {args.output.with_suffix(".json")} and {args.output.with_suffix(".csv")}')
    if args.fail_on_errors and any(summary['statuses'].get(key, 0) for key in ('ambiguous', 'pin_mismatch')):
        raise SystemExit(1)


if __name__ == '__main__':
    main()
