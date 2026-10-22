"""Check installed component data and first-start registration on every platform."""
from pathlib import Path
import re

LIBRARIES = ('symbols', 'footprints', '3dmodels', 'template')


def validate_component_libraries(data):
    data = Path(data)
    for folder in LIBRARIES:
        if not (data / folder).is_dir():
            raise ValueError(f'Missing component library: {data / folder}')
    for table, kind, folder, suffix in (
            ('sym-lib-table', 'sym_lib_table', 'symbols', '.kicad_sym'),
            ('fp-lib-table', 'fp_lib_table', 'footprints', '.pretty')):
        path = data / 'template' / table
        if not path.is_file():
            raise ValueError(f'Missing installed library table: {path}')
        content = path.read_text(encoding='utf-8')
        uris = re.findall(r'\(uri\s+"([^"\n]+)"\)', content)
        if not content.lstrip().startswith('(' + kind) or not uris:
            raise ValueError(f'Empty or invalid installed library table: {path}')
        referenced = set()
        variable = 'SYMBOL_DIR' if folder == 'symbols' else 'FOOTPRINT_DIR'
        for uri in uris:
            match = re.fullmatch(r'\$\{KICAD10_' + variable + r'\}/([^/\\]+)', uri)
            if not match or not match[1].endswith(suffix):
                raise ValueError(f'Unexpected installed library URI in {path}: {uri}')
            library = data / folder / match[1]
            if folder == 'symbols':
                if not library.is_file() or not library.stat().st_size:
                    raise ValueError(f'Missing packed symbol library: {library}')
            elif not library.is_dir() or not any(library.glob('*.kicad_mod')):
                raise ValueError(f'Missing footprint library: {library}')
            referenced.add(library.name)
        installed = {p.name for p in (data / folder).glob('*' + suffix)}
        if installed != referenced:
            raise ValueError(f'Libraries and installed table disagree: {path}')
    for required in ('symbols/Device.kicad_sym', 'footprints/Resistor_SMD.pretty'):
        if not (data / required).exists():
            raise ValueError(f'Missing standard component library: {data / required}')
    shapes = data / '3dmodels/Resistor_SMD.3dshapes'
    if not shapes.is_dir() or not any(p.is_file() and p.suffix.lower() in ('.step', '.stp', '.wrl')
                                     for p in shapes.iterdir()):
        raise ValueError(f'Missing bundled 3D models: {shapes}')
    if not any((data / 'template').rglob('*.kicad_pro')):
        raise ValueError(f'Missing bundled project templates: {data / "template"}')


def validate_installed_assets(app):
    app = Path(app)
    validate_component_libraries(app / 'share/kicad')
    for folder in ('models', 'blocks'):
        if not any((app / 'share/circuitalive' / folder).glob('*.casim')):
            raise ValueError(f'Missing bundled simulation {folder}')


def verify_first_start(config_home):
    """Run after the installed CLI has initialized the same profile used by the GUI."""
    config_home = Path(config_home)
    tables = list(config_home.rglob('sym-lib-table'))
    if len(tables) != 1:
        raise ValueError(f'First start did not register the bundled symbol libraries: {config_home}')
    for name in ('sym-lib-table', 'fp-lib-table'):
        path = tables[0].parent / name
        if not path.is_file() or '${KICAD10_TEMPLATE_DIR}/' + name not in path.read_text(encoding='utf-8'):
            raise ValueError(f'First start did not register the installed library table: {path}')
