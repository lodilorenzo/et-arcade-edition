"""One command: build, package, update a local portable copy and play it.

    python tools/play_portable.py [--no-build] [Play-ET.exe arguments...]

Builds build/slice (incremental; configures it if missing), runs tools/package.py, extracts the
game zip over dist/play/EnemyTerritory-ArcadeEdition-dev (program files are replaced, saved profiles,
downloads, identity and copied ET data are kept) and starts its Play-ET.exe. On first run the
original files are imported from runtime/etmain when present; otherwise Play-ET.exe asks.
"""
import argparse
from pathlib import Path
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]
DIST = ROOT / 'dist'
NAME = 'EnemyTerritory-ArcadeEdition-dev'
ORIGINAL = ('pak0.pk3', 'pak1.pk3', 'pak2.pk3', 'mp_bin.pk3')


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--no-build', action='store_true', help='Package and play the existing build')
    args, game_args = parser.parse_known_args()
    if subprocess.call([sys.executable, str(ROOT / 'tools/package.py'), '--out', str(DIST), '--version', 'dev'] +
                       (['--no-build'] if args.no_build else [])):
        raise SystemExit('Packaging failed.')
    play = DIST / 'play'
    with zipfile.ZipFile(DIST / f'{NAME}.zip') as archive:
        archive.extractall(play)  # Overwrites shipped files only; player data is not in the zip.
    folder = play / NAME
    command = [str(folder / 'Play-ET.exe')]
    originals = ROOT / 'runtime/etmain'
    if not all((folder / 'etmain' / n).is_file() for n in ORIGINAL) and all((originals / n).is_file() for n in ORIGINAL):
        command += ['--import', str(originals)]  # Your own extracted 2.60b files; verified by the launcher.
    print(f'> {subprocess.list2cmdline(command + game_args)}', flush=True)
    code = subprocess.call(command + game_args, cwd=folder)
    if code == 2:
        print(f'Original ET files are missing; see {folder / "launcher.log"}.')
    raise SystemExit(code)


if __name__ == '__main__':
    main()
