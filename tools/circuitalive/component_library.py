"""Promote project libraries to the versioned CircuitAliveEDA component collection.

Only native component assets are copied. Project files and source library tables
are never modified. Installed tables use the same relocatable variables as KiCad.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import tempfile

ROOT = Path(__file__).resolve().parents[2]
COLLECTION = ROOT / 'resources/component-libraries'
TABLES = (('sym-lib-table', 'sym_lib_table', 'symbols', '.kicad_sym', 'SYMBOL'),
          ('fp-lib-table', 'fp_lib_table', 'footprints', '.pretty', 'FOOTPRINT'))


def quoted(value):
    return json.dumps(str(value), ensure_ascii=False)


class Node:
    def __init__(self, start, end, value=None, children=None):
        self.start, self.end, self.value = start, end, value
        self.children = children or []

    def fields(self, name):
        return [n for n in self.children if n.children and n.children[0].value == name]

    def field(self, name):
        matches = self.fields(name)
        return matches[0].children[1].value if matches else None


def parse(text):
    """Read balanced expressions while retaining original formatting for edits."""
    stack, roots = [], []
    token = re.compile(r'\s+|;[^\n]*|\(|\)|"(?:\\.|[^"\\])*"|[^\s()";]+')
    position = 0
    for match in token.finditer(text):
        if match.start() != position:
            raise ValueError(f'Invalid library syntax near offset {position}')
        position = match.end()
        value = match[0]
        if value.isspace() or value.startswith(';'):
            continue
        if value == '(':
            node = Node(match.start(), None)
            (stack[-1].children if stack else roots).append(node)
            stack.append(node)
        elif value == ')':
            if not stack:
                raise ValueError('Unbalanced library expression')
            stack.pop().end = match.end()
        else:
            if value.startswith('"'):
                value = re.sub(r'\\(.)', lambda m: {'n': '\n', 'r': '\r', 't': '\t'}.get(m[1], m[1]), value[1:-1])
            (stack[-1].children if stack else roots).append(Node(match.start(), match.end(), value))
    if stack or len(roots) != 1 or position != len(text) or not roots[0].children:
        raise ValueError('Invalid or incomplete library expression')
    return roots[0]


def replace(text, edits):
    for node, value in sorted(edits, key=lambda edit: edit[0].start, reverse=True):
        text = text[:node.start] + quoted(value) + text[node.end:]
    return text


def write_changed(path, content):
    path = Path(path)
    if path.is_file() and path.read_text(encoding='utf-8') == content:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + '.tmp')
    temporary.write_text(content, encoding='utf-8')
    temporary.replace(path)


def safe_name(name):
    if not name or name in ('.', '..') or any(c in name for c in '/\\:"${}'):
        raise ValueError(f'Invalid library nickname: {name!r}')
    return name


def resolve_uri(uri, project):
    variables = dict(os.environ, KIPRJMOD=str(project))
    def expand(match):
        if match[1] not in variables:
            raise ValueError(f'Undefined variable in component path: {match[1]}')
        return variables[match[1]]
    value = re.sub(r'\$\{([^}]+)\}', expand, uri)
    path = Path(value)
    return path if path.is_absolute() else project / path


def sources(path, name=None):
    path = Path(path).resolve()
    if path.suffix == '.kicad_sym':
        return [(safe_name(name or path.stem), path)], []
    if path.suffix == '.pretty' and path.is_dir():
        return [], [(safe_name(name or path.stem), path)]
    project = path if path.is_dir() else path.parent
    result = []
    for filename, kind, *_ in TABLES:
        table = project / filename
        entries = []
        if table.is_file():
            tree = parse(table.read_text(encoding='utf-8'))
            if tree.children[0].value != kind:
                raise ValueError(f'Unexpected library table: {table}')
            seen = set()
            for row in tree.fields('lib'):
                if row.field('type') != 'KiCad':
                    raise ValueError(f'Only native KiCad libraries can be bundled: {table}')
                nickname = safe_name(row.field('name'))
                if nickname in seen:
                    raise ValueError(f'Duplicate library nickname in {table}: {nickname}')
                seen.add(nickname)
                entries.append((nickname, resolve_uri(row.field('uri'), project)))
        result.append(entries)
    if not any(result):
        raise ValueError(f'No component libraries found in {project}')
    return result


def import_components(source, collection=COLLECTION, name=None, model_map=None):
    """Stage and validate the whole import before changing the collection."""
    collection = Path(collection)
    symbol_sources, footprint_sources = sources(source, name)
    project = Path(source).resolve()
    if not project.is_dir() or project.suffix == '.pretty':
        project = project.parent
    model_map = model_map or {}
    with tempfile.TemporaryDirectory(prefix='circuitalive-components-') as directory:
        staged = Path(directory)
        footprint_ids = {}
        for nickname, library in footprint_sources:
            files = sorted(library.glob('*.kicad_mod'))
            if not files:
                raise ValueError(f'Empty or missing footprint library: {library}')
            for file in files:
                text = file.read_text(encoding='utf-8')
                tree = parse(text)
                if tree.children[0].value not in ('footprint', 'module'):
                    raise ValueError(f'Invalid footprint: {file}')
                edits = []
                for model in tree.fields('model'):
                    uri = model.children[1].value
                    mapped = model_map.get(uri, uri)
                    model_path = resolve_uri(mapped, project)
                    if not model_path.is_file():
                        model_path = resolve_uri(mapped, file.parent)
                    if not model_path.is_file():
                        raise ValueError(f'Missing 3D model {uri!r} in {file}; use --model-map OLD=FILE')
                    target = staged / '3dmodels' / (nickname + '.3dshapes') / model_path.name
                    target.parent.mkdir(parents=True, exist_ok=True)
                    if target.exists() and target.read_bytes() != model_path.read_bytes():
                        raise ValueError(f'Conflicting 3D model filename: {model_path.name}')
                    shutil.copy2(model_path, target)
                    edits.append((model.children[1], '${KICAD10_3DMODEL_DIR}/' + nickname
                                  + '.3dshapes/' + model_path.name))
                write_changed(staged / 'footprints' / (nickname + '.pretty') / file.name, replace(text, edits))
                footprint_ids.setdefault(file.stem, []).append(nickname + ':' + file.stem)
        for nickname, library in symbol_sources:
            text = library.read_text(encoding='utf-8')
            tree = parse(text)
            if tree.children[0].value != 'kicad_symbol_lib' or not tree.fields('symbol'):
                raise ValueError(f'Invalid or empty symbol library: {library}')
            edits = []
            for symbol in tree.fields('symbol'):
                for prop in symbol.fields('property'):
                    if prop.children[1].value != 'Footprint':
                        continue
                    value = prop.children[2].value
                    if value and ':' not in value:
                        matches = footprint_ids.get(value, [])
                        # A symbol-only import may reference an already promoted footprint.
                        if not matches:
                            matches = [p.parent.stem + ':' + p.stem for p in
                                       (collection / 'footprints').glob('*.pretty/' + value + '.kicad_mod')]
                        if len(matches) != 1:
                            raise ValueError(f'Unresolved or ambiguous footprint {value!r} in {library}')
                        edits.append((prop.children[2], matches[0]))
                    elif value:
                        fp_library, fp_name = value.split(':', 1)
                        relative = Path('footprints') / (fp_library + '.pretty') / (fp_name + '.kicad_mod')
                        standard = Path(os.environ.get('KICAD10_FOOTPRINT_DIR', '/nonexistent')) / (fp_library + '.pretty') / (fp_name + '.kicad_mod')
                        if not any(p.is_file() for p in (staged / relative, collection / relative, standard)):
                            raise ValueError(f'Missing footprint {value!r} in {library}')
            write_changed(staged / 'symbols' / (nickname + '.kicad_sym'), replace(text, edits))
        for file in staged.rglob('*'):
            if file.is_file():
                target = collection / file.relative_to(staged)
                target.parent.mkdir(parents=True, exist_ok=True)
                if not target.is_file() or target.read_bytes() != file.read_bytes():
                    shutil.copy2(file, target)


def library_rows(collection):
    aliases_file = collection / 'aliases.json'
    aliases = json.loads(aliases_file.read_text(encoding='utf-8')) if aliases_file.exists() else {}
    result = {}
    for table, kind, folder, suffix, variable in TABLES:
        entries = {p.stem: p.name for p in sorted((collection / folder).glob('*' + suffix))}
        for alias, canonical in aliases.get(folder, {}).items():
            safe_name(alias)
            if canonical not in entries:
                raise ValueError(f'Unknown library alias target: {canonical}')
            entries[alias] = entries[canonical]
        result[table] = {name: f'${{KICAD10_{variable}_DIR}}/{file}' for name, file in entries.items()}
    return result


def merge_table(path, kind, entries, repair_project_paths=False, dry_run=False):
    if not entries:
        return
    text = path.read_text(encoding='utf-8') if path.exists() else f'({kind}\n  (version 7)\n)\n'
    tree = parse(text)
    if tree.children[0].value != kind:
        raise ValueError(f'Unexpected library table: {path}')
    found, edits = set(), []
    for row in tree.fields('lib'):
        name = row.field('name')
        if name not in entries:
            continue
        current = row.field('uri')
        expected = entries[name]
        if current != expected:
            if repair_project_paths and '${KIPRJMOD}' in (current or ''):
                # Only explicitly promoted names are repaired; other personal rows survive.
                uri = row.fields('uri')[0].children[1]
                edits.append((uri, expected))
            else:
                raise ValueError(f'Library nickname conflict in {path}: {name}')
        found.add(name)
    text = replace(text, edits)
    tree = parse(text)
    addition = ''.join(f'  (lib (name {quoted(name)}) (type "KiCad") (uri {quoted(uri)}) '
                       '(options "") (descr "CircuitAliveEDA components"))\n'
                       for name, uri in entries.items() if name not in found)
    if dry_run:
        return
    if edits:
        backup = path.with_name(path.name + '.before-component-promotion')
        if not backup.exists():
            shutil.copy2(path, backup)
    updated = text[:tree.end - 1] + addition + text[tree.end - 1:]
    if repair_project_paths and path.exists() and updated != path.read_text(encoding='utf-8'):
        backup = path.with_name(path.name + '.before-components')
        if not backup.exists():
            shutil.copy2(path, backup)
    write_changed(path, updated)


def install_collection(data, collection=COLLECTION, profile=None):
    """Overlay versioned assets without replacing standard or personal table rows."""
    data, collection = Path(data), Path(collection)
    rows = library_rows(collection)
    # Validate all tables before copying anything, including profile conflicts.
    for filename, kind, *_ in TABLES:
        merge_table(data / 'template' / filename, kind, rows[filename], dry_run=True)
        if profile:
            merge_table(Path(profile) / filename, kind, rows[filename],
                        repair_project_paths=True, dry_run=True)
    stamp = data / 'circuitalive-component-files.json'
    previous = json.loads(stamp.read_text(encoding='utf-8')) if stamp.exists() else {}
    files, hashes = [], {}
    for folder in ('symbols', 'footprints', '3dmodels'):
        for file in (collection / folder).rglob('*'):
            if file.is_file():
                relative = file.relative_to(collection).as_posix()
                target = data / relative
                digest = hashlib.sha256(file.read_bytes()).hexdigest()
                hashes[relative] = digest
                if target.exists():
                    current = hashlib.sha256(target.read_bytes()).hexdigest()
                    if current != digest and current != previous.get(relative):
                        raise ValueError(f'Installed component has local changes: {target}. '
                                         'Incorpore as alterações antes de sincronizar.')
                files.append((file, target, digest))
    for file, target, digest in files:
        target.parent.mkdir(parents=True, exist_ok=True)
        if not target.exists() or hashlib.sha256(target.read_bytes()).hexdigest() != digest:
            shutil.copy2(file, target)
    for filename, kind, *_ in TABLES:
        merge_table(data / 'template' / filename, kind, rows[filename])
        if profile:
            merge_table(Path(profile) / filename, kind, rows[filename], repair_project_paths=True)
    write_changed(stamp, json.dumps(hashes, indent=2) + '\n')


def refresh_sources(root=ROOT):
    registry = root / '.circuitalive/component-sources.json'
    if registry.is_file():
        for entry in json.loads(registry.read_text(encoding='utf-8')):
            # A disconnected project must not remove the last versioned copy.
            if Path(entry['source']).exists():
                import_components(entry['source'], root / 'resources/component-libraries',
                                  entry.get('name'), entry.get('model_map'))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    add = sub.add_parser('add', help='Incorporar uma biblioteca ou bibliotecas de um projeto')
    add.add_argument('source', type=Path)
    add.add_argument('--name')
    add.add_argument('--model-map', action='append', default=[], metavar='OLD=FILE')
    add.add_argument('--watch', action='store_true', help='Sincronizar esta origem ao iniciar e empacotar')
    sync = sub.add_parser('sync', help='Sincronizar origens e instalar a coleção local')
    sync.add_argument('--data', type=Path, default=ROOT / '.circuitalive/libraries/usr/share/kicad')
    sync.add_argument('--profile', type=Path)
    args = parser.parse_args()
    try:
        if args.command == 'add':
            model_map = {}
            for pair in args.model_map:
                old, new = pair.split('=', 1)
                model_map[old] = new
            import_components(args.source, name=args.name, model_map=model_map)
            if args.watch:
                registry = ROOT / '.circuitalive/component-sources.json'
                entries = json.loads(registry.read_text(encoding='utf-8')) if registry.exists() else []
                source = str(args.source.resolve())
                entries = [e for e in entries if e['source'] != source]
                entries.append(dict(source=source, name=args.name, model_map=model_map))
                write_changed(registry, json.dumps(entries, indent=2, ensure_ascii=False) + '\n')
            print(f'Componentes incorporados em {COLLECTION}')
        else:
            refresh_sources()
            install_collection(args.data, profile=args.profile)
    except (ValueError, OSError) as error:
        parser.exit(1, f'Bibliotecas: {error}\n')


if __name__ == '__main__':
    main()
