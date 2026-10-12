"""Add Omni-bot navigation for every map in the upstream archive to runtime/omnibot/et/nav.

    python tools/omnibot_nav.py [--archive build/omnibot-reference/omnibot-windows-latest.zip]

Copies <map>.way, <map>.gm and <map>_goals.gm files that are missing; existing (possibly locally
tuned) files are never overwritten. Maps with a .way file are shown as "bots" in the local map list.
"""
import argparse
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--archive', type=Path, default=ROOT / 'build/omnibot-reference/omnibot-windows-latest.zip')
    args = parser.parse_args()
    target = ROOT / 'runtime/omnibot/et/nav'
    target.mkdir(parents=True, exist_ok=True)
    added = kept = 0
    with zipfile.ZipFile(args.archive) as archive:
        for info in archive.infolist():
            path = info.filename.replace(chr(92), '/')
            name = path.rsplit('/', 1)[-1]
            if '/et/nav/' not in path or info.is_dir() or not name.lower().endswith(('.way', '.gm')):
                continue
            destination = target / name
            if destination.exists():
                kept += 1
                continue
            destination.write_bytes(archive.read(info))
            added += 1
    maps = len(list(target.glob('*.way')))
    print(f'{added} navigation files added, {kept} kept; {maps} maps with bot navigation in {target}')


if __name__ == '__main__':
    main()
