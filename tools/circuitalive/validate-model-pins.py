#!/usr/bin/env python3
"""Check .casim associations against installed KiCad symbol pin numbers and terminal names."""
import argparse
import json
from pathlib import Path
from symbol_library import library_symbols, symbol_pins


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('models', type=Path, nargs='+', help='Model files to check')
    parser.add_argument('--symbols', type=Path, default=Path('/usr/share/kicad/symbols'))
    args = parser.parse_args()
    cache = {}
    checked = 0
    for path in args.models:
        model = json.loads(path.read_text())
        associations = model.get('matches', {}).get('library_ids', [])
        if not associations:
            raise ValueError(f'{path}: no automatic library association to validate')
        for association in associations:
            library, name = association.split(':', 1)
            if library not in cache:
                cache[library] = library_symbols(args.symbols / (library + '.kicad_sym'))
            pins = symbol_pins(cache[library], name)
            mapped = {port['default_pin'] for port in model['pins']}
            if mapped != set(pins):
                raise ValueError(f'{association}: physical pins differ; '
                                 f'unmodeled={sorted(set(pins) - mapped)}, '
                                 f'missing={sorted(mapped - set(pins))}')
            for port in model['pins']:
                physical = port['default_pin']
                if physical not in pins:
                    raise ValueError(f'{association}: missing physical pin {physical} for {port["id"]}')
                # Common semiconductor terminal names have an unambiguous electrical meaning.
                if port['id'] in {'a', 'k', 'b', 'c', 'e', 'g', 'd', 's'}:
                    if pins[physical].upper() != port['id'].upper():
                        raise ValueError(f'{association}: port {port["id"]} maps to {physical} ({pins[physical]})')
            checked += 1
            print(f'{association}: {model["id"]} — pins OK')
    print(f'{checked} library associations validated.')


if __name__ == '__main__':
    main()
