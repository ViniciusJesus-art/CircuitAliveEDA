#!/usr/bin/env python3
"""Distribute the user's existing artwork across KiCad's runtime/platform icon variants.

Requires Pillow. This only resizes/reuses supplied pixels; it does not redesign the logo.
Run before building. Authored PNG/ICO/XPM files in icons/ remain the source assets.
"""
from pathlib import Path
from io import BytesIO
import base64
import shutil
from PIL import Image, ImageOps

ROOT = Path(__file__).resolve().parents[2]
ICONS = ROOT / 'resources/bitmaps_png/icons'
PNG = ROOT / 'resources/bitmaps_png/png'
SOURCES = ROOT / 'resources/bitmaps_png/sources'
LINUX = ROOT / 'resources/linux/icons/hicolor'


def main():
    authored = {size: Image.open(ICONS / filename).convert('RGBA') for size, filename in
                [(64, 'icon_kicad_64.png'), (128, 'icon_kicad.png')]}
    ico = Image.open(ICONS / 'icon_kicad.ico')
    for size in ico.ico.sizes():
        if size[0] == size[1] and size[0] not in authored:
            authored[size[0]] = ico.ico.getimage(size).convert('RGBA')
    master = authored[max(authored)]

    def artwork(size):
        if size[0] == size[1] and size[0] in authored:
            return authored[size[0]].copy()
        image = ImageOps.contain(master, size, Image.Resampling.LANCZOS)
        canvas = Image.new('RGBA', size)
        canvas.alpha_composite(image, ((size[0]-image.width)//2, (size[1]-image.height)//2))
        return canvas

    def svg(size):
        stream = BytesIO()
        artwork((size, size)).save(stream, format='PNG')
        data = base64.b64encode(stream.getvalue()).decode('ascii')
        return (f'<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" '
                f'width="{size}" height="{size}" viewBox="0 0 {size} {size}">'
                f'<title>CircuitAliveEDA</title><image width="{size}" height="{size}" '
                f'xlink:href="data:image/png;base64,{data}"/></svg>\n')

    count = 0
    for path in PNG.glob('*.png'):
        if path.stem.startswith(('icon_kicad', 'kicad_logo_small')):
            with Image.open(path) as current:
                size = current.size
            artwork(size).save(path)
            count += 1
    # Keep SVG source regeneration consistent with the supplied raster artwork.
    for theme in ('light', 'dark'):
        for path in (SOURCES/theme).glob('*.svg'):
            if path.stem.startswith(('icon_kicad', 'kicad_logo_small')):
                suffix = path.stem.rsplit('_', 1)[-1]
                path.write_text(svg(int(suffix) if suffix.isdigit() else 256))
    for path in LINUX.glob('*/apps/kicad.png'):
        with Image.open(path) as current:
            size = current.size
        artwork(size).save(path)
        artwork(size).save(path.with_name('circuitaliveeda.png'))
    for name in ('kicad.svg', 'circuitaliveeda.svg'):
        (LINUX/'scalable/apps'/name).write_text(svg(256))
    shutil.copyfile(ICONS/'icon_kicad.ico', ICONS/'fileicon_project.ico')
    print(f'Updated {count} runtime PNGs, SVG sources, Linux icons and the project ICO.')


if __name__ == '__main__':
    main()
