"""Build the Enemy Territory: Arcade Edition brand assets from the generated cover.

    python tools/brand.py

Source: output/imagegen/axis-allies-covers/03-goldrush-defence-medic.png (generated cover art).
The title band's lettering and red rules are keyed off the charcoal plate into a transparent
wordmark; everything else is derived from that wordmark and the cover:

- build/brand/wordmark.png   transparent wordmark with a soft drop shadow (trailer, site)
- native/ui/art/logo.tga     the same, for the main menu
- native/arcade.ico          window/launcher icon (cut-out "ET" on the charcoal plate)
- site/assets/{logo,cover,cover-art,band}.webp, icon.png, social.jpg (link preview)
"""
from pathlib import Path
import struct

import numpy as np
from PIL import Image, ImageFilter

ROOT = Path(__file__).resolve().parents[1]
COVER = ROOT / 'output/imagegen/axis-allies-covers/03-goldrush-defence-medic.png'
OUT = ROOT / 'build/brand'
BAND_TOP = 1130            # The charcoal title band starts here on the 1024x1536 cover.
TITLE = (40, 1150, 990, 1420)  # Lettering and rules, with room for the shadow.
PLATE = np.array([18, 16, 13], np.float32)  # Charcoal behind the lettering.


def smooth(x, lo, hi):
    x = np.clip((x - lo) / (hi - lo), 0, 1)
    return x * x * (3 - 2 * x)


def key(image):
    """Lettering (bright) and rules (red) off the dark plate, colours un-premultiplied."""
    a = np.asarray(image.convert('RGB')).astype(np.float32)
    lum = a.mean(axis=2)
    red = a[..., 0] - (a[..., 1] + a[..., 2]) / 2
    alpha = np.maximum(smooth(lum, 52, 96), smooth(red, 22, 55))
    # Drop isolated grunge specks on the plate: keep pixels with enough keyed neighbours.
    dense = Image.fromarray((alpha * 255).astype(np.uint8)).filter(ImageFilter.BoxBlur(5))
    alpha *= np.asarray(dense) > 40
    rgb = (a - PLATE * (1 - alpha[..., None])) / np.maximum(alpha[..., None], 1e-3)
    rgba = np.dstack([np.clip(rgb, 0, 255), alpha * 255]).astype(np.uint8)
    return Image.fromarray(rgba, 'RGBA')


def shadowed(mark, scale):
    """Upscale, then put a soft black drop shadow under the keyed mark."""
    mark = mark.resize((round(mark.width * scale), round(mark.height * scale)), Image.LANCZOS)
    pad, off = round(24 * scale), round(5 * scale)
    out = Image.new('RGBA', (mark.width + 2 * pad, mark.height + 2 * pad))
    shadow = Image.new('RGBA', out.size, (0, 0, 0, 0))
    shadow.putalpha(Image.new('L', out.size))
    alpha = Image.new('L', out.size); alpha.paste(mark.getchannel('A'), (pad + off, pad + off))
    shadow.putalpha(alpha.filter(ImageFilter.GaussianBlur(7 * scale)).point(lambda v: int(v * 0.8)))
    out.alpha_composite(shadow)
    out.alpha_composite(mark, (pad, pad))
    return out.crop(out.getbbox())


def write_tga(image, path):
    image = image.convert('RGBA').transpose(Image.FLIP_TOP_BOTTOM)  # Bottom-up, as tools/ui_art.py.
    r, g, b, a = image.split()
    data = Image.merge('RGBA', (b, g, r, a)).tobytes()
    path.write_bytes(struct.pack('<BBBHHBHHHHBB', 0, 0, 2, 0, 0, 0, 0, 0, image.width, image.height, 32, 8) + data)


def icon(cover, mark):
    """'E' of ENEMY over 'T' of TERRITORY, cut from the wordmark, on a square of the plate."""
    plate = cover.crop((300, BAND_TOP + 292, 404, BAND_TOP + 396)).convert('RGBA')  # Empty charcoal under the title.
    e = mark.crop((184, 0, 276, 90))     # Coordinates in the keyed title (tools/brand.py measured).
    e = e.crop(e.getbbox())
    t = mark.crop((12, 92, 121, 206)); t = t.crop(t.getbbox())
    h = 150
    e = e.resize((round(e.width * h / e.height), h), Image.LANCZOS)
    t = t.resize((round(t.width * h * 1.2 / t.height), round(h * 1.2)), Image.LANCZOS)  # T keeps its flared stem.
    letters = Image.new('RGBA', (e.width + t.width + 4, t.height))
    letters.alpha_composite(e, (0, 0)); letters.alpha_composite(t, (e.width + 4, 0))
    letters = shadowed(letters, 1.0)
    letters = letters.resize((228, round(228 * letters.height / letters.width)), Image.LANCZOS)
    tile = plate.resize((256, 256), Image.LANCZOS)
    y = (256 - letters.height) // 2 - 4
    red = Image.new('RGBA', (212, 9), (150, 36, 30, 255))
    tile.alpha_composite(red, (22, y - 8)); tile.alpha_composite(red, (22, y + letters.height + 2))
    tile.alpha_composite(letters, ((256 - letters.width) // 2, y))
    return tile


def social(cover, wordmark):
    """1200x630 link preview: the cover scene, darkened on the right under the wordmark."""
    scene = cover.crop((0, 0, 1024, BAND_TOP)).resize((1200, round(1200 * BAND_TOP / 1024)), Image.LANCZOS)
    card = scene.crop((0, 170, 1200, 800)).convert('RGBA')
    shade = Image.linear_gradient('L').rotate(90).resize((1200, 630)).point(lambda v: int(40 + v * 0.75))
    dark = Image.new('RGBA', card.size, (8, 8, 7, 255)); dark.putalpha(shade)
    card.alpha_composite(dark)
    mark = wordmark.resize((640, round(640 * wordmark.height / wordmark.width)), Image.LANCZOS)
    card.alpha_composite(mark, (1200 - mark.width - 30, (630 - mark.height) // 2))
    return card.convert('RGB')


def webp(image, name, width, quality=86):
    image = image.resize((width, round(width * image.height / image.width)), Image.LANCZOS)
    image.save(ROOT / 'site/assets' / f'{name}.webp', quality=quality, method=6)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    cover = Image.open(COVER).convert('RGB')
    mark = key(cover.crop(TITLE))
    mark = mark.crop(mark.getbbox())
    wordmark = shadowed(mark, 2.0)
    wordmark.save(OUT / 'wordmark.png')
    write_tga(wordmark.resize((1024, 256), Image.LANCZOS), ROOT / 'native/ui/art/logo.tga')  # Power of two; menus set the aspect.
    tile = icon(cover, mark)
    tile.save(OUT / 'icon.png')
    tile.save(ROOT / 'native/arcade.ico', sizes=[(s, s) for s in (16, 24, 32, 48, 64, 128, 256)])
    webp(wordmark, 'logo', 1400)
    webp(cover, 'cover', 720)
    webp(cover.crop((0, 0, 1024, BAND_TOP)), 'cover-art', 1024)
    webp(cover.crop((0, BAND_TOP, 1024, 1536)), 'band', 1024)
    tile.resize((64, 64), Image.LANCZOS).save(ROOT / 'site/assets/icon.png')
    social(cover, wordmark).save(ROOT / 'site/assets/social.jpg', quality=88)
    print('wordmark', wordmark.size, 'mark', mark.size)


if __name__ == '__main__':
    main()
