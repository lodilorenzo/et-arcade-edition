"""Export the current project as a source-only GitHub repository folder.

    python tools/export_source.py [--out ../EnemyTerritory-ArcadeEdition-source]

No build, Git installation, network access, or third-party Python packages required.
The destination must not exist. The working project is never modified.
"""
import argparse
from pathlib import Path
import shutil
import tempfile

from package import NAME, ROOT, stage_source

PUBLISHING = """# Publishing this source snapshot

This folder contains the current modified engine, native/Rust code, build files,
tests, tools, and custom UI art. It has no Git history or nested repositories.
LICENSE.txt is the engine's GPLv3 license with id Software's additional terms.
Preserve engine/README.txt, vendor/omnibot/COPYING.txt, vendor/omnibot/README.md,
and all source copyright/license notices. This is an unofficial altered release.
Selected ET: Legacy adaptations retain their original notices in the source.

## Build from source

See README.md for Windows/x86 MSVC, Windows SDK, CMake and Rust requirements.
From a Visual Studio x86 developer shell:

```powershell
rustup target add i686-pc-windows-msvc
cargo test --target i686-pc-windows-msvc
cmake -S . -B build/slice -A Win32 -DET_RUST_RANDOM=ON -DET_RUST_SHARED=ON
cmake --build build/slice --config Release --parallel
ctest --test-dir build/slice -C Release --output-on-failure
```

Compilation and self-contained checks do not need original game data. To play
or run game smoke checks, supply your own ET 2.60b data in runtime/etmain.
Omni-bot runtime/scripts/navigation must be supplied separately; interface
source is included, but runtime redistribution terms still need confirmation.
README.md also describes local workflows whose runtime/reference downloads,
website, trailer, and generated branding inputs are intentionally absent.
Custom UI textures/icons needed by the game are included; regenerating branding
requires the excluded local image-generation inputs.
The upstream curl test-only private-key fixture (stunnel.pem) is excluded.

## Before uploading

The exporter uses an explicit source allowlist and excludes known credentials,
profiles, installers, game archives, logs, caches, binaries and build output.
This is not a complete secret or licensing audit: review source for embedded
credentials/personal information and confirm rights to included custom artwork.
Do not add original ET assets or the Omni-bot runtime to this source repository.

Create an empty GitHub repository, then run from this folder:

```powershell
git init -b main
git add .
git diff --cached --stat
# Review staged files before committing/pushing.
git commit -m "Initial source release"
git remote add origin https://github.com/YOUR-USER/YOUR-REPO.git
git push -u origin main
```
"""


def export_source(root, destination):
    root, destination = root.resolve(), destination.resolve()
    if destination.exists():
        raise SystemExit(f'Destination already exists; choose a new folder: {destination}')
    if destination.is_relative_to(root) or root.is_relative_to(destination):
        raise SystemExit('Choose a destination outside the project directory.')
    destination.parent.mkdir(parents=True, exist_ok=True)
    # Stage completely before exposing the export; failures leave no partial repository.
    with tempfile.TemporaryDirectory(prefix='.et-source-', dir=destination.parent) as temporary:
        stage = Path(temporary) / 'source'
        stage_source(stage, root)
        shutil.copy2(stage / 'engine/COPYING.txt', stage / 'LICENSE.txt')
        (stage / 'PUBLISHING.md').write_text(PUBLISHING, encoding='utf-8')
        # mkdir exclusively refuses an existing folder, including a concurrent export.
        destination.mkdir()
        try:
            for path in stage.iterdir():
                shutil.move(str(path), destination / path.name)
        except BaseException:
            shutil.rmtree(destination)
            raise
    return destination


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--out', type=Path, default=ROOT.parent / f'{NAME}-source',
                        help='New source folder outside the project (must not exist)')
    args = parser.parse_args()
    destination = export_source(ROOT, args.out)
    files = [path for path in destination.rglob('*') if path.is_file()]
    print(f'Exported {len(files)} files ({sum(path.stat().st_size for path in files) / 2**20:.1f} MiB) to {destination}')
    print('Review PUBLISHING.md before committing or uploading. Nothing was pushed to GitHub.')


if __name__ == '__main__':
    main()
