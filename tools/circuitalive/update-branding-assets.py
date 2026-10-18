#!/usr/bin/env python3
"""Render shipped brand assets from the existing logo, using ImageMagick."""
from pathlib import Path
import base64
import re
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
BITMAPS = ROOT / 'resources/bitmaps_png'
LOGO = BITMAPS / 'png/icon_kicad_256.png'


def data_uri(path):
    return 'data:image/png;base64,' + base64.b64encode(path.read_bytes()).decode()


def render(source, target, size=None):
    args = ['magick', '-background', 'none', str(source)]
    if size:
        args += ['-resize', f'{size}x{size}']
    subprocess.run(args + [str(target)], check=True)


def icns(source, target, temporary):
    chunks = []
    for size, kind in [(128, b'ic07'), (256, b'ic08'), (512, b'ic09'), (1024, b'ic10')]:
        png = temporary / f'{target.stem}-{size}.png'
        render(source, png, size)
        data = png.read_bytes()
        chunks.append(kind + struct.pack('>I', len(data) + 8) + data)
    data = b''.join(chunks)
    target.write_bytes(b'icns' + struct.pack('>I', len(data) + 8) + data)


def ico(source, target):
    subprocess.run(['magick', '-background', 'none', str(source), '-define',
                    'icon:auto-resize=256,128,64,48,32,16', str(target)], check=True)


def svg(body, width=128, height=128):
    return (f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" '
            f'viewBox="0 0 {width} {height}"><title>CircuitAliveEDA</title>{body}</svg>\n')


def main():
    logo_uri = data_uri(LOGO)
    for theme in ('light', 'dark'):
        sources = BITMAPS / 'sources' / theme
        color = '#101820' if theme == 'light' else '#f3f6f8'
        (sources / 'kicad_logo.svg').write_text(svg(
            f'<image x="152" y="16" width="96" height="96" href="{logo_uri}"/>'
            f'<text x="200" y="157" text-anchor="middle" font-family="DejaVu Sans,sans-serif" '
            f'font-size="30" font-weight="bold" fill="{color}">CircuitAliveEDA</text>', 400, 200))
        (sources / 'splash.svg').write_text(svg(
            '<rect width="620" height="300" fill="#10151e"/>'
            '<path d="M0 45H140L175 80H620M0 255H420L455 220H620" fill="none" stroke="#153c48" stroke-width="2"/>'
            '<circle cx="175" cy="80" r="5" fill="#05b8cd"/><circle cx="455" cy="220" r="5" fill="#05b8cd"/>'
            f'<image x="258" y="39" width="104" height="104" href="{logo_uri}"/>'
            '<text x="310" y="191" text-anchor="middle" font-family="DejaVu Sans,sans-serif" font-size="35" '
            'font-weight="bold" fill="#f3f6f8">CircuitAliveEDA</text>'
            '<text x="310" y="223" text-anchor="middle" font-family="DejaVu Sans,sans-serif" font-size="15" '
            'fill="#9aaabb">Design. Simulate. Bring circuits to life.</text>', 620, 300))
        render(sources / 'splash.svg', BITMAPS / 'png' / ('splash.png' if theme == 'light' else 'splash_dark.png'))

    roles = {'project': 'kicad', 'schematic': 'eeschema', 'pcb': 'pcbnew',
             'symbol': 'libedit', 'footprint': 'modedit', 'worksheet': 'pagelayout_editor',
             'gerber': 'gerbview'}
    bundles = {'project': ['kicad/kicad_doc.icns'],
               'schematic': ['eeschema/eeschema_doc.icns', 'cvpcb/cvpcb_doc.icns'],
               'pcb': ['pcbnew/pcbnew_doc.icns'], 'symbol': ['eeschema/libedit_doc.icns'],
               'footprint': ['pcbnew/fpedit_doc.icns'],
               'worksheet': ['pagelayout_editor/pagelayout_editor_doc.icns'],
               'gerber': ['gerbview/gerbview_doc.icns']}
    with tempfile.TemporaryDirectory(prefix='circuitalive-assets-') as temp:
        temporary = Path(temp)
        for role, icon in roles.items():
            icon_uri = data_uri(BITMAPS / 'png' / f'icon_{icon}_256.png')
            document = svg(
                '<path d="M24 2H80L110 32V120Q110 126 104 126H24Q16 126 16 118V10Q16 2 24 2Z" '
                'fill="#f4f6f8" stroke="#aab2bd" stroke-width="2"/>'
                '<path d="M80 2V32H110M32 45H91M32 57H91M32 69H75" fill="none" stroke="#c5cdd6" stroke-width="3"/>'
                f'<image x="2" y="60" width="64" height="64" href="{icon_uri}"/>')
            source = temporary / f'{role}.svg'
            source.write_text(document)
            for p in (ROOT / 'resources/linux/icons/hicolor/scalable/mimetypes').glob(f'application-x-kicad-{role}*.svg'):
                match = re.search(r'-(\d+)\.svg$', p.name)
                size = int(match[1]) if match else 128
                p.write_text(document.replace('width="128" height="128"', f'width="{size}" height="{size}"', 1))
            for p in (ROOT / 'resources/linux/icons/hicolor').glob(f'*x*/mimetypes/application-x-kicad-{role}.png'):
                render(source, p, int(p.parent.parent.name.split('x')[0]))
            document_ico = BITMAPS / 'icons' / f'fileicon_{role}.ico'
            if document_ico.exists():
                ico(source, document_ico)
            if role == 'project':
                for theme in ('light', 'dark'):
                    (BITMAPS / 'sources' / theme / 'project_kicad.svg').write_text(document)
                for p in (BITMAPS / 'png').glob('project_kicad*.png'):
                    render(source, p, int(p.stem.rsplit('_', 1)[1]))
            for name in bundles[role]:
                icns(source, ROOT / name, temporary)

        for p in (ROOT / 'resources/linux/icons/hicolor').glob('*x*/apps/kicad.png'):
            render(LOGO, p, int(p.parent.parent.name.split('x')[0]))
        for p in (ROOT / 'resources/linux/icons/hicolor/scalable/apps').glob('kicad*.svg'):
            p.write_text((BITMAPS / 'sources/light/icon_kicad.svg').read_text())
        for p in (BITMAPS / 'icons').glob('icon_kicad*.xpm'):
            render(LOGO, p, 64 if '_64' in p.stem else 128)
        apps = {'kicad/kicad.icns': 'kicad', 'eeschema/eeschema.icns': 'eeschema',
                'eeschema/libedit.icns': 'libedit', 'pcbnew/pcbnew.icns': 'pcbnew',
                'pcbnew/fpedit.icns': 'modedit', 'cvpcb/cvpcb.icns': 'cvpcb',
                'gerbview/gerbview.icns': 'gerbview', 'pcb_calculator/pcb_calculator.icns': 'pcbcalculator',
                'pagelayout_editor/pagelayout_editor.icns': 'pagelayout_editor',
                'bitmap2component/bitmap2component.icns': 'bitmap2component'}
        for name, icon in apps.items():
            source = BITMAPS / 'png' / f'icon_{icon}_256.png'
            icns(source, ROOT / name, temporary)
            app_ico = BITMAPS / 'icons' / f'icon_{icon}.ico'
            if app_ico.exists():
                ico(source, app_ico)
    print('CircuitAliveEDA splash, logos, document icons and platform icons rendered.')


if __name__ == '__main__':
    main()
