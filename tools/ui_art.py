"""Cut the generated UI art (native/ui_art_source/*.png) into stretchable game textures.

    python tools/ui_art.py

Writes native/ui/art/<asset>_<piece>.tga (32-bit, uncompressed, bottom-up: the format both
ET's loader and the engine's own UI-art loader read). Nine-slice assets become tl/t/tr/l/c/r/bl/b/br,
three-slice rows (<asset>3_l/c/r, inset as field3) and the title bar become l/c/r, the dial stays one image. Borders are measured in source pixels.
The sources are new images generated in the style of the original limbo menu; no original
game texture is copied.
"""
from pathlib import Path
import struct

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'native/ui_art_source'
OUT = ROOT / 'native/ui/art'
# Nine-slice border per asset in source pixels; output pieces are 64 (border) / 256 (stretch) px.
NINE = {'panel': 76, 'button': 64, 'button_focus': 64, 'inset': 72}
HEADER_ROWS, HEADER_CAP = 228, 130  # The generated strip: the screwed bar occupies rows 0..227.
# Three-slice rows (menus: one item per piece; caps scale with the drawn height).
THREE = {'button': 'button', 'button_focus': 'button_focus', 'inset': 'field'}


def write_tga(image, path, opaque=True):
    image = image.convert('RGBA').transpose(Image.FLIP_TOP_BOTTOM)  # Bottom-up rows.
    r, g, b, a = image.split()
    # Frames are solid; the generator leaves ~250 alpha. Only the dial keeps its cut-out.
    a = a.point(lambda v: 255 if opaque or v >= 250 else v)
    data = Image.merge('RGBA', (b, g, r, a)).tobytes()  # BGRA.
    path.write_bytes(struct.pack('<BBBHHBHHHHBB', 0, 0, 2, 0, 0, 0, 0, 0, image.width, image.height, 32, 8) + data)


def nine(name, border):
    image = Image.open(SOURCE / f'{name}.png').convert('RGBA')
    w, h = image.size
    xs, ys = (0, border, w - border, w), (0, border, h - border, h)
    pieces = (('tl', 'l', 'bl'), ('t', 'c', 'b'), ('tr', 'r', 'br'))
    for i in range(3):
        for j in range(3):
            piece = image.crop((xs[i], ys[j], xs[i + 1], ys[j + 1]))
            size = (256 if i == 1 else 64, 256 if j == 1 else 64)
            write_tga(piece.resize(size, Image.LANCZOS), OUT / f'{name}_{pieces[i][j]}.tga')


def three(name, border, output):
    image = Image.open(SOURCE / f'{name}.png').convert('RGBA')
    w, h = image.size
    for piece, box, size in (('l', (0, 0, border, h), (32, 256)), ('c', (border, 0, w - border, h), (256, 256)),
                             ('r', (w - border, 0, w, h), (32, 256))):
        write_tga(image.crop(box).resize(size, Image.LANCZOS), OUT / f'{output}3_{piece}.tga')


def header():
    image = Image.open(SOURCE / 'header.png').convert('RGBA')
    image = image.crop((0, 0, image.width, HEADER_ROWS))
    w = image.width
    for piece, box, size in (('l', (0, 0, HEADER_CAP, HEADER_ROWS), (64, 128)),
                             ('c', (HEADER_CAP, 0, w - HEADER_CAP, HEADER_ROWS), (256, 128)),
                             ('r', (w - HEADER_CAP, 0, w, HEADER_ROWS), (64, 128))):
        write_tga(image.crop(box).resize(size, Image.LANCZOS), OUT / f'header_{piece}.tga')


def dial():
    # Generic dial plus the team shell bases (US brass / German lacquered steel headstamps).
    for name in ('dial', 'dial_allies', 'dial_axis'):
        image = Image.open(SOURCE / f'{name}.png').convert('RGBA')
        write_tga(image.resize((512, 512), Image.LANCZOS), OUT / f'{name}.tga', opaque=False)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    for name, border in NINE.items():
        nine(name, border)
    for name, output in THREE.items():
        three(name, NINE[name], output)
    header()
    dial()
    files = sorted(OUT.glob('*.tga'))
    print(f'{len(files)} textures, {sum(f.stat().st_size for f in files) / 2**20:.1f} MiB in {OUT}')


if __name__ == '__main__':
    main()
