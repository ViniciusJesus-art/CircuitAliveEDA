"""Read symbol identities and physical pins for offline .casim authoring/audits.

This reads KiCad's canonical library serialization, including inherited symbols,
multi-unit parts and alternate drawings. It is not a simulation allowlist.
"""
import re
from dataclasses import dataclass


@dataclass(frozen=True)
class Pin:
    number: str
    name: str
    role: str
    unit: int
    style: int


def library_symbols(path):
    text = path.read_text(encoding='utf-8')
    roots = list(re.finditer(r'^\t\(symbol "((?:\\.|[^"\\])*)"', text, re.MULTILINE))
    return {match.group(1): text[match.start():roots[i + 1].start() if i + 1 < len(roots) else len(text)]
            for i, match in enumerate(roots)}


def symbol_body(symbols, name):
    visited = set()
    while True:
        if name in visited:
            raise ValueError(f'cyclic symbol inheritance: {name}')
        visited.add(name)
        body = symbols[name]
        parent = re.search(r'\(extends "([^"]+)"', body)
        if not parent:
            return body
        name = parent.group(1)


def symbol_pin_records(symbols, name):
    body = symbol_body(symbols, name)
    units = list(re.finditer(r'^\t\t\(symbol "[^"\n]+_(\d+)_(\d+)"', body, re.MULTILINE))
    result = []
    for i, unit in enumerate(units):
        unit_number, style = map(int, unit.groups())
        # Body style 2 is a De Morgan/alternate drawing of the same terminals.
        if style > 1:
            continue
        section = body[unit.end():units[i + 1].start() if i + 1 < len(units) else len(body)]
        pins = list(re.finditer(r'\(pin (input|output|bidirectional|tri_state|passive|free|unspecified|power_in|power_out|open_collector|open_emitter|no_connect)\s', section))
        for j, pin in enumerate(pins):
            text = section[pin.end():pins[j + 1].start() if j + 1 < len(pins) else len(section)]
            number = re.search(r'\(number "([^"]+)"', text)
            label = re.search(r'\(name "([^"]*)"', text)
            if number:
                result.append(Pin(number.group(1), label.group(1) if label else '',
                                  pin.group(1), unit_number, style))
    return result


def symbol_pins(symbols, name):
    return {pin.number: pin.name for pin in symbol_pin_records(symbols, name)}
