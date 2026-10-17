#!/usr/bin/env python3
"""Check .casim associations against installed KiCad symbol pin numbers and terminal names."""
import argparse
import json
import re
from pathlib import Path


def library_symbols(path):
    text = path.read_text()
    roots = list(re.finditer(r'^\t\(symbol "([^"\n]+)"', text, re.MULTILINE))
    if not roots:
        raise ValueError(f'{path}: no top-level library symbols found')
    return {match.group(1): text[match.start(): roots[i + 1].start() if i + 1 < len(roots) else len(text)]
            for i, match in enumerate(roots)}


def symbol_pins(symbols, name):
    visited = set()
    while True:
        if name in visited:
            raise ValueError(f'cyclic symbol inheritance: {name}')
        visited.add(name)
        body = symbols[name]
        parent = re.search(r'\(extends "([^"]+)"', body)
        if not parent:
            break
        name = parent.group(1)
    result = {}
    for body in re.split(r'\(pin (?:input|output|bidirectional|tri_state|passive|free|unspecified|power_in|power_out|open_collector|open_emitter|no_connect) ', body)[1:]:
        number = re.search(r'\(number "([^"]+)"', body)
        label = re.search(r'\(name "([^"]+)"', body)
        if number and label:
            result[number.group(1)] = label.group(1)
    return result


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
