"""Build the static landing page's assets (site/assets/) from this project's art, captures and trailer.

    python tools/site_assets.py

Needs Pillow and imageio-ffmpeg (as for the trailer), build/trailer/clips/*.mp4, the regression
captures in build/*.png and output/trailer/EnemyTerritory-ArcadeEdition-Trailer[-Portrait]-720p.mp4.
Brand assets (logo, cover, icon, social card) come from tools/brand.py.
UI frames come from native/ui_art_source (the project's generated limbo-style art, not original
textures) and keep the same nine-/three-slice borders as tools/ui_art.py, scaled for CSS border-image.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile

import imageio_ffmpeg
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'native/ui_art_source'
CLIPS = ROOT / 'build/trailer/clips'
OUT = ROOT / 'site/assets'

# name -> (source, width); CSS slices: panel 31, button/button_focus 21, inset 36, header caps 65 (px of the output).
ART = {'panel': ('panel.png', 512), 'button': ('button.png', 724), 'button_focus': ('button_focus.png', 724),
       'inset': ('inset.png', 724), 'dial_allies': ('dial_allies.png', 480), 'dial_axis': ('dial_axis.png', 480)}
HEADER_ROWS = 228  # Same screwed strip as tools/ui_art.py.
# Clean bot-match demo frames (seconds into each clip, see trailer/points.py and make_trailer.py).
STILLS = {'hero': ('oasis.mp4', 92.6), 'battery': ('battery.mp4', 82.0), 'fueldump': ('fueldump.mp4', 41.0),
          'goldrush': ('goldrush.mp4', 58.3), 'oasis': ('oasis.mp4', 69.8), 'radar': ('radar.mp4', 31.8),
          'railgun': ('railgun.mp4', 80.6), 'ultrawide': ('goldrush-wide.mp4', 39.5)}
CAPTURES = ['four-playing', 'input-map-local-3', 'online-four-etmain-host', 'team-wheels-v2', 'four-menus',
            'players-setup', 'map-chooser-real', 'ui-art-browser-rows', 'ui-art-main']


def save(image, name, width, quality=82):
    if image.width > width:
        image = image.resize((width, round(image.height * width / image.width)), Image.LANCZOS)
    image.save(OUT / f'{name}.webp', quality=quality, method=6)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    for name, (src, width) in ART.items():
        image = Image.open(SOURCE / src).convert('RGBA')
        if not name.startswith('dial'):  # Frames are solid; the generator leaves ~250 alpha.
            image.putalpha(255)
        save(image, name, width, 88)
    header = Image.open(SOURCE / 'header.png').convert('RGB')
    save(header.crop((0, 0, header.width, HEADER_ROWS)), 'header', 1086, 88)
    # The unpadded panorama (the runtime JPEG is letterboxed into a power-of-two texture).
    save(Image.open(ROOT / 'native/ui/assets/ultrawide_storm-source.png').convert('RGB'), 'storm', 1940, 78)

    ffmpeg = imageio_ffmpeg.get_ffmpeg_exe()
    with tempfile.TemporaryDirectory() as tmp:
        for name, (clip, at) in STILLS.items():
            frame = Path(tmp) / f'{name}.png'
            subprocess.run([ffmpeg, '-loglevel', 'error', '-y', '-ss', str(at), '-i', str(CLIPS / clip),
                            '-frames:v', '1', str(frame)], check=True)
            save(Image.open(frame).convert('RGB'), name, 1920 if name in ('hero', 'ultrawide') else 1280)
    for name in CAPTURES:
        save(Image.open(ROOT / f'build/{name}.png').convert('RGB'), name, 1280, 86)

    # 720p web cuts of both trailers; posters show the wordmark at 14 s.
    for suffix, out, width in (('', 'trailer', 1280), ('-Portrait', 'trailer-portrait', 720)):
        trailer = ROOT / f'output/trailer/EnemyTerritory-ArcadeEdition-Trailer{suffix}-720p.mp4'
        shutil.copy2(trailer, OUT / f'{out}.mp4')
        poster = OUT / f'{out}-poster.png'
        subprocess.run([ffmpeg, '-loglevel', 'error', '-y', '-ss', '14', '-i', str(trailer), '-frames:v', '1',
                        str(poster)], check=True)
        save(Image.open(poster).convert('RGB'), out.replace('trailer', 'trailer-poster'), width)
        poster.unlink()
    for path in sorted(OUT.iterdir()):
        print(f'{path.name:28} {path.stat().st_size // 1024:6} KB')


if __name__ == '__main__':
    main()
